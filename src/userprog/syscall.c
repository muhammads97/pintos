#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"

static void syscall_handler (struct intr_frame *);
void is_addr_valid(int * addr);
bool sys_create(int * addr);
int sys_exec(int * addr);
void sys_exit(int * addr);
tid_t sys_wait(int * addr);
void proc_exit(int status);
bool sys_remove(int * addr);
bool sys_create(int * addr);
int sys_open(int * addr);
int sys_filesize(int * addr);
int sys_read(int * addr);
int sys_write(int * addr);
void sys_seek(int * addr);
int sys_tell(int * addr);
void sys_close(int * addr);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // interrupt happened for a sys call, the interrupt frame has the information
  // the stack pointer has the address of the sys call number
  // we need to check for the address to be valid
  // printf("sys call\n");
  int *addr = f->esp;
  is_addr_valid(addr);
  
  int sys_call_num = * addr;
  switch(sys_call_num){
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXIT:
      sys_exit(addr);
      break;
    case SYS_EXEC:
      f->eax = sys_exec(addr);
      break;
    case SYS_WAIT:
      f->eax = sys_wait(addr);
      break;
    case SYS_CREATE:
      f->eax = sys_create(addr);
      break;
    case SYS_REMOVE:
      f->eax = sys_remove(addr);
      break;
    case SYS_OPEN:
      f->eax = sys_open(addr);
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize(addr);
      break;
    case SYS_READ:
      f->eax = sys_read(addr);
      break;
    case SYS_WRITE:
      f->eax = sys_write(addr);
      break;
    case SYS_SEEK:
      sys_seek(addr);
      break;
    case SYS_TELL:
      f->eax = sys_tell(addr);
      break;
    case SYS_CLOSE:
      sys_close(addr);
      break;

    default:
      printf ("system call!\n");
      proc_exit(-1);
      break;
  }

  
}

void is_addr_valid(int * addr){
  char * byte_addr = addr;
  if(!is_user_vaddr(byte_addr) || !pagedir_get_page(thread_current()->pagedir, byte_addr)){
    proc_exit(-1);
  }
  if(!is_user_vaddr(byte_addr + 1) || !pagedir_get_page(thread_current()->pagedir, byte_addr + 1)){
    proc_exit(-1);
  }
  if(!is_user_vaddr(byte_addr + 2) || !pagedir_get_page(thread_current()->pagedir, byte_addr + 2)){
    proc_exit(-1);
  }
  if(!is_user_vaddr(byte_addr + 3) || !pagedir_get_page(thread_current()->pagedir, byte_addr + 3)){
    proc_exit(-1);
  }
  if(!is_user_vaddr(addr) || !pagedir_get_page(thread_current()->pagedir, addr)){
    proc_exit(-1);
  }
}

void sys_exit(int * addr){
  is_addr_valid(addr + 1);
  // printf("3ddaa! %x\n", (addr+1));
  // printf("%x\n", thread_current()->pagedir);
  int status = *(addr+1);
  // printf("%x  %x\n", status);
  proc_exit(status);
}

void proc_exit(int status){
  thread_current()->exit_status = status;
  // list_remove(&thread_current()->to_be_child);
  if(thread_current()->parent->ch_tid == thread_current()->tid){
    thread_current()->parent->child_exit_status = status;
    sema_up(&thread_current()->parent->wait_for_child);
  }
  thread_exit();
}

int sys_exec(int * addr){
  // if(thread_current()->depth > 25) return -1;
  is_addr_valid(addr + 1);
  char *fn = *(addr+1);
  is_addr_valid(fn);
  return process_execute(fn);

}

tid_t sys_wait(int * addr){
  is_addr_valid(addr + 1);
  tid_t id = *(addr+1);
  return process_wait(id);
}

bool sys_create(int * addr){
  is_addr_valid(addr + 1);
  is_addr_valid(addr + 2);
  char * file_name = *(addr+1);
  if(!is_user_vaddr(file_name) || !pagedir_get_page(thread_current()->pagedir, file_name)){
    proc_exit(-1);
  }
  int size = *(addr+2);
  if(file_name == NULL) proc_exit(-1);
  sema_down(&file_access);
  bool temp = filesys_create(file_name, size);
  sema_up(&file_access);
  return temp;
}

bool sys_remove(int * addr){
  is_addr_valid(addr + 1);
  char * file_name = *(addr+1);
  if(file_name == NULL) proc_exit(-1);
  if(!is_user_vaddr(file_name) || !pagedir_get_page(thread_current()->pagedir, file_name)){
    proc_exit(-1);
  }
  sema_down(&file_access);
  bool temp = filesys_remove(file_name);
  sema_up(&file_access);
  return temp;
}

int sys_open(int * addr){
  is_addr_valid(addr+1);
  char * file_name = *(addr+1);
  if(file_name == NULL) proc_exit(-1);
  if(!is_user_vaddr(file_name) || !pagedir_get_page(thread_current()->pagedir, file_name)){
    proc_exit(-1);
  }
  
  sema_down(&file_access);
  struct file* f = filesys_open(file_name);
  sema_up(&file_access);
  if(f == NULL){
    return -1;
  }

  thread_current()->descriptor++;
  f->fd = thread_current()->descriptor;
  list_push_back(&thread_current()->files, &f->elem);
  return f->fd;
}

int sys_filesize(int * addr){
  is_addr_valid(addr+1);
  int fd = *(addr+1);

  struct file* found;
  struct list_elem * e;
  for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
       e = list_next(e))
  {
      struct file * f = list_entry (e, struct file, elem);
      if(f->fd == fd){
        found = f;
        break;
      }
  }
  if(found == NULL) return -1;

  return file_length(found);
}

int sys_read(int * addr){
  is_addr_valid(addr+1);
  is_addr_valid(addr+2);
  is_addr_valid(addr+3);
  int fd = *(addr+1);
  uint8_t* buffer = *(addr+2);

  if(buffer == NULL) proc_exit(-1);
  if(!is_user_vaddr(buffer) || !pagedir_get_page(thread_current()->pagedir, buffer)){
    proc_exit(-1);
  }

  int size = *(addr+3);
  int bytes_read = 0;

  if(fd == 0){
    for(int i = 0; i < size; i++){
      *(buffer + i) = input_getc();
    }
    bytes_read = size;
  } else {
    struct file* found = NULL;
    struct list_elem * e;
    for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
         e = list_next(e)) {
      struct file * f = list_entry (e, struct file, elem);
      if(f->fd == fd){
        found = f;
        break;
      }
    }
    if(found == NULL) proc_exit(-1);
    sema_down(&file_access);
    bytes_read = file_read(found, buffer, size);
    sema_up(&file_access);
  }
  return bytes_read;
}

int sys_write(int * addr){
  is_addr_valid(addr+1);
  is_addr_valid(addr+2);
  is_addr_valid(addr+3);
  int fd = *(addr+1);
  uint8_t * buffer = *(addr+2);
  if(buffer == NULL) proc_exit(-1);
  if(!is_user_vaddr(buffer) || !pagedir_get_page(thread_current()->pagedir, buffer)){
    proc_exit(-1);
  }
  int size = *(addr+3);
  int bytes_write = 0;
  if(fd == 1){
    bytes_write = size;
    int i = 0;
    while(size > 256){
      putbuf((buffer + i), 256);
      i += 256;
      size -= 256;
    }
    putbuf((buffer + i), size);
  }else {
    struct file* found = NULL;
    struct list_elem * e;
    for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
         e = list_next(e)) {
      struct file * f = list_entry (e, struct file, elem);
      if(f->fd == fd){
        found = f;
        break;
      }
    }
    if(found == NULL) proc_exit(-1);
    if(found->deny_write) proc_exit(-1);
    sema_down(&file_access);
    bytes_write = file_write(found, buffer, size);
    sema_up(&file_access);
  }
  return bytes_write;
}

void sys_seek(int * addr){
  is_addr_valid(addr + 1);
  is_addr_valid(addr + 2);
  int fd = *(addr + 1);
  int pos = * (addr + 2);
  struct file* found;
  struct list_elem * e;
  for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
         e = list_next(e)) {
    struct file * f = list_entry (e, struct file, elem);
    if(f->fd == fd){
      found = f;
      break;
    }
  }
  if(found == NULL) return;
  file_seek(found, pos);
}

int sys_tell(int * addr){
  is_addr_valid(addr + 1);
  int fd = *(addr + 1);
  struct file* found;
  struct list_elem * e;
  for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
         e = list_next(e)) {
    struct file * f = list_entry (e, struct file, elem);
    if(f->fd == fd){
      found = f;
      break;
    }
  }
  if(found == NULL) return -1;
  return file_tell(found);
}

void sys_close(int * addr){
  is_addr_valid(addr +1);
  int fd = *(addr+1);
  // printf("handle %d\n", fd);
  struct file* found = NULL;
  struct list_elem * e;
  for (e = list_begin(&thread_current()->files); e != list_end(&thread_current()->files);
         e = list_next(e)) {
    struct file * f = list_entry (e, struct file, elem);
    if(f->fd == fd){
      found = f;
      list_remove(e);
      break;
    }
  }
  if(found == NULL) proc_exit(-1);
  sema_down(&file_access);
  file_close(found);
  sema_up(&file_access);
}