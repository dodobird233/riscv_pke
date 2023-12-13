/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"
#include "elf.h"

extern int func_cnt;
extern char func_name[101][32];
extern elf_sym syms[101];
//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

ssize_t sys_user_backtrace(uint64 depth){
  // sprint("%d\n", func_cnt);
  // for(int i=0;i<func_cnt;i++){
  //   sprint("%s\n", func_name[i]);
  // }

  uint64 fp=current->trapframe->regs.s0;//当前在backtrace函数中
  //sprint("fp/s0: %x\n", current->trapframe->regs.s0);
  //sprint("sp: %x\n", current->trapframe->regs.sp);
  uint64 ra;
  for(int i=0;i<depth;i++){
    ra=*(uint64*)(fp+8);//获取返回地址
    //begin
    for(int i=0;i<func_cnt&&i<=100;i++){
      //在该函数名地址范围内
      if(ra>=syms[i].st_value&&ra<syms[i].st_value+syms[i].st_size){
        sprint("%s\n", func_name[i]);
        if(strcmp(func_name[i],"main")==0) return 0;
      }
    }
    //end
    //sprint("fp: %x\n", fp);
    //sprint("fp: %x\n", *(uint64*)(fp));
    //sprint("fp+8: %x\n", (fp+8));
    //sprint("fp+8: %x\n", *(uint64*)(fp+8));
    //sprint("fp+16: %x\n", (fp+16));
    //sprint("fp+16: %x\n", *(uint64*)(fp+16));
    //sprint("fp+24: %x\n", (fp+24));
    //sprint("fp+24: %x\n", *(uint64*)(fp+24));
    //sprint("fp+32: %x\n", (fp+32));
    //sprint("fp+32: %x\n", *(uint64*)(fp+32));
    fp=fp+16;
  }

  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);

    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
