#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <list.h>
#include <stdio.h>
#include "threads/interrupt.h"

#ifdef USERPROG
#include "userprog/pagedir.h"
#endif

#ifdef VM
#include "vm/swap.h"
#endif

static int total_frames = 0;
static int pinned_frames = 0;

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

/* Finds the frame that contains the given kernel page */
void *frame_lookup(void *vaddr) 
{
  // printf("Frame lookup for vaddr: %p\n", vaddr);
  struct list_elem *e;
  struct frame_entry *f;
  void *kpage = NULL;
  
  lock_acquire(&frame_table_lock);
  
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    f = list_entry(e, struct frame_entry, elem);
    // Debug print to see what's in the frame table
    // printf("Frame entry: kpage=%p, spte->vaddr=%p, looking for %p\n", 
    //        f->kpage, f->spte->vaddr, vaddr);
    if (f->spte != NULL && f->spte->vaddr == vaddr) {
      kpage = f->kpage;
      break;
    }
  }
  
  lock_release(&frame_table_lock);
  return kpage;
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
  pinned_frames++;
  f->pinned = true;  // Pin it initially while we set it up
  
  list_push_back(&frame_list, &f->elem);

  total_frames++;
  // printf("DEBUG: Frame allocated: %p (total: %d, pinned:, %d)\n", 
  //   kpage, total_frames, pinned_frames);

  lock_release(&frame_table_lock);
  
  return kpage;
}

// static void *
// frame_evict(void) 
// {
// #ifdef USERPROG
//   static struct list_elem *clock_ptr = NULL;
//   struct frame_entry *f;
  
//   /* Scan for a frame to evict */
//   size_t iterations = 0;
//   size_t num_frames = list_size(&frame_list);
  
//   while (iterations < 2 * num_frames) {
//     /* Precondition: Must hold frame_table_lock while scanning (frame_allocate acquired) */
//     if (clock_ptr == NULL || clock_ptr == list_end(&frame_list))
//       clock_ptr = list_begin(&frame_list);
      
//     f = list_entry(clock_ptr, struct frame_entry, elem);
//     clock_ptr = list_next(clock_ptr);
    
//     /* Skip pinned frames */
//     if (f->pinned)
//       continue;
      
//     /* Try to acquire the page lock for this frame's SPT entry */
//     if (!lock_try_acquire(&f->spte->page_lock)) {
//       /* Skip if we can't get the lock without blocking */
//       iterations++;
//       continue;
//     }
      
//     /* Check accessed bit */
//     if (pagedir_is_accessed(f->owner->pagedir, f->spte->vaddr)) {
//       /* Give a second chance */
//       pagedir_set_accessed(f->owner->pagedir, f->spte->vaddr, false);
//       lock_release(&f->spte->page_lock);
//     } else {
//       /* Evict this frame - we hold both the frame table lock and the page lock */
//       void *kpage = f->kpage;
      
//       /* If dirty, write to swap */

//       // this is through the user page directory, so we need to make sure 
//       // that the kernel always uses the user page directory to access the page.
//       // need to modify syscall accordingly (some translations right, some wrong).
//       if (pagedir_is_dirty(f->owner->pagedir, f->spte->vaddr) ||
//           pagedir_is_dirty(thread_current()->pagedir, kpage)) {
//         extern bool swap_available;
//         if (!swap_available) {
//           lock_release(&f->spte->page_lock);
//           continue; // Skip this frame, can't swap
//         }

//         /* Temporarily release frame table lock during I/O for parallelism */
//         lock_release(&frame_table_lock);
        
//         /* Do swap I/O while holding only the page lock */
//         f->spte->swap_index = swap_out(kpage);
//         f->spte->status = IN_SWAP;
        
//         /* Reacquire frame table lock */
//         lock_acquire(&frame_table_lock);
//       } else if (f->spte->source == SOURCE_FILE) {
//         /* Clean page from file, just update status */
//         /* No need to write back to file */
//         f->spte->status = IN_FILESYS;
//       }
      
//       /* Remove mapping from page table */
//       pagedir_clear_page(f->owner->pagedir, f->spte->vaddr);
      
//       /* Update SPT and release page lock */
//       lock_release(&f->spte->page_lock);
      
//       /* Remove from frame table */
//       list_remove(&f->elem);
//       free(f);
      
//       return kpage;
//     }
    
//     iterations++;
//   }
// #endif
//   return NULL; /* Could not evict any frame */
// }

/* Called with frame_table_lock held. Do not release. */
static void *
frame_evict(void) 
{
  // enum intr_level old_level = intr_disable();
#ifdef USERPROG
  // each thread has its own clock pointer to prevent race conditions
  struct list_elem *clock_ptr = NULL;
  struct frame_entry *f;
  
  /* Scan for a frame to evict */
  size_t iterations = 0;
  size_t num_frames = list_size(&frame_list);
  
  // printf("DEBUG: Starting eviction (frames: %zu, pinned: %d)\n", 
  //        num_frames, pinned_frames);
         
  while (iterations < 2 * num_frames) {
    if (clock_ptr == NULL || clock_ptr == list_end(&frame_list))
      clock_ptr = list_begin(&frame_list);

    // Validate that the frame is still in the list before dereferencing
    bool valid_frame = false;
    struct list_elem *validate_e;
    
    for (validate_e = list_begin(&frame_list); validate_e != list_end(&frame_list); 
        validate_e = list_next(validate_e)) {
      if (validate_e == clock_ptr) {
        valid_frame = true;
        break;
      }
    }
    
    // Skip if this frame element no longer exists in the list
    if (!valid_frame) {
      clock_ptr = list_begin(&frame_list);
      iterations++;
      continue;
    }
      
    f = list_entry(clock_ptr, struct frame_entry, elem);
    
    // printf("DEBUG: Checking frame %p (pinned: %d)\n", f->kpage, f->pinned);
    /* Skip pinned frames */
    if (f->pinned) {
      // printf("DEBUG: Skipping pinned frame %p\n", f->kpage);
      /* Still count this as an iteration */
      iterations++;
      clock_ptr = list_next(clock_ptr);
      continue;
    }
      
    /* Try to acquire the page lock for this frame's SPT entry */
    // Note: the page we are trying to acquire should not have a frame in the list
    if (!lock_try_acquire(&f->spte->page_lock)) {
      /* Skip if we can't get the lock without blocking */
      // printf("DEBUG: Skipping frame %p - could not acquire lock\n", f->kpage);
      /* Don't count as an iteration? */
      clock_ptr = list_next(clock_ptr);
      continue;
    }
      
    /* Check accessed bit */
    if (pagedir_is_accessed(f->owner->pagedir, f->spte->vaddr)) {
      /* Give a second chance */
      // printf("DEBUG: Frame %p accessed, giving second chance\n", f->kpage);
      pagedir_set_accessed(f->owner->pagedir, f->spte->vaddr, false);
      lock_release(&f->spte->page_lock);
    } else {
      // printf("DEBUG: Evicting frame %p (not recently accessed)\n", f->kpage);
      /* We found a frame to evict - it hasn't been accessed */
      void *kpage = f->kpage;
      
      // printf("DEBUG: Evicting frame %p (not recently accessed)\n", kpage);
      
      /* If dirty, write to swap */
      if (pagedir_is_dirty(f->owner->pagedir, f->spte->vaddr) ||
          pagedir_is_dirty(thread_current()->pagedir, kpage)) {
        extern bool swap_available;
        if (!swap_available && 0) {
          // printf("DEBUG: Cannot evict frame %p - swap not available\n", kpage);
          lock_release(&f->spte->page_lock);
          iterations++;
          clock_ptr = list_next(clock_ptr);
          continue; // Skip this frame, can't swap
        }

        // printf("DEBUG: Writing dirty frame %p to swap\n", kpage);
        
        /* Pin frame to prevent race conditions when we release the frame table lock */
        f->pinned = true;
        pinned_frames++;

        /* Save critical values locally before releasing lock */
        void *local_kpage = kpage;
        struct sup_page_table_entry *local_spte = f->spte;

        /* Temporarily release frame table lock during I/O for parallelism */
        // intr_enable();
        lock_release(&frame_table_lock);
        
        /* Still holding page_lock */
        swap_index_t swap_idx = swap_out(local_kpage);
              
        /* Reacquire frame table lock */
        lock_acquire(&frame_table_lock);
        // intr_disable();

        /* Verify frame still exists and is valid */
        bool frame_still_valid = false;
        struct list_elem *e;
        for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
          struct frame_entry *check_f = list_entry(e, struct frame_entry, elem);
          if (check_f == f && check_f->kpage == local_kpage) {
            frame_still_valid = true;
            break;
          }
        }
        
        if (!frame_still_valid) {
          /* Frame was removed while we were swapping - abort this eviction */
          // printf("DEBUG: Frame was invalidated during swap I/O\n");
          lock_release(&local_spte->page_lock);
          clock_ptr = list_next(clock_ptr);
          iterations++;
          continue;
        }
        
        /* Safe to update frame now */
        f->spte->swap_index = swap_idx;
        f->spte->status = IN_SWAP;

      } else if (f->spte->source == SOURCE_FILE) {
        /* Clean page from file, just update status */
        // printf("DEBUG: Clean frame %p from file, no need to write back\n", kpage);
        f->spte->status = IN_FILESYS;
      }
      
      /* Remove mapping from page table for the evicted frame */
      pagedir_clear_page(f->owner->pagedir, f->spte->vaddr);
      
      lock_release(&f->spte->page_lock);
      
      /* Remove evicted from frame table */
      list_remove(&f->elem);
      
      /* If frame was pinned, decrement pinned count */
      if (f->pinned) {
        pinned_frames--;
      }
      
      /* Update accounting */
      total_frames--;
      
      /* Free the frame entry but not the physical page, will reuse! */
      free(f);

      // intr_set_level(old_level);
      
      // printf("DEBUG: Successfully evicted frame %p\n", kpage);
      return kpage; /* Reusing physical address of frame (translated to kernel vaddr here) */
    }
    clock_ptr = list_next(clock_ptr);
    
    iterations++;
  }
  
  printf("DEBUG: Eviction failed! Too many pinned frames (%d/%zu) or locks unavailable\n", 
         pinned_frames, num_frames);
#endif

  // intr_set_level(old_level);
  
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

  pinned_frames++;
  // printf("DEBUG: Frame pinned: %p (total: %d, pinned: %d)\n", 
  //       kpage, total_frames, pinned_frames);
  
  lock_release(&frame_table_lock);
}

/* Mark a frame as unpinned (eligible for eviction) */
void
frame_unpin(void *kpage) 
{
  struct list_elem *e;
  struct frame_entry *f;
  if (kpage == NULL){
    printf("DEBUG: Attempted to unpin NULL frame\n");
    return;
  }
  
  lock_acquire(&frame_table_lock);
  bool found = false; 
  for (e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)) {
    f = list_entry(e, struct frame_entry, elem);
    if (f->kpage == kpage) {
      f->pinned = false;
      found = true;
      break;
    }
  }
  if (found) pinned_frames--;
  // printf("DEBUG: Frame unpinned: %p (total: %d, pinned: %d)\n", 
  //       kpage, total_frames, pinned_frames);
  
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
      if (f->pinned) {
        pinned_frames--;
      }
      list_remove(&f->elem);
      palloc_free_page(kpage);
      free(f);
      break;
    }
  }

  total_frames--;
  // printf("DEBUG: Frame freed: %p (total: %d, pinned: %d)\n", 
  //        kpage, total_frames, pinned_frames);
  
  lock_release(&frame_table_lock);
}

/* Register an existing page with the frame system */
void* 
frame_register(void *kpage, struct sup_page_table_entry *spte)
{
  struct frame_entry *f;
  
  lock_acquire(&frame_table_lock);
  
  /* Create frame entry */
  f = malloc(sizeof(struct frame_entry));
  if (f == NULL) {
    lock_release(&frame_table_lock);
    return NULL;
  }
  
  /* Set up frame entry */
  f->kpage = kpage;
  #ifdef USERPROG
  f->owner = thread_current();
  #endif
  f->spte = spte;

  pinned_frames++;
  f->pinned = true;  // Pinned by default during setup
  
  /* Add to frame table */
  list_push_back(&frame_list, &f->elem);
  
  lock_release(&frame_table_lock);
  return kpage;
}

/* prevents access of stale page frames from exited processes */
#ifdef USERPROG
void frame_free_thread_frames(struct thread *t) {
  lock_acquire(&frame_table_lock);
  
  struct list_elem *e = list_begin(&frame_list);
  while (e != list_end(&frame_list)) {
      struct frame_entry *f = list_entry(e, struct frame_entry, elem);
      struct list_elem *next = list_next(e);
      
      if (f->owner == t) {
        if (f->pinned) {
          f->pinned = false;
          pinned_frames--;
        }
          // don't free the virtual page, that gets handled in process_exit
          list_remove(&f->elem);
          free(f);
          total_frames--;
          
      }

      e = next;
  }
  
  lock_release(&frame_table_lock);
}
#endif