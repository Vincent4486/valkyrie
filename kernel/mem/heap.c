// SPDX-License-Identifier: GPL-3.0-only

#include "mm_kernel.h"
#include "mm_proc.h"
#include <cpu/process.h>
#include <hal/paging.h>
#include <std/stdio.h>
#include <stddef.h>
#include <stdint.h>

extern uint8_t __end; /* linker-provided end of kernel image */

/* Simple bump allocator state */
static uintptr_t heap_start = 0;
static uintptr_t heap_end = 0;
static uintptr_t heap_ptr = 0;

/* Heap block header for safety checks */
typedef struct
{
   size_t size;
   uint32_t canary_before;
   uint32_t canary_after;
} HeapBlockHeader;

#define HEAP_CANARY 0xDEADBEEF

int Heap_ProcessInitialize(Process *proc, uint32_t heap_start_va)
{
   if (!proc) return -1;

   proc->heap_start = heap_start_va;
   proc->heap_end = heap_start_va;

   // Allocate initial heap page
   uint32_t phys = PMM_AllocatePhysicalPage();
   if (phys == 0)
   {
      printf("[HEAP] Heap_Initialize: PMM_AllocatePhysicalPage failed\n");
      return -1;
   }

   // Map to process page directory
   if (!g_HalPagingOperations->MapPage(proc->page_directory, heap_start_va,
                                       phys, 0x007))
   { // RW, Present
      printf("[HEAP] Heap_Initialize: map_page failed\n");
      PMM_FreePhysicalPage(phys);
      return -1;
   }

   proc->heap_end = heap_start_va + PAGE_SIZE;
   return 0;
}

int Heap_ProcessBrk(Process *proc, void *addr)
{
   if (!proc) return -1;

   uintptr_t target = (uintptr_t)addr;
   if (target < proc->heap_start || target > HEAP_MAX) return -1;

   // If extending heap, allocate pages
   if (target > proc->heap_end)
   {
      uint32_t pages_needed =
          (target - proc->heap_end + PAGE_SIZE - 1) / PAGE_SIZE;
      for (uint32_t i = 0; i < pages_needed; ++i)
      {
         uint32_t va = proc->heap_end + (i * PAGE_SIZE);
         uint32_t phys = PMM_AllocatePhysicalPage();
         if (phys == 0)
         {
            printf("[HEAP] brk: PMM_AllocatePhysicalPage failed at page "
                   "%u/%u\n",
                   i, pages_needed);
            return -1;
         }
         if (!g_HalPagingOperations->MapPage(proc->page_directory, va, phys,
                                             0x007))
         { // RW, Present
            printf("[HEAP] brk: map_page failed at 0x%08x\n", va);
            PMM_FreePhysicalPage(phys);
            return -1;
         }
      }
   }

   proc->heap_end = target;
   return 0;
}

void *Heap_ProcessSbrk(Process *proc, intptr_t inc)
{
   if (!proc) return (void *)-1;

   uintptr_t old = proc->heap_end;
   if (inc == 0) return (void *)old;

   uintptr_t new_end = proc->heap_end + inc;
   if ((inc > 0 && new_end < proc->heap_end) || new_end > HEAP_MAX)
      return (void *)-1;
   if (new_end < proc->heap_start) return (void *)-1;

   if (Heap_ProcessBrk(proc, (void *)new_end) == -1) return (void *)-1;

   return (void *)old;
}

static uintptr_t align_up(uintptr_t v, size_t align)
{
   uintptr_t mask = (align - 1);
   return (v + mask) & ~mask;
}

void Heap_Initialize(void)
{
   /* place heap just after the kernel image end symbol */
   heap_start = align_up((uintptr_t)&__end, 8);

   /* Set heap to a reasonable size - 64 MiB should be plenty for a kernel */
   const uintptr_t heap_size = 64 * 1024 * 1024u; // 64 MiB
   uintptr_t desired_end = heap_start + heap_size;

   /* Check for overflow and cap at 32-bit max */
   if (desired_end < heap_start || desired_end > 0xFFFFFFFFu)
   {
      heap_end = 0xFFFFFFFFu;
   }
   else
   {
      heap_end = desired_end;
   }

   heap_ptr = heap_start;

   printf("[heap] initialized: start=0x%08x end=0x%08x size=%u MB\n",
          (uint32_t)heap_start, (uint32_t)heap_end,
          (uint32_t)((heap_end - heap_start) / (1024 * 1024)));
}

void *kmalloc(size_t size)
{
   if (size == 0) return NULL;

   /* Allocate extra space for header with canaries */
   size_t total = size + sizeof(HeapBlockHeader);
   uintptr_t cur = align_up(heap_ptr, 8);

   if (cur > heap_end)
   {
      printf("[heap] kmalloc: EXHAUSTED (cur=0x%08x > end=0x%08x)\n",
             (uint32_t)cur, (uint32_t)heap_end);
      return NULL; /* heap already exhausted */
   }

   /* available bytes from cur to heap_end (inclusive) */
   uintptr_t avail = (heap_end - cur) + 1;
   if (total > avail)
   {
      printf("[heap] kmalloc: OUT OF MEMORY (need=%u avail=%u)\n",
             (uint32_t)total, (uint32_t)avail);
      return NULL; /* not enough room */
   }

   /* Write header with canaries */
   HeapBlockHeader *header = (HeapBlockHeader *)cur;
   header->size = size;
   header->canary_before = HEAP_CANARY;
   header->canary_after = HEAP_CANARY;

   heap_ptr = cur + total;

   /* Return pointer after header */
   return (void *)(cur + sizeof(HeapBlockHeader));
}

void *kzalloc(size_t size)
{
   void *p = kmalloc(size);
   if (!p) return NULL;
   memset(p, 0, size);
   return p;
}

uintptr_t mem_heap_start(void) { return heap_start; }
uintptr_t mem_heap_end(void) { return heap_end; }

void heap_check_integrity(void)
{
   uintptr_t cur = heap_start;
   uint32_t block_count = 0;

   while (cur < heap_ptr)
   {
      HeapBlockHeader *h = (HeapBlockHeader *)cur;

      if (h->canary_before != HEAP_CANARY || h->canary_after != HEAP_CANARY)
      {
         printf("[HEAP] CORRUPTION at 0x%08x! Block size=%u "
                "canary_before=0x%08x canary_after=0x%08x\n",
                (uint32_t)cur, (uint32_t)h->size, h->canary_before,
                h->canary_after);
         /* Call panic function if available */
         printf("[HEAP] PANIC: Heap corruption detected!\n");
         while (1)
         {
         } /* Hang */
      }

      cur += sizeof(HeapBlockHeader) + h->size;
      block_count++;
   }

   printf("[heap] integrity check passed: %u blocks verified\n", block_count);
}

void free(void *ptr)
{
   /* No-op: bump allocator does not reclaim memory. */
   (void)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
   size_t total = nmemb * size;
   return kzalloc(total);
}

void *realloc(void *ptr, size_t size)
{
   if (!ptr) return kmalloc(size);
   if (size == 0)
   {
      free(ptr);
      return NULL;
   }

   void *n = kmalloc(size);
   if (!n) return NULL;
   memcpy(n, ptr, size);
   return n;
}

/* brk/sbrk -------------------------------------------------------------- */
int brk(void *addr)
{
   uintptr_t target = (uintptr_t)addr;
   if (target < heap_start || target > heap_end) return -1;
   heap_ptr = target;
   return 0;
}

void *sbrk(intptr_t inc)
{
   uintptr_t old = heap_ptr;
   if (inc == 0) return (void *)old;

   uintptr_t new_ptr = heap_ptr + inc;
   if ((inc > 0 && new_ptr < heap_ptr) || new_ptr > heap_end) return (void *)-1;
   if (new_ptr < heap_start) return (void *)-1;

   heap_ptr = new_ptr;
   return (void *)old;
}

/* Self-test ------------------------------------------------------------- */
void Heap_SelfTest(void)
{
   printf("[HEAP] start=0x%08x end=0x%08x\n", (uint32_t)heap_start,
          (uint32_t)heap_end);

   char *p = (char *)kmalloc(32);
   if (!p)
   {
      printf("[HEAP] kmalloc failed\n");
      return;
   }
   for (int i = 0; i < 32; ++i) p[i] = (char)(i + 1);

   char *q = (char *)realloc(p, 64);
   if (!q)
   {
      printf("[HEAP] realloc failed\n");
      return;
   }
   int ok = 1;
   for (int i = 0; i < 32; ++i)
      if (q[i] != (char)(i + 1)) ok = 0;

   char *z = (char *)calloc(4, 8);
   int zeroed = 1;
   for (int i = 0; i < 32; ++i)
      if (z[i] != 0)
      {
         zeroed = 0;
         break;
      }

   void *brk0 = sbrk(0);
   void *brk1 = sbrk(4096);
   int brk_ok = (brk1 != (void *)-1);
   brk(brk0);

   printf("[HEAP] test kmalloc/realloc copy=%s, calloc zero=%s, sbrk=%s\n",
          ok ? "OK" : "FAIL", zeroed ? "OK" : "FAIL", brk_ok ? "OK" : "FAIL");
}
