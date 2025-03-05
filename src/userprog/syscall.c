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

static void syscall_handler (struct intr_frame *);

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
      break;
    default:
      NOT_REACHED();
  }



  thread_exit ();
}
