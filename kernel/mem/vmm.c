// SPDX-License-Identifier: GPL-3.0-only

#include <hal/paging.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_ALIGN_DOWN(v) ((v) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(v) (((v) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

static void *kernel_page_dir = NULL;
static uint32_t kernel_next_vaddr =
    0x80000000u; // Start at 2 GiB for kernel mappings

#ifndef KERNEL_BASE
#define KERNEL_BASE 0xC0000000u
#endif

void VMM_Initialize(void)
{
   // Get the kernel page directory from paging subsystem
   kernel_page_dir = g_HalPagingOperations->GetCurrentPageDirectory();
   if (!kernel_page_dir)
   {
      printf("[VMM] ERROR: no kernel page directory!\n");
      // Skip further VMM work to avoid faults
      return;
   }
   printf("[VMM] initialized with kernel page dir at 0x%08x\n",
          (uint32_t)kernel_page_dir);
}

void *VMM_AllocateInDir(void *page_dir, uint32_t *next_vaddr_state,
                        uint32_t size, uint32_t flags)
{
   if (size == 0) return NULL;

   // Align size to page boundary
   uint32_t aligned_size = PAGE_ALIGN_UP(size);
   uint32_t num_pages = aligned_size / PAGE_SIZE;

   // Choose bump pointer: per-dir state or kernel default
   uint32_t *bump = next_vaddr_state ? next_vaddr_state : &kernel_next_vaddr;

   // Find a free virtual address range (simple bump allocator)
   if (*bump + aligned_size > KERNEL_BASE || *bump + aligned_size < *bump)
   {
      printf("[VMM] VMM_Allocate: virtual address space exhausted\n");
      return NULL;
   }

   uint32_t vaddr = *bump;
   *bump += aligned_size;

   // Allocate and map physical pages
   uint32_t mapped_pages = 0;
   for (uint32_t i = 0; i < num_pages; ++i)
   {
      uint32_t paddr = PMM_AllocatePhysicalPage();
      if (paddr == 0)
      {
         printf("[VMM] VMM_Allocate: failed to allocate physical page %u/%u\n",
                i + 1, num_pages);
         goto fail_cleanup;
      }

      uint32_t va = vaddr + (i * PAGE_SIZE);
      if (!g_HalPagingOperations->MapPage(page_dir, va, paddr,
                                          flags | HAL_PAGE_PRESENT))
      {
         printf("[VMM] VMM_Allocate: failed to map page at 0x%08x\n", va);
         PMM_FreePhysicalPage(paddr);
         goto fail_cleanup;
      }

      // Zero-fill only if we're in the same active address space
      if (g_HalPagingOperations->GetCurrentPageDirectory() == page_dir)
      {
         memset((void *)va, 0, PAGE_SIZE);
      }
      mapped_pages++;
   }

   return (void *)vaddr;

fail_cleanup:
   for (uint32_t j = 0; j < mapped_pages; ++j)
   {
      uint32_t va_cleanup = vaddr + (j * PAGE_SIZE);
      uint32_t pa_cleanup =
          g_HalPagingOperations->GetPhysicalAddress(page_dir, va_cleanup);
      g_HalPagingOperations->UnmapPage(page_dir, va_cleanup);
      if (pa_cleanup) PMM_FreePhysicalPage(pa_cleanup);
   }
   return NULL;
}

void *VMM_Allocate(uint32_t size, uint32_t flags)
{
   return VMM_AllocateInDir(kernel_page_dir, &kernel_next_vaddr, size, flags);
}

void VMM_FreeInDir(void *page_dir, void *vaddr, uint32_t size)
{
   if (!vaddr || size == 0) return;

   uint32_t va = (uint32_t)vaddr;
   uint32_t aligned_size = PAGE_ALIGN_UP(size);
   uint32_t num_pages = aligned_size / PAGE_SIZE;

   // Unmap and free each page
   for (uint32_t i = 0; i < num_pages; ++i)
   {
      uint32_t page_va = va + (i * PAGE_SIZE);
      uint32_t page_pa =
          g_HalPagingOperations->GetPhysicalAddress(page_dir, page_va);

      if (page_pa != 0)
      {
         g_HalPagingOperations->UnmapPage(page_dir, page_va);
         PMM_FreePhysicalPage(page_pa);
      }
   }
}

void VMM_Free(void *vaddr, uint32_t size)
{
   VMM_FreeInDir(kernel_page_dir, vaddr, size);
}

bool VMM_MapInDir(void *page_dir, uint32_t vaddr, uint32_t paddr, uint32_t size,
                  uint32_t flags)
{
   if (size == 0) return false;

   uint32_t aligned_size = PAGE_ALIGN_UP(size);
   uint32_t num_pages = aligned_size / PAGE_SIZE;

   uint32_t mapped_pages = 0;
   for (uint32_t i = 0; i < num_pages; ++i)
   {
      uint32_t va = vaddr + (i * PAGE_SIZE);
      uint32_t pa = paddr + (i * PAGE_SIZE);

      if (!g_HalPagingOperations->MapPage(page_dir, va, pa,
                                          flags | HAL_PAGE_PRESENT))
      {
         printf("[VMM] VMM_Map: failed at offset 0x%x\n", i * PAGE_SIZE);
         goto rollback;
      }
      mapped_pages++;
   }

   return true;

rollback:
   for (uint32_t j = 0; j < mapped_pages; ++j)
   {
      uint32_t va_cleanup = vaddr + (j * PAGE_SIZE);
      g_HalPagingOperations->UnmapPage(page_dir, va_cleanup);
   }
   return false;
}

bool VMM_Map(uint32_t vaddr, uint32_t paddr, uint32_t size, uint32_t flags)
{
   return VMM_MapInDir(kernel_page_dir, vaddr, paddr, size, flags);
}

bool VMM_UnmapInDir(void *page_dir, uint32_t vaddr, uint32_t size)
{
   if (size == 0) return true;

   uint32_t aligned_size = PAGE_ALIGN_UP(size);
   uint32_t num_pages = aligned_size / PAGE_SIZE;

   for (uint32_t i = 0; i < num_pages; ++i)
   {
      uint32_t va = vaddr + (i * PAGE_SIZE);
      g_HalPagingOperations->UnmapPage(page_dir, va);
   }

   return true;
}

bool VMM_Unmap(uint32_t vaddr, uint32_t size)
{
   return VMM_UnmapInDir(kernel_page_dir, vaddr, size);
}

uint32_t VMM_GetPhysInDir(void *page_dir, uint32_t vaddr)
{
   return g_HalPagingOperations->GetPhysicalAddress(page_dir, vaddr);
}

uint32_t VMM_GetPhys(uint32_t vaddr)
{
   return VMM_GetPhysInDir(kernel_page_dir, vaddr);
}

void *VMM_GetPageDirectory(void) { return kernel_page_dir; }

void VMM_SelfTest(void)
{
   printf("[VMM] self-test: starting\n");

   // Allocate 3 pages via VMM
   void *v1 = VMM_Allocate(PAGE_SIZE, VMM_DEFAULT);
   void *v2 = VMM_Allocate(PAGE_SIZE * 2, VMM_DEFAULT);

   if (!v1 || !v2)
   {
      printf("[VMM] self-test: FAIL (VMM_Allocate returned NULL)\n");
      return;
   }

   // Write and read through virtual addresses
   volatile uint32_t *ptr1 = (volatile uint32_t *)v1;
   volatile uint32_t *ptr2 = (volatile uint32_t *)v2;

   *ptr1 = 0xDEADBEEFu;
   *ptr2 = 0xCAFEBABEu;

   uint32_t val1 = *ptr1;
   uint32_t val2 = *ptr2;

   if (val1 != 0xDEADBEEFu || val2 != 0xCAFEBABEu)
   {
      printf("[VMM] self-test: FAIL (write/read mismatch)\n");
      return;
   }

   // Verify physical addresses are different
   uint32_t pa1 = VMM_GetPhys((uint32_t)v1);
   uint32_t pa2 = VMM_GetPhys((uint32_t)v2);

   if (pa1 == 0 || pa2 == 0 || pa1 == pa2)
   {
      printf("[VMM] self-test: FAIL (physical address issue)\n");
      return;
   }

   // Free and verify unmapped
   VMM_Free(v1, PAGE_SIZE);
   uint32_t pa1_after = VMM_GetPhys((uint32_t)v1);

   if (pa1_after != 0)
   {
      printf("[VMM] self-test: FAIL (page not unmapped)\n");
      return;
   }

   printf("[VMM] self-test: PASS (alloc/map/write/read/free)\n");
}
