#include "vm/page.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include <string.h>
#include <stdio.h>

#ifdef USERPROG
#include "userprog/pagedir.h"
#endif
extern struct lock filesys_lock;

/* Initialize a supplemental page table */
void
sup_page_table_init(struct hash *spt)
{
  hash_init(spt, sup_page_table_hash, sup_page_table_less, NULL);
}

/* Insert a page into the supplemental page table.
   Returns true on insert, false on no change (already in table) */
bool
sup_page_table_insert(struct hash *spt, struct sup_page_table_entry *spte)
{
  return hash_insert(spt, &spte->hash_elem) == NULL;
}

/* Look up a page in the supplemental page table */
struct sup_page_table_entry *
sup_page_table_lookup(struct hash *spt, void *vaddr)
{
  if (spt == NULL || vaddr == NULL)
    return NULL;

  struct sup_page_table_entry spte;
  struct hash_elem *e;
  
  /* We need to clear the offset bits to get the page address */
  spte.vaddr = pg_round_down(vaddr);
  e = hash_find(spt, &spte.hash_elem);
  
  return e != NULL ? hash_entry(e, struct sup_page_table_entry, hash_elem) : NULL;
}

/* Called with spte locked by caller page_fault. Return with spte lock held. */
bool
load_page(struct sup_page_table_entry *spte)
{
#ifdef USERPROG
  /* Page is already in memory, nothing to do */
  if (spte->status == IN_MEMORY)
    return true;

  /* Release-and-recheck pattern with two-phase locking */
  /* Temporarily release lock before allocating frame to avoid deadlocks */
  lock_release(&spte->page_lock);

  /* Allocate a frame for the page */
  void *kpage = frame_allocate(PAL_USER, spte);
  if (kpage == NULL) {
    lock_acquire(&spte->page_lock);
    return false;
  }

  /* Re-acquire the lock after frame allocation */
  lock_acquire(&spte->page_lock);

  /* Check again if another thread loaded it while we were allocating */
  if (spte->status == IN_MEMORY) {
    /* Page was loaded by another thread, free our frame */
    frame_free(kpage);
    return true;
  }

  bool success = false;
  
  switch (spte->status)
  {   
    case IN_SWAP:
      /* Load from swap */
      swap_in(spte->swap_index, kpage);
      spte->status = IN_MEMORY;
      /* Do NOT modify spte->source */
      success = true;
      break;
      
    case IN_FILESYS:
      /* Handling demand paging from file */
      if (spte->read_bytes == 0) {
        /* All bytes to zero */
        memset(kpage, 0, PGSIZE);
        success = true;
      } else {
        /* Some to read, potentially some to zero */

        // already pinned
        // frame_pin(kpage);  /* Prevent eviction during I/O */

        /* File I/O doesn't need frame table lock */
        lock_acquire(&filesys_lock);
        file_seek(spte->file, spte->file_offset);
        if (file_read(spte->file, kpage, spte->read_bytes) != (int) spte->read_bytes) {
          frame_free(kpage);
          lock_release(&filesys_lock);
          return false;
        }
        lock_release(&filesys_lock);

        /* Zero rest of page */
        if (spte->zero_bytes > 0)
          memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
        
        success = true;
      }
      break;
      
    case NOT_LOADED:
      /* Zero the page */
      memset(kpage, 0, PGSIZE);
      success = true;
      break;
    default:
      PANIC("Invalid page status");
  }
  
  /* If successful, add the page to the process's page table */
  if (success && 
      pagedir_get_page(thread_current()->pagedir, spte->vaddr) == NULL &&
      pagedir_set_page(thread_current()->pagedir, spte->vaddr, kpage, spte->writable)) {
    spte->status = IN_MEMORY;
    frame_unpin(kpage);  /* Unpin the frame now that it's set up */
    return true;
  }
  
  /* If we get here, loading failed */
  frame_unpin(kpage);
  frame_free(kpage);
#endif
  return false;
}

/* Hash function for supplemental page table */
unsigned 
sup_page_table_hash(const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_table_entry *spte = 
    hash_entry(e, struct sup_page_table_entry, hash_elem);
  return hash_bytes(&spte->vaddr, sizeof spte->vaddr);
}

/* Comparison function for supplemental page table */
bool
sup_page_table_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct sup_page_table_entry *sa = 
    hash_entry(a, struct sup_page_table_entry, hash_elem);
  const struct sup_page_table_entry *sb = 
    hash_entry(b, struct sup_page_table_entry, hash_elem);
    
  return sa->vaddr < sb->vaddr;
}

/* Frees an SPT entry */
void
spt_entry_free(struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, hash_elem);
  
#ifdef USERPROG
  /* Close any open files */
  if (spte->status == IN_FILESYS && spte->file != NULL) {
    lock_acquire(&filesys_lock);
    file_close(spte->file);
    lock_release(&filesys_lock);
  }
    
  /* Free any swap slots */
  if (spte->status == IN_SWAP)
    swap_free(spte->swap_index);
#endif

  /* Free the entry itself */
  free(spte);
}

void
sup_page_table_destroy(struct hash *spt)
{
  hash_destroy(spt, spt_entry_free);
}

// do we need a spt_entry_free?