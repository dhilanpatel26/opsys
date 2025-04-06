// In exception.c or a similar file that handles exceptions

#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Page fault handler */
static void
page_fault(struct intr_frame *f)
{
  /* Get the virtual address that caused the fault */
  void *fault_addr = (void *) f->cr2;
  
  /* Check if the address is valid */
  if (fault_addr == NULL || !is_user_vaddr(fault_addr)) {
    kill_process();
    return;
  }
  
  /* Get the supplemental page table entry */
  struct thread *t = thread_current();
  struct sup_page_table_entry *spte = 
    sup_page_table_lookup(&t->spt, pg_round_down(fault_addr));
    
  /* If there's no entry or it's not writable but the access is a write,
     the fault is invalid */
  if (spte == NULL || (!spte->writable && (f->error_code & PF_W))) {
    kill_process();
    return;
  }
  
  /* Try to load the page */
  bool success = load_page(spte);
  if (!success) {
    kill_process();
    return;
  }
}