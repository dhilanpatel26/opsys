#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <list.h>

#ifdef USERPROG
#include "userprog/pagedir.h"
#include "vm/swap.h"
#endif

static struct list frame_list;       /* List of all frames */
static struct lock frame_table_lock; /* Lock for frame table operations */

static void *frame_evict(void);      /* Evict a frame using clock algorithm */

/* Initializes the frame table */
void
frame_table_init(void) 
{
  list_init(&frame_list);
  lock_init(&frame_table_lock);
}

/* Allocates a frame for the given page and returns the kernel virtual address.
   If no frames are available, evicts a frame according to the clock algorithm. */
void *
frame_allocate(enum palloc_flags flags, struct sup_page_table_entry *spte) 
{
  void *kpage;
  
  lock_acquire(&frame_table_lock);
  
  /* Try to get a free frame */
  kpage = palloc_get_page(flags);
  
  /* If no free frame, evict one */
  if (kpage == NULL) {
#ifdef USERPROG
    kpage = frame_evict();
    if (kpage == NULL) {
      lock_release(&frame_table_lock);
      return NULL;
    }
#else
    lock_release(&frame_table_lock);
    return NULL;
#endif
  }
  
  /* Create frame entry and add to table */
  struct frame_entry *f = malloc(sizeof(struct frame_entry));
  if (f == NULL) {
    palloc_free_page(kpage);
    lock_release(&frame_table_lock);
    return NULL;
  }
  
  f->kpage = kpage;
#ifdef USERPROG
  f->owner = thread_current();
  f->spte = spte;
#endif
  f->pinned = true;  // Pin it initially while we set it up
  
  list_push_back(&frame_list, &f->elem);
  lock_release(&frame_table_lock);
  
  return kpage;
}

/* Eviction using clock algorithm (simplified) */
static void *
frame_evict(void) 
{
#ifdef USERPROG
  static struct list_elem *clock_ptr = NULL;
  struct frame_entry *f;
  
  /* Start from the current position or beginning of list */
  if (clock_ptr == NULL || clock_ptr == list_end(&frame_list))
    clock_ptr = list_begin(&frame_list);
  
  /* Loop through frames looking for one to evict */
  size_t iterations = 0;
  size_t num_frames = list_size(&frame_list);
  
  while (iterations < 2 * num_frames) {
    f = list_entry(clock_ptr, struct frame_entry, elem);
    clock_ptr = list_next(clock_ptr);
    
    if (clock_ptr == list_end(&frame_list))
      clock_ptr = list_begin(&frame_list);
    
    /* Skip pinned frames */
    if (f->pinned)
      continue;
      
    /* Check accessed bit */
    if (pagedir_is_accessed(f->owner->pagedir, f->spte->vaddr)) {
      /* Give a second chance */
      pagedir_set_accessed(f->owner->pagedir, f->spte->vaddr, false);
    } else {
      /* Evict this frame */
      void *kpage = f->kpage;
      
      /* If dirty, write to swap */
      if (pagedir_is_dirty(f->owner->pagedir, f->spte->vaddr)) {
        f->spte->swap_index = swap_out(kpage);
        f->spte->status = IN_SWAP;
      }
      
      /* Remove mapping from page table */
      pagedir_clear_page(f->owner->pagedir, f->spte->vaddr);
      
      /* Remove from frame table */
      list_remove(&f->elem);
      free(f);
      
      return kpage;
    }
    
    iterations++;
  }
#endif
  return NULL; /* Could not evict any frame */
}

/* Mark a frame as pinned (not eligible for eviction) */
void
frame_pin(void *kpage) 
{
  struct list_elem *e;
  struct frame_entry *f;
  
  lock_acquire(&frame_table_lock);
  
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    f = list_entry(e, struct frame_entry, elem);
    if (f->kpage == kpage) {
      f->pinned = true;
      break;
    }
  }
  
  lock_release(&frame_table_lock);
}

/* Mark a frame as unpinned (eligible for eviction) */
void
frame_unpin(void *kpage) 
{
  struct list_elem *e;
  struct frame_entry *f;
  
  lock_acquire(&frame_table_lock);
  
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    f = list_entry(e, struct frame_entry, elem);
    if (f->kpage == kpage) {
      f->pinned = false;
      break;
    }
  }
  
  lock_release(&frame_table_lock);
}

/* Frees a frame */
void
frame_free(void *kpage) 
{
  struct list_elem *e;
  struct frame_entry *f;
  
  lock_acquire(&frame_table_lock);
  
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    f = list_entry(e, struct frame_entry, elem);
    if (f->kpage == kpage) {
      list_remove(&f->elem);
      palloc_free_page(kpage);
      free(f);
      break;
    }
  }
  
  lock_release(&frame_table_lock);
}