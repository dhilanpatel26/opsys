#ifndef VM_MMAP_H
#define VM_MMAP_H

typedef int mapid_t;

#include <list.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "vm/page.h"


// a struct representing a memory mapped file
struct mmap_entry {
  mapid_t mapid;                // Mapping ID
  struct file *file;            // File being mapped
  void *addr;                   // Starting virtual address
  size_t page_count;            // Number of pages in mapping
  struct list_elem elem;        // List element for thread's mmap_list
};

// initialize mmap list for a thread
void mmap_init(struct thread *t);

// add a new memory mapping
mapid_t mmap_add(struct file *file, void *addr, size_t page_count);

// remove a memory mapping
bool mmap_remove(mapid_t mapid);

// lookup a memory mapping
struct mmap_entry *mmap_lookup(mapid_t mapid);

// remove all memory mappings
void mmap_remove_all(void);

#endif /* vm/mmap.h */