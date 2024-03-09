/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "vmm.h"
#include "pmm.h"
#include "vfs.h"
#include "spike_interface/spike_utils.h"

typedef struct elf_info_t {
  struct file *f;
  process *p;
} elf_info;

//
// the implementation of allocater. allocates memory space for later segment loading.
// this allocater is heavily modified @lab2_1, where we do NOT work in bare mode.
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  elf_info *msg = (elf_info *)ctx->info;
  // we assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");

  //memset((void *)pa, 0, PGSIZE);
  user_vm_map((pagetable_t)msg->p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

static void *elf_alloc_mb_process(process*p, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // we assume that size of proram segment is smaller than a page.
  kassert(size < PGSIZE);
  void *pa = alloc_page();
  if (pa == 0) panic("uvmalloc mem alloc falied\n");
  //sprint("allocpage:%lx\n",pa);
  //memset((void *)pa, 0, PGSIZE);
  user_vm_map(p->pagetable, elf_va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ | PROT_EXEC, 1));

  return pa;
}

//
// actual file reading, using the vfs file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  vfs_lseek(msg->f, offset, SEEK_SET);
  return vfs_read(msg->f, dest, nb);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// init elf_ctx, using vfs
//
elf_status elf_init_vfs(elf_ctx *ctx, struct file*f) {
  // load the elf header
  vfs_lseek(f,0,0);
  if (vfs_read(f, (char*)&ctx->ehdr, sizeof(ctx->ehdr)) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions.
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ ) //seek the last mapped region
      if( (process*)(((elf_info*)(ctx->info))->p)->mapped_info[j].va == 0x0 ) break;

    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].va = ph_addr.vaddr;
    ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      ((process*)(((elf_info*)(ctx->info))->p))->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    ((process*)(((elf_info*)(ctx->info))->p))->total_mapped_region ++;
  }

  return EL_OK;
}

//
// load the elf segments to memory regions, using vfs
//
elf_status elf_load_vfs(elf_ctx *ctx,struct file*f,process*p) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    vfs_lseek(f,off,0);
    if (vfs_read(f, (void *)&ph_addr, sizeof(ph_addr)) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb_process(p, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    vfs_lseek(f,ph_addr.off,0);
    if (vfs_read(f, dest, ph_addr.memsz) != ph_addr.memsz)
      return EL_EIO;

    // record the vm region in proc->mapped_info. added @lab3_1
    int j;
    for( j=0; j<PGSIZE/sizeof(mapped_region); j++ ) //seek the last mapped region
      if(p->mapped_info[j].va == 0x0 ) break;

    p->mapped_info[j].va = ph_addr.vaddr;
    p->mapped_info[j].npages = 1;

    // SEGMENT_READABLE, SEGMENT_EXECUTABLE, SEGMENT_WRITABLE are defined in kernel/elf.h
    if( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE) ){
      p->mapped_info[j].seg_type = CODE_SEGMENT;
      sprint( "CODE_SEGMENT added at mapped info offset:%d\n", j );
    }else if ( ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE) ){
      p->mapped_info[j].seg_type = DATA_SEGMENT;
      sprint( "DATA_SEGMENT added at mapped info offset:%d\n", j );
    }else
      panic( "unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    p->total_mapped_region ++;
  }

  return EL_OK;
}

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p, char *filename) {
  sprint("Application: %s\n", filename);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = vfs_open(filename, O_RDONLY);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the vfs file
  vfs_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

//load the elf of user application, by using vfs.
void load_bincode_from_vfs_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;


  struct file* f=vfs_open(arg_bug_msg.argv[0], O_RDONLY);

  // init elfloader context. elf_init() is defined above.
  if (elf_init_vfs(&elfloader, f) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load_vfs(&elfloader,f,p) != EL_OK) panic("Fail on loading elf.\n");

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  vfs_close(f);

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}

void reload_elf_exec(process *p,char*pathpa){
  sprint("Application: %s\n", pathpa); 
  elf_ctx elfctx;
  // read from new elf
  struct file*f=vfs_open(pathpa,O_RDONLY);
  if(vfs_read(f,(char*)&(elfctx.ehdr),sizeof(elfctx.ehdr))!=sizeof(elfctx.ehdr)) panic("read elf header error");

  elf_prog_header ph_addr;
  int i, off;
  elf_ctx*ctx=&elfctx;
  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers from  offset of program segment headers
    vfs_lseek(f,off,0);
    if(vfs_read(f,(char*)&(ph_addr),sizeof(ph_addr))!=sizeof(ph_addr)) panic("read elf segment header error");
    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) panic("read elf segment header error");
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) panic("read elf segment header error");

    // modify the vm region in proc->mapped_info to new elf segments.
    if(ph_addr.flags == (SEGMENT_READABLE|SEGMENT_EXECUTABLE)){
      int j;
      for(j=0;j<PGSIZE/sizeof(mapped_region);j++){ 
        if(p->mapped_info[j].seg_type==CODE_SEGMENT){
          // unmap old page, can not free because farther process use the same pa
          // fork() ->p1 --------------------------->error in code segment
          //   |-> p0 ->exec() -> free code segment physically
          user_vm_unmap(p->pagetable,p->mapped_info[j].va,PGSIZE,0);
          // map new elf
          p->mapped_info[j].va = ph_addr.vaddr;
          p->mapped_info[j].npages = 1;                
          // allocate memory block before elf loading
          void *dest = elf_alloc_mb_process(p, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
          // actual loading
          vfs_lseek(f,ph_addr.off,0);
          if(vfs_read(f,dest,ph_addr.memsz)!=ph_addr.memsz){
            panic("actual loading elf code segment error");
          }
          sprint("CODE_SEGMENT added at mapped info offset:%d\n",j);
          break;
        }
      }
    }else if (ph_addr.flags == (SEGMENT_READABLE|SEGMENT_WRITABLE)){
      int j;
      for(j=0;j<PGSIZE/sizeof(mapped_region);j++){ 
        if(p->mapped_info[j].seg_type==DATA_SEGMENT){
          user_vm_unmap(p->pagetable,p->mapped_info[j].va,PGSIZE,1);
          p->mapped_info[j].va = ph_addr.vaddr;
          p->mapped_info[j].npages = 1;
          // allocate memory block before elf loading
          void *dest = elf_alloc_mb_process(p, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);
          // actual loading
          vfs_lseek(f,ph_addr.off,0);
          if(vfs_read(f,dest,ph_addr.memsz)!=ph_addr.memsz)
            panic("actual loading elf data segment error");
          sprint("DATA_SEGMENT added at mapped info offset:%d\n",j);
          break;
        }
      }
    }else
      panic("unknown program segment encountered, segment flag:%d.\n", ph_addr.flags );

    p->total_mapped_region ++;
  }

  
  vfs_close(f);
  p->trapframe->epc=elfctx.ehdr.entry;
  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}