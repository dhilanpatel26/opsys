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
#include "devices/input.h"
#include "lib/kernel/stdio.h"


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
static int read_handler(int fd, void *buffer, unsigned length);
static void seek_handler(int fd, unsigned position);
static unsigned tell_handler(int fd);
static struct lock filesys_lock; // filesys code is a critical section


void
syscall_init (void) 
{
  lock_init(&filesys_lock);
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
      int size = filesize_handler(fd);
      f->eax = size;
      return;

    }
    case SYS_READ:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      void *buffer = translate_uvaddr(*(void**)(esp + 2));
      unsigned length = *(unsigned*) translate_uvaddr(esp + 3);
      int bytes_read = read_handler(fd, buffer, length);
      f->eax = bytes_read;
      return;
    }
    case SYS_SEEK:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      unsigned position = *(unsigned*) translate_uvaddr(esp + 2);
      seek_handler(fd, position);
      return;

    }
    case SYS_TELL:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      unsigned position = tell_handler(fd);
      f->eax = position;
      return;
    }
    case SYS_WRITE: {
      int fd = *(int*) translate_uvaddr(esp + 1);
      const void *buffer = *(void**)(esp + 2);
      unsigned length = *(unsigned*)(esp + 3);
      int bytes_written = write_handler(fd, buffer, length);
      f->eax = bytes_written;
      return;
    }
    default:
      NOT_REACHED();
  }
}

static bool
validate_buffer (const void *buffer, unsigned length) {
  char *buf = (char *) buffer;
  for (unsigned i = 0; i < length; i += PGSIZE) {
    translate_uvaddr(buf + i);
  }
  if (length % PGSIZE != 0) {
    translate_uvaddr(buf + length - 1);
  }
}

static int
write_handler (int fd, const void *buffer, unsigned length) {
  validate_buffer(buffer, length);
  
  if (fd == 0) {
    return -1;
  }
  
  if (fd == 1) {
    putbuf(buffer, length);
    return length;
  }

  struct thread *cur = thread_current();

  struct file *file = cur->fd_table[fd];
  if (file == NULL) {
    return -1;
  }

  lock_acquire(&filesys_lock);
  int bytes_written = file_write(file, buffer, length);
  lock_release(&filesys_lock);
  
  return (bytes_written >= 0) ? bytes_written : -1;
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

  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);
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

  lock_acquire(&filesys_lock);
  struct file *file = filesys_open(file_name);
  lock_release(&filesys_lock);

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
  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);
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
  lock_acquire(&filesys_lock);
  bool status = filesys_create(file_name, initial_size);
  lock_release(&filesys_lock);
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
  // struct thread *cur = thread_current();

//TODO: needs to ensure standard unix semantics for file removal when its open
  bool status = filesys_remove(file_name);
  return status;
}

static int
filesize_handler(int fd){
  if (fd < 2 || fd >= FILE_TABLE_SIZE) {
    return -1;
  }
  struct thread *cur = thread_current();
  struct file *file = cur->fd_table[fd];
  if (file == NULL) {
    return -1;
  }

  lock_acquire(&filesys_lock);
  int length = file_length(file);
  lock_release(&filesys_lock);

  return length;
}


static int
read_handler(int fd, void *buffer, unsigned length){
  //invalid buffer
  if (buffer == NULL||!is_user_vaddr(buffer)) {
    return -1;
  }

  struct thread *cur = thread_current();
  if (fd == 0) {
    // read from stdin
    unsigned i;
    for (i = 0; i < length; i++) {
      ((uint8_t*) buffer)[i] = input_getc();
    }
    return length;
  }
  // trying to read from stout or invalid fd
  if (fd < 2 || fd >= FILE_TABLE_SIZE || cur->fd_table[fd] == NULL) {
        return -1;
  }
    
  struct file *file = cur->fd_table[fd];
  lock_acquire(&filesys_lock);  // Ensure thread safety
  int bytes_read = file_read(file, buffer, length);
  lock_release(&filesys_lock);

  return (bytes_read >= 0) ? bytes_read : -1; 
}

static void
seek_handler(int fd, unsigned position){
  struct thread *cur = thread_current();
  if (fd < 2 || fd >= FILE_TABLE_SIZE||cur->fd_table[fd]) {
    return;
  }
  struct file *file = cur->fd_table[fd];
  lock_acquire(&filesys_lock);
  file_seek(file, position);
  lock_release(&filesys_lock);
} 

static unsigned
tell_handler(int fd){
  struct thread *cur = thread_current();
  if (fd < 2 || fd >= FILE_TABLE_SIZE||cur->fd_table[fd]) {
    return -1;
  }
  struct file *file = cur->fd_table[fd];
  lock_acquire(&filesys_lock);
  unsigned position = file_tell(file);
  lock_release(&filesys_lock);
  return position;
}


static bool 
validate_string(const char *str) {
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
