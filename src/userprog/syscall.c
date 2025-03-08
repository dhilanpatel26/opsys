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
static void exit_handler (int status);
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
      int *pid_ptr = (int*) translate_uvaddr(esp + 1);
      if (pid_ptr == NULL) {
        f->eax = -1;
        return;
      }
      int pid = *pid_ptr;
      int status = process_wait((tid_t) pid); // 1:1 mapping of proc to thread
      f->eax = status; // return status to user program
      break;
    }
    case SYS_EXIT: {
      int *status_ptr = (int*) translate_uvaddr(esp + 1);
      if (status_ptr == NULL) {
        f->eax = -1;
        return;
      }
      int status = *status_ptr;
      exit_handler(status);
      NOT_REACHED();
    }
    case SYS_CREATE: {
      char **file_name_ptr = (char**) translate_uvaddr(esp + 1);
      if (file_name_ptr == NULL) {
        f->eax = -1;
        return;
      }
      char *file_name = *file_name_ptr;
      if (file_name == NULL) {
        f->eax = -1;
        return;
      }
      if (!validate_string(file_name)) {
        f->eax = -1;
        return;
      }
      unsigned *initial_size_ptr = (unsigned*) translate_uvaddr(esp + 2);
      if (initial_size_ptr == NULL) {
        f->eax = -1;
        return;
      }
      unsigned initial_size = *initial_size_ptr;
      bool success = filesys_create(file_name, initial_size);
      f->eax = success;
      break;
    }
    default:
      NOT_REACHED();
  }
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
exit_handler (int status) {
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  ASSERT (procdesc != NULL);
  procdesc->exit_status = status;
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
