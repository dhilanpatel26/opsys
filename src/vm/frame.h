#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/stdbool.h"
#include <list.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "vm/page.h"  // For supplemental page table entry
#include "threads/palloc.h"

/* Frame table entry */
struct frame_entry {
  void *kpage;                /* Kernel virtual address of the frame */
#ifdef USERPROG
  struct thread *owner;       /* Thread that owns the frame */
#endif
  struct sup_page_table_entry *spte; /* Pointer to supplemental page table entry */
  bool pinned;                /* Whether this frame can be evicted */
  struct list_elem elem;      /* List element for frame table */
};

void frame_table_init(void);
void *frame_allocate(enum palloc_flags, struct sup_page_table_entry *);
void frame_free(void *kpage);
void frame_pin(void *kpage);
void frame_unpin(void *kpage);
void *frame_lookup(void *vaddr);
void *frame_register(void *kpage, struct sup_page_table_entry *spte);

#endif