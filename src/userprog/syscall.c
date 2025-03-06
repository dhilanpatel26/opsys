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

  int syscall_number = *(int*) translate_vaddr(esp);

  switch (syscall_number) {
    case SYS_WAIT: {
      int pid = *(int*) translate_vaddr(esp + 1);
      int status = process_wait((tid_t) pid); // 1:1 mapping of proc to thread
      f->eax = status; // return status to user program
      break;
    }
    case SYS_EXIT: {
      int status = *(int*) translate_vaddr(esp + 1);
      exit_handler(status);
      NOT_REACHED();
    }
    default:
      NOT_REACHED();
  }
}

static void *
translate_uvaddr(void *uptr) {
  if (!is_user_vaddr(uptr)) {
    thread_exit();
  }
  
  void *kptr = pagedir_get_page(thread_current()->pagedir, uptr);
  
  if (kptr == NULL) {
    thread_exit();
  }

  return kptr;
}

// TODO: maybe move this to process.c
static void
exit_handler (int status) {
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  if (procdesc != NULL) { // initial thread doesn't have a process
    // critical section
    procdesc->exit_status = status;
    procdesc->exited = true;
    // end critical section
    sema_up(&procdesc->wait_sema);
  }
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
