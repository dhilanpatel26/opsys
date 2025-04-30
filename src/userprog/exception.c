#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"

#ifdef USERPROG
#include "userprog/pagedir.h"
#endif

#ifdef VM
#include "vm/page.h"
#endif

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
bool valid_stack_access(void *fault_addr, void *esp);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));
  #ifdef VM
  thread_current()->esp = f->esp;
  #endif

//   #ifdef VM
//    printf("Registers: eax=%08x, ebx=%08x, ecx=%08x, edx=%08x, eip=%08x, ebp=%08x, esp=%08x\n", 
//           f->eax, f->ebx, f->ecx, f->edx, f->eip, f->ebp, f->esp);
//   #endif

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  // case one: handle kernel access to user memory (e.g. during syscall)
  if (!user && fault_addr < PHYS_BASE && is_user_vaddr(fault_addr)) {

  // if this is a write to a read-only page...terminate process
  if (!not_present && write) {
    thread_exit();
    NOT_REACHED();
  }

   f->eip = (void*) f->eax;
   f->eax = 0xffffffff;
   return;
  }

//   #ifdef VM
//   printf("DEBUG: Page fault at %p (rounded: %p)\n", fault_addr, pg_round_down(fault_addr));
//   printf("DEBUG: not_present=%d write=%d user=%d\n", not_present, write, user);
//   #endif

#ifdef VM
/* Is this a valid page fault that can be handled by VM system? */
  if (not_present) {
   /* Round down to get page address */
   void *page_addr = pg_round_down(fault_addr);
   
   // printf("DEBUG: Page fault at %p (rounded: %p)\n", fault_addr, page_addr);

   /* Check if the page is in the supplemental page table */
   struct sup_page_table_entry *spte = 
      sup_page_table_lookup(&thread_current()->spt, page_addr);

   // printf("DEBUG: SPT entry found: %p\n", spte);

   /* If SPTE exists (it really should) */
   if (spte != NULL) {
      /* Lock SPTE while checking shared fields */
      lock_acquire(&spte->page_lock);

      /* If valid access */
      if (!(write && !spte->writable)) {

         bool success = load_page(spte);

         lock_release(&spte->page_lock);

         /* Don't need lock, using value that won't change */
         if (success)
            return;
         
      } else {
         
         lock_release(&spte->page_lock);
      }
   }
   else if (user && valid_stack_access(fault_addr, f->esp)) {
    struct sup_page_table_entry *new_spte =
        malloc(sizeof *new_spte);
      if (new_spte == NULL) {
         thread_exit(); //out of memory 
         NOT_REACHED();
      }
      new_spte->vaddr = page_addr;
      new_spte->writable = true;
      new_spte->status = NOT_LOADED;
      new_spte->source = SOURCE_ZERO;
      lock_init(&new_spte->page_lock);
      if (!sup_page_table_insert(&thread_current()->spt, new_spte)) {
         free(new_spte);
         thread_exit(); //insertion failed
         NOT_REACHED();
      }
      lock_acquire(&new_spte->page_lock);
      if (!load_page(new_spte)) {
         lock_release(&new_spte->page_lock);
         free(new_spte);
         thread_exit(); //loading failed
         NOT_REACHED();
      }
      lock_release(&new_spte->page_lock);
      return;
   }
  }
#endif

   /* Page fault could not be handled - terminate process */

  // case two: the user process is causing page fault
  if (user) {
   printf ("Page fault at %p: %s error %s page in %s context.\n",
         fault_addr,
         not_present ? "not present" : "rights violation",
         write ? "writing" : "reading",
         user ? "user" : "kernel");
   
   thread_exit();
   NOT_REACHED();
  } else {
   // case three: if we get here, then the kernel is accessing 
   // kernel memory improperly somehow
   PANIC ("Kernel page fault at %p", fault_addr);
  }
}

bool
valid_stack_access(void *fault_addr, void *esp) {
   void* stack_extension = 0;
#ifdef VM
   stack_extension = MAX_STACK_SIZE;
#endif
    return is_user_vaddr(fault_addr) &&
           fault_addr >= esp - 32 &&
           fault_addr >= PHYS_BASE - stack_extension;
}