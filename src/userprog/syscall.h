#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

#ifdef VM
#define MAX_STACK_SIZE ((void*) (8 * 1024 * 1024))  /* 8 MB stack */
#endif

#endif /* userprog/syscall.h */
