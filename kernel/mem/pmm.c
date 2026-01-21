// SPDX-License-Identifier: GPL-3.0-only

#include "mm_kernel.h"
#include "mm_proc.h"
#include <std/stdio.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_BYTE 8

/* Bitmap to track free/allocated pages
 * bit=0: free, bit=1: allocated
 * We allocate the bitmap itself from identity-mapped kernel space
 */
static uint8_t *page_bitmap = NULL;
static uint32_t total_pages = 0;
static uint32_t allocated_count = 0;
static int pmm_initialized = 0;

static void bitmap_set(uint32_t page_idx)
{
   uint32_t byte_idx = page_idx / BITS_PER_BYTE;
   uint32_t bit_idx = page_idx % BITS_PER_BYTE;
   page_bitmap[byte_idx] |= (1u << bit_idx);
   allocated_count++;
}

static void bitmap_clear(uint32_t page_idx)
{
   uint32_t byte_idx = page_idx / BITS_PER_BYTE;
   uint32_t bit_idx = page_idx % BITS_PER_BYTE;
   page_bitmap[byte_idx] &= ~(1u << bit_idx);
   if (allocated_count > 0) allocated_count--;
}

static bool bitmap_is_set(uint32_t page_idx)
{
   uint32_t byte_idx = page_idx / BITS_PER_BYTE;
   uint32_t bit_idx = page_idx % BITS_PER_BYTE;
   return (page_bitmap[byte_idx] & (1u << bit_idx)) != 0;
}

void PMM_Initialize(uint32_t total_mem_bytes)
{
   pmm_initialized = 1;
   // Calculate number of pages
   total_pages = (total_mem_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

   // Bitmap size in bytes
   uint32_t bitmap_bytes = (total_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;

   // Allocate bitmap from lower memory (identity-mapped)
   // For now, use a static buffer to avoid chicken-and-egg
   // In a real system, you'd place this in a reserved region
   static uint8_t bitmap_storage[131072]; // max ~1M pages (4 GiB)
   page_bitmap = bitmap_storage;

   if (bitmap_bytes > sizeof(bitmap_storage))
   {
      printf("[PMM] WARNING: bitmap too small for %u pages\n", total_pages);
      total_pages = sizeof(bitmap_storage) * BITS_PER_BYTE;
      bitmap_bytes = sizeof(bitmap_storage);
   }

   // Initially all pages are free (bitmap = 0)
   memset(page_bitmap, 0, bitmap_bytes);
   allocated_count = 0;

   // Reserve pages 0-2 MiB for kernel/boot (0x00000 - 0x200000)
   uint32_t reserved_pages = (2 * 1024 * 1024) / PAGE_SIZE;
   for (uint32_t i = 0; i < reserved_pages && i < total_pages; ++i)
   {
      bitmap_set(i);
   }

   printf("[PMM] init: total=%u pages, reserved=%u, free=%u\n", total_pages,
          reserved_pages, total_pages - allocated_count);
}

int PMM_IsInitialized(void) { return pmm_initialized; }

uint32_t PMM_AllocatePhysicalPage(void)
{
   if (!page_bitmap) return 0;

   // Simple linear search for a free page
   for (uint32_t i = 0; i < total_pages; ++i)
   {
      if (!bitmap_is_set(i))
      {
         bitmap_set(i);
         return i * PAGE_SIZE;
      }
   }

   printf("[PMM] PMM_AllocatePhysicalPage: out of memory\n");
   return 0;
}

void PMM_FreePhysicalPage(uint32_t addr)
{
   if (!page_bitmap || (addr % PAGE_SIZE) != 0) return;

   uint32_t page_idx = addr / PAGE_SIZE;
   if (page_idx >= total_pages) return;

   if (bitmap_is_set(page_idx))
   {
      bitmap_clear(page_idx);
   }
}

bool PMM_IsPhysicalPageFree(uint32_t addr)
{
   if (!page_bitmap || (addr % PAGE_SIZE) != 0) return false;

   uint32_t page_idx = addr / PAGE_SIZE;
   if (page_idx >= total_pages) return false;

   return !bitmap_is_set(page_idx);
}

uint32_t PMM_TotalMemory(void) { return total_pages * PAGE_SIZE; }

uint32_t PMM_FreePages(void) { return total_pages - allocated_count; }

uint32_t PMM_AllocatedPages(void) { return allocated_count; }

void PMM_SelfTest(void)
{
   printf("[PMM] self-test: starting\n");

   // Allocate a few pages
   uint32_t p1 = PMM_AllocatePhysicalPage();
   uint32_t p2 = PMM_AllocatePhysicalPage();
   uint32_t p3 = PMM_AllocatePhysicalPage();

   if (!p1 || !p2 || !p3)
   {
      printf("[PMM] self-test: FAIL (alloc returned 0)\n");
      return;
   }

   // Check they're page-aligned and different
   if ((p1 % PAGE_SIZE) || (p2 % PAGE_SIZE) || (p3 % PAGE_SIZE))
   {
      printf("[PMM] self-test: FAIL (not page-aligned)\n");
      return;
   }

   if (p1 == p2 || p2 == p3 || p1 == p3)
   {
      printf("[PMM] self-test: FAIL (pages are same)\n");
      return;
   }

   // Free and check
   PMM_FreePhysicalPage(p2);
   if (!PMM_IsPhysicalPageFree(p2))
   {
      printf("[PMM] self-test: FAIL (free didn't work)\n");
      return;
   }

   // Reallocate should get p2 back
   uint32_t p2_new = PMM_AllocatePhysicalPage();
   if (p2_new != p2)
   {
      printf("[PMM] self-test: FAIL (realloc didn't get same page)\n");
      return;
   }

   printf("[PMM] self-test: PASS (allocated %u, freed, reallocated)\n", p1);
}
