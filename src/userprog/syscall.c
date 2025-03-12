#ifndef USERPROG
#define USERPROG
#endif

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);
static int wait_handler (int pid);
static bool create_handler (char *file_name, unsigned initial_size);
static bool validate_string(const char *str);
static void exit_handler (int status);
static void *translate_uvaddr(void *uptr);

static int exec_handler(char *file_name);
static int open_handler(char *file_name);
static int close_handler(int fd);

static bool remove_handler(char *file);
static int filesize_handler(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// called by user thread in kernel mode (has a process)
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  
  // relevant stack data is 4 bytes and aligned
  int *esp = f->esp; // user virtual memory

  int syscall_number = *(int*) translate_uvaddr(esp);

  switch (syscall_number) {
    case SYS_WAIT: {
      int pid = *(int*) translate_uvaddr(esp + 1);
      int status = wait_handler(pid);
      f->eax = status;
      return;
    }
    case SYS_EXIT: {
      int status = *(int*) translate_uvaddr(esp + 1);
      exit_handler(status);
      NOT_REACHED();
    }
    case SYS_CREATE: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      unsigned initial_size = *(unsigned*) translate_uvaddr(esp + 2);
      bool status = create_handler(file_name, initial_size);
      f->eax = status;
      return;
    }
    case SYS_EXEC: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      tid_t id = exec_handler(file_name);
      f->eax = id;
      
      return;
    }
    case SYS_OPEN: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      int fd = open_handler(file_name);
      f->eax = fd;
      return;
    }
    case SYS_CLOSE: {
      int fd = *(int*) translate_uvaddr(esp + 1);
      close_handler(fd);
      return;
    }
    case SYS_REMOVE:{
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      bool status = remove_handler(file_name);
      f->eax = status; 
      return;
    }
    case SYS_FILESIZE:{
      int fd = *(int*) translate_uvaddr(esp + 1);

    }
    case SYS_READ:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      void *buffer = translate_uvaddr(*(void**)(esp + 2));
      unsigned length = *(unsigned*) translate_uvaddr(esp + 3);


    }
    case SYS_SEEK:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      unsigned position = *(unsigned*) translate_uvaddr(esp + 2);
      
    }
    case SYS_TELL:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      
    }
    default:
      NOT_REACHED();
  }
}

static int
close_handler (int fd)
{
  // closing stdin or stdout is invalid
  if (fd < 2 || fd >= FILE_TABLE_SIZE) {
    return -1;
  }
  struct thread *cur = thread_current();
  struct file *file = cur->fd_table[fd];
  if (file == NULL) {
    return -1;
  }

  file_close(file);
  cur->fd_table[fd] = NULL;
  return 0;
}

static int
open_handler (char *file_name)
{
  // file_name provided as a user vaddr
  if (!validate_string(file_name)) {
    return -1;
  }
  struct file *file = filesys_open(file_name);
  if (file == NULL) {
    return -1;
  }
  
  struct thread *cur = thread_current();
  for (unsigned fd = 2; fd < FILE_TABLE_SIZE; fd++) {
    if (cur->fd_table[fd] == NULL) {
      cur->fd_table[fd] = file;
      return fd;
    }
  }
  file_close(file);
  return -1;
}

static int
wait_handler (int pid)
{
  int status = process_wait((tid_t) pid); // 1:1 mapping of proc to thread
  return status;
}

static bool
create_handler (char *file_name, unsigned initial_size)
{
  if (!validate_string(file_name)) {
    return false;
  }
  bool status = filesys_create(file_name, initial_size);
  return status;
}

static int
exec_handler(char *file_name){
  if (!validate_string(file_name)) {
    return -1;
  }
  tid_t tid = process_execute(file_name);
  if (tid == TID_ERROR) {
    return -1;
  }
  // Find the child process descriptor (ensure synchronization)
  struct thread *cur = thread_current();
  struct process_descriptor *childpd = NULL;
  struct list_elem *e;

  for (e = list_begin(&cur->procdesc->children); 
       e != list_end(&cur->procdesc->children); 
       e = list_next(e)) {
    childpd = list_entry(e, struct process_descriptor, elem);
    if (childpd->tid == tid) {
      break;
    } else {
      childpd = NULL;
    }
  }

  if (childpd == NULL || !childpd->exited) {
    return -1;  // The child process didn't load successfully
  }
  
  // sema_down(&pi->load_sema);

  return tid;
}

static bool 
remove_handler(char *file_name){
  if (!validate_string(file_name)) {
    return false;
  }
  struct thread *cur = thread_current();

//TODO: needs to ensure standard unix semantics for file removal when its open
  bool status = filesys_remove(file_name);
  return status;
}



static bool validate_string(const char *str) {
  if (str == NULL) {
    return false;
  }
  for (;; str++) {
    // exits/segfaults if invalid memory access
    translate_uvaddr((void*)str);
    if (*str == '\0') {
      break;
    }
  }
  return true;
}

static void *
translate_uvaddr(void *uptr) {
  if (uptr == NULL || !is_user_vaddr(uptr)) {
    exit_handler(-1); // invalid memory access, segfault
    NOT_REACHED();
  }
  
  void *kptr = pagedir_get_page(thread_current()->pagedir, uptr);
  
  if (kptr == NULL) {
    exit_handler(-1); // invalid memory access, segfault
    NOT_REACHED();
  }

  return kptr;
}

static void
exit_handler (int status)
{
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  /* Debugging */
  ASSERT (procdesc != NULL);
  ASSERT (procdesc->tid == cur->tid);
  
  procdesc->exit_status = status;
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
