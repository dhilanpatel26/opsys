#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <bitmap.h>

// incomplete
// TODO: finish

/* The swap device */
static struct block *swap_device;

/* Bitmap for tracking free swap slots */
static struct bitmap *swap_bitmap;

/* Sector size and how many sectors per page */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Lock for swap operations */
static struct lock swap_lock;

/* Initialize the swap table */
void
swap_init(void)
{
  swap_device = block_get_role(BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC("No swap device found, VM system cannot work");
    
  swap_bitmap = bitmap_create(block_size(swap_device) / SECTORS_PER_PAGE);
  if (swap_bitmap == NULL)
    PANIC("Failed to create swap bitmap");
    
  bitmap_set_all(swap_bitmap, false);  /* All slots start free */
  lock_init(&swap_lock);
}

/* Write a page to swap space and return the swap index */
swap_index_t
swap_out(void *page)
{
  lock_acquire(&swap_lock);
  
  /* Find a free swap slot */
  size_t swap_index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
  if (swap_index == BITMAP_ERROR)
    PANIC("Swap space is full");
    
  /* Write the page to the swap slot, sector by sector */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    block_write(swap_device, 
                swap_index * SECTORS_PER_PAGE + i,
                page + i * BLOCK_SECTOR_SIZE);
  }
  
  lock_release(&swap_lock);
  return swap_index;
}

/* Read a page from swap space */
void
swap_in(swap_index_t swap_index, void *page)
{
  ASSERT(bitmap_test(swap_bitmap, swap_index));
  
  lock_acquire(&swap_lock);
  
  /* Read the page from the swap slot, sector by sector */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++) {
    block_read(swap_device, 
               swap_index * SECTORS_PER_PAGE + i,
               page + i * BLOCK_SECTOR_SIZE);
  }
  
  /* Mark the swap slot as free */
  bitmap_set(swap_bitmap, swap_index, false);
  
  lock_release(&swap_lock);
}

/* Free a swap slot without reading it */
void
swap_free(swap_index_t swap_index)
{
  lock_acquire(&swap_lock);
  bitmap_set(swap_bitmap, swap_index, false);
  lock_release(&swap_lock);
}