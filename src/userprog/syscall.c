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
#include <sys/types.h>

static void syscall_handler (struct intr_frame *);
static int wait_handler (int *esp);
static int create_handler (int *esp);
static bool validate_string(const char *str);
static void exit_handler (int *esp);
static void *translate_uvaddr(void *uptr);
static int exec_handler(int *esp);
static int remove_handler(int *esp);

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
    case SYS_EXEC:{
      tid_t id = exec_handler(esp);
      f->eax = id;
      
      return;

    }
    case SYS_REMOVE:{
      int success = remove_handler(esp);
      f->eax = success;
      return;

    }
    case SYS_FILESIZE:{

    }
    case SYS_READ:{

    }
    case SYS_SEEK:{
      
    }
    case SYS_TELL:{
      
    }
    default:
      NOT_REACHED();
  }
}

static int
wait_handler (int *esp)
{
  int pid = *(int*) translate_uvaddr(esp + 1);
  int status = process_wait((tid_t) pid); // 1:1 mapping of proc to thread
  return status;
}

static int
create_handler (int *esp)
{
  char *file_name = *(char**) translate_uvaddr(esp + 1);

  if (!validate_string(file_name)) {
    return -1;
  }

  unsigned initial_size = *(unsigned*) translate_uvaddr(esp + 2);

  bool success = filesys_create(file_name, initial_size);
  return success;
}
exec_handler(int *esp){
  char *file_name = *(char**) translate_uvaddr(esp + 1);

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
    struct process_descriptor *temp_child = list_entry(e, struct process_descriptor, elem);
    if (temp_child->tid == tid) {
      childpd = temp_child;
      break;
    }
  }

  if (childpd == NULL || !childpd->exited) {
    return -1;  // The child process didn't load successfully
  }
  
  // sema_down(&pi->load_sema);

  return tid;
}
remove_handler(int *esp){
  char *file_name = *(char**) translate_uvaddr(esp + 1);

  if (!validate_string(file_name)) {
    return -1;
  }
  bool success = filesys_remove(file_name);
  return success;
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
exit_handler (int *esp)
{
  struct thread *cur = thread_current();
  struct process_descriptor *procdesc = cur->procdesc;

  /* Debugging */
  ASSERT (procdesc != NULL);
  ASSERT (procdesc->tid == cur->tid);
  
  int status = *(int*) translate_uvaddr(esp + 1);
  procdesc->exit_status = status;
  
  // process cleanup handled by implicit process_exit
  thread_exit();
}
