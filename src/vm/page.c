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

static void spt_entry_free(struct hash_elem *e, void *aux UNUSED);

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

/* Load a page according to its type. Called after a page fault where
   the SPT entry exists and the access type is valid. */
bool
load_page(struct sup_page_table_entry *spte)
{
#ifdef USERPROG
  /* Allocate a frame for the page */
  void *kpage = frame_allocate(PAL_USER, spte);
  if (kpage == NULL) {
    printf("DEBUG: Failed to allocate frame for page %p\n", spte->vaddr);
    return false;
  }

  bool success = false;
  
  switch (spte->status)
  {
    case IN_MEMORY:
      /* Already in memory, nothing to do */
      success = true;
      break;
      
    case IN_SWAP: // TOOD: fully implement swap.c
      /* Load from swap */
      swap_in(spte->swap_index, kpage);
      spte->status = IN_MEMORY;
      /* Do NOT modify spte->source */
      success = true;
      break;
      
    case IN_FILESYS:
      /* Read/write bytes already validated in lazy loading */

      // printf("DEBUG: Loading page from file %p, offset %d, read_bytes %d\n",
      //        spte->vaddr, spte->file_offset, spte->read_bytes);

      /* Handling demand paging from file */
      if (spte->read_bytes == 0) {
        /* All bytes to zero */
        memset(kpage, 0, PGSIZE);
        success = true;
      } else {
        /* Some to read, potentially some to zero */

        /* Releases frame table lock during I/O for parallelism */
        frame_pin(kpage);  /* Prevent eviction during I/O */

        /* File I/O occupied, does not need to hold locks */
        file_seek(spte->file, spte->file_offset);
        if (file_read(spte->file, kpage, spte->read_bytes) != (int) spte->read_bytes) {
          frame_free(kpage);
          return false;
        }

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
  }
  
  /* If successful, add the page to the process's page table */
  if (success && 
      pagedir_get_page(thread_current()->pagedir, spte->vaddr) == NULL &&
      pagedir_set_page(thread_current()->pagedir, spte->vaddr, kpage, spte->writable)) {
    // printf("DEBUG: Page %p loaded successfully\n", spte->vaddr);
    spte->status = IN_MEMORY;
    frame_unpin(kpage);  /* Unpin the frame now that it's set up */
    return true;
  }
  
  // printf("DEBUG: Failed to load page %p\n", spte->vaddr);
  
  /* If we get here, loading failed */
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
static void
spt_entry_free(struct hash_elem *e, void *aux UNUSED)
{
  struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, hash_elem);
  
#ifdef USERPROG
  /* Close any open files */
  if (spte->status == IN_FILESYS && spte->file != NULL)
    file_close(spte->file);
    
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