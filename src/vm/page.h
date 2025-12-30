#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

/* Types of pages in the supplemental page table */
enum page_status {
  IN_MEMORY,    /* Page is currently in memory */
  IN_SWAP,      /* Page is in the swap space */
  IN_FILESYS,   /* Page is in the file system */
  NOT_LOADED    /* Page has not been loaded yet */
};

enum page_source {
  SOURCE_FILE,
  SOURCE_MMAP,
  SOURCE_SWAP,
  SOURCE_ZERO
};

/* Supplemental page table entry */
struct sup_page_table_entry {
  void *vaddr;              /* User virtual address */
  bool writable;            /* True if writable */
  enum page_status status;  /* Current status of the page */
  enum page_source source;  /* Original source of the page, never changes after initialization */
  struct lock page_lock;    /* Lock for synchronizing access to the page across multiple processes */

#ifdef USERPROG
  /* For pages in the swap */
  swap_index_t swap_index;  /* Index in the swap table if swapped */
  
  /* For file-backed pages */
  struct file *file;        /* File that backs this page */
  off_t file_offset;        /* Offset in the file */
  uint32_t read_bytes;      /* Number of bytes to read */
  uint32_t zero_bytes;      /* Number of bytes to zero */
#endif

  struct hash_elem hash_elem; /* Hash table element */
};

void sup_page_table_init(struct hash *spt);
bool sup_page_table_insert(struct hash *spt, struct sup_page_table_entry *spte);
struct sup_page_table_entry *sup_page_table_lookup(struct hash *spt, void *vaddr);
bool load_page(struct sup_page_table_entry *spte);
void sup_page_table_destroy(struct hash *spt);

/* Hash functions for the supplemental page table */
unsigned sup_page_table_hash(const struct hash_elem *e, void *aux);
bool sup_page_table_less(const struct hash_elem *a, 
                         const struct hash_elem *b, void *aux);
bool valid_stack_access(void *fault_addr, void *esp);
void spt_entry_free(struct hash_elem *e, void *aux);
#endif