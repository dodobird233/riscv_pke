#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include <string.h>
static void handle_instruction_access_fault() { panic("Instruction access fault!"); }

static void handle_load_access_fault() { panic("Load access fault!"); }

static void handle_store_access_fault() { panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { panic("Illegal instruction!"); }

static void handle_misaligned_load() { panic("Misaligned Load!"); }

static void handle_misaligned_store() { panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

struct stat st;
//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  uint64 perror=read_csr(mepc);
  //sprint("mepc:%p\n",perror);
  addr_line err_line;
  //sprint("line_ind:%d\n",current->line_ind);
  for(int i=0;i<current->line_ind;i++){
    if(current->line[i].addr==perror){
      err_line=current->line[i];
      break;
    }
  }
  char path[200];
  char filebuf[8192];
  strcpy(path,current->dir[current->file[err_line.file].dir]);
  int path_len=strlen(current->dir[current->file[err_line.file].dir]);
  path[path_len]='/';
  strcpy(path+path_len+1,current->file[err_line.file].file);
  path_len+=strlen(current->file[err_line.file].file)+1;
  path[path_len]='\0';
  sprint("Runtime error at %s:%d\n",path,err_line.line);
  spike_file_t*sf=spike_file_open(path,O_RDONLY,0); //get fd
  spike_file_stat(sf,&st); //get size
  spike_file_read(sf,filebuf,st.st_size);
  spike_file_close(sf);
  int offset=0,linecnt=0;
  while(offset<st.st_size){
    if(filebuf[offset]=='\n') linecnt++;
    if(linecnt==err_line.line-1){
      char temp[200];
      int right=offset+1;
      while(right<st.st_size&&filebuf[right]!='\n') right++;
      for(int i=0;i<right-offset-1;i++) temp[i]=filebuf[offset+1+i];
      temp[right-offset]='\0';
      sprint("%s\n",temp);
      break;
    }
    offset++;
  }

  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
      handle_illegal_instruction();
      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}
