#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

struct process_descriptor {
  tid_t tid;
  int exit_status;
  bool exited;
  struct semaphore wait_sema; // used for parent blocking upon wait
  bool waited_on; // used to enforce wait-once Unix semantics
  struct list_elem elem;
  struct list children; // list of child pds (access thread via offset)
  // elem -> process_descriptor -> thread
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
