/*
 * Interface functions between file system and kernel/processes. added @lab4_1
 */

#include "proc_file.h"

#include "hostfs.h"
#include "pmm.h"
#include "process.h"
#include "ramdev.h"
#include "rfs.h"
#include "riscv.h"
#include "spike_interface/spike_file.h"
#include "spike_interface/spike_utils.h"
#include "util/functions.h"
#include "util/string.h"

//
// initialize file system
//
void fs_init(void) {
  // initialize the vfs
  vfs_init();

  // register hostfs and mount it as the root
  if( register_hostfs() < 0 ) panic( "fs_init: cannot register hostfs.\n" );
  struct device *hostdev = init_host_device("HOSTDEV");
  vfs_mount("HOSTDEV", MOUNT_AS_ROOT);

  // register and mount rfs
  if( register_rfs() < 0 ) panic( "fs_init: cannot register rfs.\n" );
  struct device *ramdisk0 = init_rfs_device("RAMDISK0");
  rfs_format_dev(ramdisk0);
  vfs_mount("RAMDISK0", MOUNT_DEFAULT);
}

//
// initialize a proc_file_management data structure for a process.
// return the pointer to the page containing the data structure.
//
proc_file_management *init_proc_file_management(void) {
  proc_file_management *pfiles = (proc_file_management *)alloc_page();
  pfiles->cwd = vfs_root_dentry; // by default, cwd is the root
  pfiles->nfiles = 0;

  for (int fd = 0; fd < MAX_FILES; ++fd)
    pfiles->opened_files[fd].status = FD_NONE;

  sprint("FS: created a file management struct for a process.\n");
  return pfiles;
}

//
// reclaim the open-file management data structure of a process.
// note: this function is not used as PKE does not actually reclaim a process.
//
void reclaim_proc_file_management(proc_file_management *pfiles) {
  free_page(pfiles);
  return;
}

//
// get an opened file from proc->opened_file array.
// return: the pointer to the opened file structure.
//
struct file *get_opened_file(int fd) {
  struct file *pfile = NULL;

  // browse opened file list to locate the fd
  for (int i = 0; i < MAX_FILES; ++i) {
    pfile = &(current->pfiles->opened_files[i]);  // file entry
    if (i == fd) break;
  }
  if (pfile == NULL) panic("do_read: invalid fd!\n");
  return pfile;
}

//
// open a file named as "pathname" with the permission of "flags".
// return: -1 on failure; non-zero file-descriptor on success.
//
int do_open(char *pathname, int flags) {
  struct file *opened_file = NULL;
  //wfs_open最终会根据pathname逐层查找
  //若为相对地址,在该处转为绝对地址
  //sprint("FS: open file pre :%s\n",pathname);
  if (pathname[0]== '.') {
    char buf[MAX_PATH_LEN];//构造的绝对路径
    memset(buf,'\0',MAX_PATH_LEN);
    struct dentry *p=current->pfiles->cwd;
    if(strlen(pathname)>1&&pathname[1]=='.'){//..
      p=p->parent;
    }
    while(p!=NULL){
      char path[MAX_PATH_LEN];
      memset(path,'\0',MAX_PATH_LEN);
      memcpy(path,buf,strlen(buf));
      memset(buf,'\0',MAX_PATH_LEN);
      memcpy(buf,p->name,strlen(p->name));
      if(p!=vfs_root_dentry)
        strcat(buf,"/");
      strcat(buf,path);
      p=p->parent;
    }
    if(strlen(pathname)>1&&pathname[1]=='.')
      strcat(buf,pathname+3);
    else if(pathname[0]=='.')
      strcat(buf,pathname+2);
    //memset(pathname,'\0',MAX_PATH_LEN);
    //memcpy(pathname,buf,strlen(buf));
    pathname=buf;
  }
  
  //sprint("FS: open file after :%s\n",pathname);
  if ((opened_file = vfs_open(pathname, flags)) == NULL) return -1;

  int fd = 0;
  if (current->pfiles->nfiles >= MAX_FILES) {
    panic("do_open: no file entry for current process!\n");
  }
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  //sprint("FS: opened a file %s with fd %d.\n", pathname, fd);
  return fd;
}

//
// read content of a file ("fd") into "buf" for "count".
// return: actual length of data read from the file.
//
int do_read(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->readable == 0) panic("do_read: no readable file!\n");

  char buffer[count + 1];
  int len = vfs_read(pfile, buffer, count);
  buffer[count] = '\0';
  strcpy(buf, buffer);
  return len;
}

//
// write content ("buf") whose length is "count" to a file "fd".
// return: actual length of data written to the file.
//
int do_write(int fd, char *buf, uint64 count) {
  struct file *pfile = get_opened_file(fd);

  if (pfile->writable == 0) panic("do_write: cannot write file!\n");

  int len = vfs_write(pfile, buf, count);
  return len;
}

//
// reposition the file offset
//
int do_lseek(int fd, int offset, int whence) {
  struct file *pfile = get_opened_file(fd);
  return vfs_lseek(pfile, offset, whence);
}

//
// read the vinode information
//
int do_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_stat(pfile, istat);
}

//
// read the inode information on the disk
//
int do_disk_stat(int fd, struct istat *istat) {
  struct file *pfile = get_opened_file(fd);
  return vfs_disk_stat(pfile, istat);
}

//
// close a file
//
int do_close(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_close(pfile);
}

//
// open a directory
// return: the fd of the directory file
//
int do_opendir(char *pathname) {
  struct file *opened_file = NULL;
  //同open_file
  //sprint("FS: open dir pre : %s\n",pathname);
  if (pathname[0]== '.') {
    char buf[MAX_PATH_LEN];//构造的绝对路径
    memset(buf,'\0',MAX_PATH_LEN);
    struct dentry *p=current->pfiles->cwd;
    if(strlen(pathname)>1&&pathname[1]=='.'){//..
      p=p->parent;
    }
    while(p!=NULL){
      char path[MAX_PATH_LEN];
      memset(path,'\0',MAX_PATH_LEN);
      memcpy(path,buf,strlen(buf));
      memset(buf,'\0',MAX_PATH_LEN);
      memcpy(buf,p->name,strlen(p->name));
      if(p!=vfs_root_dentry)
        strcat(buf,"/");
      strcat(buf,path);
      //sprint("in open  p: %s buf: %s\n",p->name,buf);
      p=p->parent;
    }
    //去除前导.
    if(strlen(pathname)>1&&pathname[1]=='.')
      strcat(buf,pathname+3);
    else if(pathname[0]=='.')
      strcat(buf,pathname+2);
    //memset(pathname,'\0',MAX_PATH_LEN);
    //memcpy(pathname,buf,strlen(buf));
    pathname=buf;
  }
  //sprint("FS: open dir after : %s\n",pathname);
  if ((opened_file = vfs_opendir(pathname)) == NULL) return -1;
  //sprint("success\n");
  int fd = 0;
  struct file *pfile;
  for (fd = 0; fd < MAX_FILES; ++fd) {
    pfile = &(current->pfiles->opened_files[fd]);
    if (pfile->status == FD_NONE) break;
  }
  if (pfile->status != FD_NONE)  // no free entry
    panic("do_opendir: no file entry for current process!\n");

  // initialize this file structure
  memcpy(pfile, opened_file, sizeof(struct file));

  ++current->pfiles->nfiles;
  return fd;
}

//
// read a directory entry
//
int do_readdir(int fd, struct dir *dir) {
  struct file *pfile = get_opened_file(fd);
  return vfs_readdir(pfile, dir);
}

//
// make a new directory
//
int do_mkdir(char *pathname) {
  return vfs_mkdir(pathname);
}

//
// close a directory
//
int do_closedir(int fd) {
  struct file *pfile = get_opened_file(fd);
  return vfs_closedir(pfile);
}

//
// create hard link to a file
//
int do_link(char *oldpath, char *newpath) {
  return vfs_link(oldpath, newpath);
}

//
// remove a hard link to a file
//
int do_unlink(char *path) {
  return vfs_unlink(path);
}

int do_rcwd(proc_file_management*pfm,char*pathname){
  if(pfm->cwd){
    if(pfm->cwd->parent==NULL){
      strcpy(pathname,"/");
      //pathname[0]='/';
      pathname[1]='\0';
      return 0;
    }else{
      memcpy(pathname,pfm->cwd->name,strlen(pfm->cwd->name));
      struct dentry *parent = pfm->cwd->parent;
      while(parent!=NULL){//向上找到根目录
        char path[MAX_PATH_LEN];
        memset(path,'\0',MAX_PATH_LEN);
        memcpy(path,pathname,strlen(pathname));//先将pathname暂存到path
        memset(pathname,'\0',MAX_PATH_LEN);
        memcpy(pathname,parent->name,strlen(parent->name));//加上父目录的name
        if(parent!=vfs_root_dentry)
          strcat(pathname,"/");
        strcat(pathname,path);//将原来的pathname接在后面
        parent=parent->parent;
      }
      pathname[strlen(pathname)]='\0';
    }
      return 0;
  }
  return -1;
}
int do_ccwd(proc_file_management*pfm,char*pathname) {
  int fd;
  if((fd=do_opendir(pathname))<0) return -1;
  //sprint("%d\n",fd);
  pfm->cwd = pfm->opened_files[fd].f_dentry;
  //sprint("%s\n",pfm->cwd->name);
  do_closedir(fd);
  return 0;
}
