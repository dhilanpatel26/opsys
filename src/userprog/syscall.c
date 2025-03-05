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

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  
  // relevant stack data is 4 bytes and aligned
  int *esp = f->esp; // user virtual memory

  // TODO: validate esp
  if (!is_user_vaddr(esp)) {
    thread_exit();
  }

  int *addr = pagedir_get_page(thread_current()->pagedir, esp);

  if (addr == NULL) {
    thread_exit();
  }

  int syscall_number = *addr;

  switch (syscall_number) {
    case SYS_EXIT:
      int status = *(addr + 1);
      exit_handler(status);
      NOT_REACHED();
    default:
      NOT_REACHED();
  }



  thread_exit ();
}

static void
exit_handler (int status) {
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  // critical section
  cur->procdesc->exit_status = status;
  cur->procdesc->exited = true;
  // end critical section

  sema_up(&procdesc->wait_sema);
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
