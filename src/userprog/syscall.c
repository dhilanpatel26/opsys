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
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "vm/frame.h"
#include "threads/malloc.h"

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
static int write_handler (int fd, const void *buffer, unsigned length);
static bool validate_buffer (const void *buffer, unsigned length);

struct lock filesys_lock; // filesys code is a critical section


void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// called by user thread in kernel mode (has a process)
static void
syscall_handler (struct intr_frame *f) 
{  
  // printf("system call!\n");

  // relevant stack data is 4 bytes and aligned
  int *esp = f->esp; // user virtual memory

  int syscall_number = *(int*) translate_uvaddr(esp);
  translate_uvaddr((void*)((char*)esp + 3)); // ensure entire syscall number is valid

  switch (syscall_number) {
    case SYS_WAIT: {
      int pid = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      int status = wait_handler(pid);
      f->eax = status;
      return;
    }
    case SYS_EXIT: {
      // When reading an int, we should validate all 4 of its bytes
      // Read the exit status byte by byte to handle page boundary crossing
      int status = 0;
      for (int i = 0; i < 4; i++) {
        uint8_t *byte_ptr = (uint8_t*)translate_uvaddr((void*)((char*)(esp + 1) + i));
        status |= ((*byte_ptr) << (i * 8));
      }
      exit_handler(status);
      NOT_REACHED();
    }
    case SYS_CREATE: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      unsigned initial_size = *(unsigned*) translate_uvaddr(esp + 2);
      translate_uvaddr((void*)((char*)esp + 11));
      bool status = create_handler(file_name, initial_size);
      f->eax = status;
      return;
    }
    case SYS_EXEC: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      tid_t id = exec_handler(file_name);
      f->eax = id;
      
      return;
    }
    case SYS_OPEN: {
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      int fd = open_handler(file_name);
      f->eax = fd;
      return;
    }
    case SYS_CLOSE: {
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      close_handler(fd);
      return;
    }
    case SYS_REMOVE:{
      char *file_name = *(char**) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      bool status = remove_handler(file_name);
      f->eax = status; 
      return;
    }
    case SYS_FILESIZE:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      int size = filesize_handler(fd);
      f->eax = size;
      return;

    }
    case SYS_READ:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      void *buffer = *(void**) translate_uvaddr(esp + 2);
      translate_uvaddr((void*)((char*)esp + 11));
      unsigned length = *(unsigned*) translate_uvaddr(esp + 3);
      translate_uvaddr((void*)((char*)esp + 15));
      int bytes_read = read_handler(fd, buffer, length);
      f->eax = bytes_read;
      return;
    }
    case SYS_SEEK:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      unsigned position = *(unsigned*) translate_uvaddr(esp + 2);
      translate_uvaddr((void*)((char*)esp + 11));
      seek_handler(fd, position);
      return;

    }
    case SYS_TELL:{
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      unsigned position = tell_handler(fd);
      f->eax = position;
      return;
    }
    case SYS_WRITE: {
      int fd = *(int*) translate_uvaddr(esp + 1);
      translate_uvaddr((void*)((char*)esp + 7));
      const void *buffer = *(void**) translate_uvaddr(esp + 2);
      translate_uvaddr((void*)((char*)esp + 11));
      unsigned length = *(unsigned*) translate_uvaddr(esp + 3);
      translate_uvaddr((void*)((char*)esp + 15));
      int bytes_written = write_handler(fd, buffer, length);
      f->eax = bytes_written;
      return;
    }
    case SYS_HALT: {
      shutdown_power_off();
      NOT_REACHED();
      return;
    }
    default:
      NOT_REACHED();
  }
}

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
  UDST must be below PHYS_BASE.
  Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
validate_buffer (const void *buffer, unsigned length) {
  if (buffer == NULL) {
    // printf("buffer is NULL\n");
    return false;
  }

  if (length == 0) {
    // printf("length is 0\n");
    return true;
  }

  // check if buffer is a user vaddr
  if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + length - 1)) {
    // printf("buffer is not a user vaddr\n");
    return false;
  }

  const uint8_t *buf = (const uint8_t *) buffer;
  if (get_user(buf) == -1) {
    // printf("get_user failed\n");
    return false;
  }

  if (length > 1 && get_user(buf + length - 1) == -1) {
    // printf("get_user failed at end of buffer\n");
    return false;
  }

  // check the bytes at the page boundaries
  for (unsigned i = PGSIZE; i < length; i += PGSIZE) {
    if (get_user(buf + i) == -1) {
      // printf("get_user failed at page boundary\n");
      return false;
    }
  }

  return true;
}

static void *
kernel_buffer_copy (const void *user_buffer, unsigned length) {

  // follows method of only accessing through user page table

  if (length == 0) {
    return NULL;
  }

  // buffer is a user vaddr
  if (!validate_buffer(user_buffer, length)) {
    return NULL;
  }

  // calculate how many pages we need based on whats in the buffer
  size_t page_count = (length + PGSIZE - 1) / PGSIZE;

  void *kernel_buffer = palloc_get_multiple(0, page_count);
  if (kernel_buffer == NULL) {
    return NULL;
  }

  const char *source = (char*) user_buffer;
  char *dst = kernel_buffer;

  // Copy byte by byte using get_user
  for (unsigned i = 0; i < length; i++) {
    int byte = get_user((const uint8_t *)source + i);
    if (byte == -1) {
      palloc_free_multiple(kernel_buffer, page_count);
      return NULL;
    }
    dst[i] = (uint8_t)byte;
  }

  return kernel_buffer;
}

static int
write_handler (int fd, const void *user_buffer, unsigned length) {

  // printf("write_handler called\n");

  if (length == 0) {
    return 0;
  }

  if(!validate_buffer(user_buffer, length)){
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
  }

  if (fd <= 0 || fd >= FILE_TABLE_SIZE) {
    return -1;
  }
  
  size_t page_count = (length + PGSIZE - 1) / PGSIZE;

  #ifdef VM
  // Pin the user buffer pages in memory to prevent eviction during copy
  for (unsigned i = 0; i < length; i += PGSIZE) {
    void *page_addr = pg_round_down((uint8_t*)user_buffer + i);
    struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, page_addr);
    if (spte != NULL) {
      void *kpage = pagedir_get_page(thread_current()->pagedir, page_addr);
      if (kpage != NULL)
        frame_pin(kpage);
    }
  }
  #endif

  if (fd == 1) {
    void *kernel_buffer = kernel_buffer_copy(user_buffer, length);
    if (kernel_buffer == NULL) {
      #ifdef VM
      // Unpin buffer pages if copy failed
      for (unsigned i = 0; i < length; i += PGSIZE) {
        void *page_addr = pg_round_down((uint8_t*)user_buffer + i);
        struct sup_page_table_entry *spte = 
          sup_page_table_lookup(&thread_current()->spt, page_addr);
        if (spte != NULL) {
          void *kpage = pagedir_get_page(thread_current()->pagedir, page_addr);
          if (kpage != NULL)
            frame_unpin(kpage);
        }
      }
      #endif
      return -1;
    }

    putbuf(kernel_buffer, length);
    palloc_free_multiple(kernel_buffer, page_count);

    #ifdef VM
    // Unpin after copy is complete
    for (unsigned i = 0; i < length; i += PGSIZE) {
      void *page_addr = pg_round_down((uint8_t*)user_buffer + i);
      struct sup_page_table_entry *spte = 
        sup_page_table_lookup(&thread_current()->spt, page_addr);
      if (spte != NULL) {
        void *kpage = pagedir_get_page(thread_current()->pagedir, page_addr);
        if (kpage != NULL)
          frame_unpin(kpage);
      }
    }
    #endif

    return length;
  }

  struct thread *cur = thread_current();

  struct file *file = cur->fd_table[fd];
  if (file == NULL) {
    return -1;
  }

  void *kernel_buffer = kernel_buffer_copy(user_buffer, length);
  if (kernel_buffer == NULL) {
    return -1;
  }

  lock_acquire(&filesys_lock);
  int bytes_written = file_write(file, kernel_buffer, length);
  lock_release(&filesys_lock);
  
  palloc_free_multiple(kernel_buffer, page_count);
  return (bytes_written >= 0) ? bytes_written : -1;
}

static int
close_handler (int fd)
{
  // closing stdin or stdout is invalid, as well as an already exited thread
  if (fd < 2 || fd >= FILE_TABLE_SIZE || thread_current()->fd_table[fd] == NULL) {
     // printf("Invalid file descriptor\n");
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
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
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
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
  }
  lock_acquire(&filesys_lock);
  bool status = filesys_create(file_name, initial_size);
  lock_release(&filesys_lock);
  return status;
}

static int
exec_handler(char *file_name){
  if (!validate_string(file_name)) {
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
  }
  tid_t tid = process_execute(file_name);
  if (tid == TID_ERROR) {
    return -1;
  }
  
  return tid;
}

static bool 
remove_handler(char *file_name){
  if (!validate_string(file_name)) {
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
  }

  lock_acquire(&filesys_lock);
  bool status = filesys_remove(file_name);
  lock_release(&filesys_lock);
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
read_handler(int fd, void *user_buffer, unsigned length) {
  
  // printf("read_handler called\n");

  if (length == 0) {
    return 0;
  }

  // printf("user_buffer: %p\n", user_buffer);

  // Basic address validity check before anything else
  if (user_buffer == NULL || !is_user_vaddr(user_buffer) || 
      !is_user_vaddr(user_buffer + length - 1)) {
    // printf("invalid buffer address range\n");
    exit_handler(-1);
    NOT_REACHED();
  }

  #ifdef VM
  // For read operations, ensure all buffer pages exist BEFORE validation
  // We need to calculate the precise page range needed for the buffer
  void *start_addr = pg_round_down(user_buffer);
  void *end_addr = pg_round_down((uint8_t*)user_buffer + length - 1);

  // Loop through all pages from start to end inclusive
  for (void *page_addr = start_addr; page_addr <= end_addr; page_addr += PGSIZE) {
    struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, page_addr);

    /* Read buffers don't need to be allocated a frame but they
       do need a SPT entry -- SPTEs should only be created at load. */
    if (spte == NULL) {
      exit_handler(-1);
      NOT_REACHED();
    }
    
    // Force load the page before validation
    lock_acquire(&spte->page_lock);
    if (spte->status != IN_MEMORY) {
      bool success = load_page(spte);
      if (!success) {
        lock_release(&spte->page_lock);
        printf("Failed to load page\n");
        exit_handler(-1);
        NOT_REACHED();
      }
    } else {
      lock_release(&spte->page_lock);
    }
  }
  #endif

  if (!validate_buffer(user_buffer, length)) {
    // printf("invalid buffer\n");
    exit_handler(-1);  // Terminate the process
    NOT_REACHED();
  }

  #ifdef VM
  // Pin the user buffer pages in memory to prevent eviction during copy
  for (unsigned i = 0; i < length; i += PGSIZE) {
    void *page_addr = pg_round_down((uint8_t*)user_buffer + i);
    struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, page_addr);
    if (spte != NULL) {
      void *kpage = pagedir_get_page(thread_current()->pagedir, page_addr);
      if (kpage != NULL)
        frame_pin(kpage);
    }
  }
  #endif

  size_t page_count = (length + PGSIZE - 1) / PGSIZE;

  struct thread *cur = thread_current();
  if (fd == 0) {
    // read from stdin
    void *kernel_buffer = palloc_get_multiple(0, page_count);
    if (kernel_buffer == NULL) {
      return -1;
    }
    unsigned i;
    for (i = 0; i < length; i++) {
      ((uint8_t*) kernel_buffer)[i] = input_getc();
    }

    for (unsigned j = 0; j < i; j++) {
      if (!put_user((uint8_t*) user_buffer + j, ((uint8_t*) kernel_buffer)[j])) {
        palloc_free_multiple(kernel_buffer, page_count);
        return -1;
      }
    }

    palloc_free_multiple(kernel_buffer, page_count);
    return i;
  }

  // trying to read from stout or invalid fd
  if (fd < 2 || fd >= FILE_TABLE_SIZE || cur->fd_table[fd] == NULL) {
    return -1;
  }
    
  struct file *file = cur->fd_table[fd];
  if (file == NULL) {
    return -1;
  }

  
  void *kernel_buffer = palloc_get_multiple(0, page_count);
  if (kernel_buffer == NULL) {
    return -1;
  }

  lock_acquire(&filesys_lock);  // Ensure thread safety
  int bytes_read = file_read(file, kernel_buffer, length);
  lock_release(&filesys_lock);

  if (bytes_read > 0) {
    for (int i = 0; i < bytes_read; i++) {
      if (!put_user((uint8_t*) user_buffer + i, ((uint8_t*) kernel_buffer)[i])) {
        palloc_free_multiple(kernel_buffer, page_count);
        return -1;
      }
    }
  }

  palloc_free_multiple(kernel_buffer, page_count);

  #ifdef VM
  // Unpin after copying is complete
  for (unsigned i = 0; i < length; i += PGSIZE) {
    void *page_addr = pg_round_down((uint8_t*)user_buffer + i);
    struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, page_addr);
    if (spte != NULL) {
      void *kpage = pagedir_get_page(thread_current()->pagedir, page_addr);
      if (kpage != NULL)
        frame_unpin(kpage);
    }
  }
  #endif
  
  return (bytes_read >= 0) ? bytes_read : -1; 
}

static void
seek_handler(int fd, unsigned position){
  struct thread *cur = thread_current();
  if (fd < 2 || fd >= FILE_TABLE_SIZE || cur->fd_table[fd] == NULL) {
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
  if (fd < 2 || fd >= FILE_TABLE_SIZE || cur->fd_table[fd] == NULL) {
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
    if (!is_user_vaddr(str)) {
      return false; // invalid memory access, segfault
    }
    int c = get_user((uint8_t*) str);
    if (c == -1) {
      return false; // invalid memory access, segfault
    }
    if (c == '\0') {
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

  if (get_user((uint8_t*) uptr) == -1) {
    exit_handler(-1); // invalid memory access, segfault
    NOT_REACHED();
  }
  
  void *kptr = pagedir_get_page(thread_current()->pagedir, uptr);
  if (kptr == NULL) {
    #ifdef VM
    // To confirm: these are user virtual addresses because we have a
    // SPT for each user process
    // what's the deal with the kernel?
    struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, pg_round_down(uptr));
      if (spte == NULL) {
        exit_handler(-1); // invalid memory access, segfault
        NOT_REACHED();
      }
      return uptr; // will cause page fault when accessed
    #else
    exit_handler(-1); // invalid memory access, segfault
    NOT_REACHED();
    #endif
  }

  // accesses the kernel virtual address
  // special bits only get set in the kernel page table

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
