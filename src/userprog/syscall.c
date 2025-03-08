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

static void syscall_handler (struct intr_frame *);
static int wait_handler (int *esp);
static int create_handler (int *esp);
static bool validate_string(const char *str);
static void exit_handler (int *esp);
static void *translate_uvaddr(void *uptr);

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
      int success = wait_handler(esp);
      f->eax = success;
      return;
    }
    case SYS_EXIT: {
      exit_handler(esp);
      NOT_REACHED();
    }
    case SYS_CREATE: {
      int success = create_handler(esp);
      f->eax = success;
      return;
    }
    default:
      NOT_REACHED();
  }
}

static int
wait_handler (int *esp)
{
  int *pid_ptr = (int*) translate_uvaddr(esp + 1);
  if (pid_ptr == NULL) {
    return -1;
  }
  int pid = *pid_ptr;
  int status = process_wait((tid_t) pid); // 1:1 mapping of proc to thread
  return status;
}

static int
create_handler (int *esp)
{
  char **file_name_ptr = (char**) translate_uvaddr(esp + 1);
  if (file_name_ptr == NULL) {
    return -1;
  }
  char *file_name = *file_name_ptr;
  if (file_name == NULL) {
    return -1;
  }
  if (!validate_string(file_name)) {
    return -1;
  }
  unsigned *initial_size_ptr = (unsigned*) translate_uvaddr(esp + 2);
  if (initial_size_ptr == NULL) {
    return -1;
  }
  unsigned initial_size = *initial_size_ptr;
  bool success = filesys_create(file_name, initial_size);
  return success;
}

static bool validate_string(const char *str) {
  if (str == NULL) {
    return false;
  }
  for (;; str++) {
    if (translate_uvaddr((void*)str) == NULL) {
      return false;
    }
    if (*str == '\0') {
      break;
    }
  }
  return true;
}

static void *
translate_uvaddr(void *uptr) {
  if (uptr == NULL || !is_user_vaddr(uptr)) {
    return NULL;
  }
  
  void *kptr = pagedir_get_page(thread_current()->pagedir, uptr);
  
  if (kptr == NULL) {
    return NULL;
  }

  return kptr;
}

static void
exit_handler (int *esp)
{
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  /* Debugging */
  ASSERT (procdesc != NULL);
  ASSERT (procdesc->tid == cur->tid);
  
  int *status_ptr = (int*) translate_uvaddr(esp + 1);
  procdesc->exit_status = status_ptr == NULL ? -1 : *status_ptr;
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
