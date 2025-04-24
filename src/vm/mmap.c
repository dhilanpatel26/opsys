#include "vm/mmap.h"

#include <list.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/file.h"
#include <stdio.h>

#ifdef VM
/* Add this line */
extern struct lock filesys_lock;

// initialize mmap list for a thread
void
mmap_init(struct thread *t)
{
  list_init(&t->mmap_list);
  t->next_mapid = 1;  // start with mapid 1 (0 is invalid)
}

// add a new memory mapping
mapid_t
mmap_add(struct file *file, void *addr, size_t page_count)
{
  struct thread *t = thread_current();
  
  // allocate and initialize mmap_entry
  struct mmap_entry *me = malloc(sizeof(struct mmap_entry));
  if (me == NULL)
    return -1;
    
  me->mapid = t->next_mapid++;
  me->file = file;
  me->addr = addr;
  me->page_count = page_count;
  
// add to thread's mmap list
  list_push_back(&t->mmap_list, &me->elem);
  
  return me->mapid;
}

// look up a memory mapping by mapid
struct mmap_entry *
mmap_lookup(mapid_t mapid)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  
  for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list);
       e = list_next(e))
  {
    struct mmap_entry *me = list_entry(e, struct mmap_entry, elem);
    if (me->mapid == mapid)
      return me;
  }
  
  return NULL;
}

// remove a memory mapping
bool
mmap_remove(mapid_t mapid)
{
  struct mmap_entry *me = mmap_lookup(mapid);
  if (me == NULL) return false;
    
  // remove from list
  list_remove(&me->elem);
  
  // write back dirty pages and unmap
  void *addr = me->addr;
  struct thread *t = thread_current();
  
  // check if page dir exists first
  if (t->pagedir != NULL) {
    for (size_t i = 0; i < me->page_count; i++, addr += PGSIZE)
    {
        struct sup_page_table_entry *spte = sup_page_table_lookup(&t->spt, addr);
        if (spte != NULL && spte->status == IN_MEMORY)
        {
        void *kpage = pagedir_get_page(t->pagedir, addr);
        if (kpage != NULL)
        {
            // if page is dirty, write back to file
            if (pagedir_is_dirty(t->pagedir, addr))
            {
            lock_acquire(&filesys_lock);
            file_seek(me->file, i * PGSIZE);
            // only write up to file size for the last page
            off_t bytes_to_write = PGSIZE;
            if (i == me->page_count - 1)
            {
                off_t file_size = file_length(me->file);
                off_t bytes_in_last_page = file_size % PGSIZE;
                if (bytes_in_last_page > 0)
                bytes_to_write = bytes_in_last_page;
            }
            file_write(me->file, kpage, bytes_to_write);
            lock_release(&filesys_lock);
            }

            // remove page from page table
            pagedir_clear_page(t->pagedir, addr);
            frame_free(kpage);
        }
        }
        
        // remove from supplemental page table
        if (spte != NULL) {
        hash_delete(&t->spt, &spte->hash_elem);
        spt_entry_free(&spte->hash_elem, NULL);
        }
    }
  }
  
  // close file and free mmap entry
  lock_acquire(&filesys_lock);
  file_close(me->file);
  lock_release(&filesys_lock);
  free(me);
  
  return true;
}

// remove all memory mappings for the current thread
void
mmap_remove_all(void)
{
  struct thread *t = thread_current();
  struct list_elem *e, *next;
  
  for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = next)
  {
    next = list_next(e);
    struct mmap_entry *me = list_entry(e, struct mmap_entry, elem);
    mmap_remove(me->mapid);
  }
}
#endif // VM