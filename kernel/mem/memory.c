// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mm_kernel.h"
#include <hal/io.h>
#include <hal/paging.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/sys.h>

/* Runtime-controlled memory debug flag. Set non-zero to make the handler
 * call `i686_Panic()` when a memory safety fault is detected. Default is 0.
 */
int memory_debug = 0;

/* Called from assembly on memory faults (overflow/null/unsafe).
 * Parameters (cdecl): void *addr, size_t len, int code
 * code: 1=memcpy fault, 2=memcmp fault, 3=memset fault
 */
void mem_fault_handler(void *addr, size_t len, int code)
{
   (void)addr;
   (void)len;
   (void)code;
   if (memory_debug)
   {
      i686_Panic();
   }
   /* Otherwise return and let caller continue (safe no-op behavior). */
}

/* Basic memory helpers */
/* We implement the hot paths `memcpy` and `memcmp` in assembly for
 * performance. The assembly provides `memcpy_asm` and `memcmp_asm`;
 * here we provide small C wrappers that forward to those symbols.
 */
extern void *memcpy_asm(void *dst, const void *src, size_t num);
void *memcpy(void *dst, const void *src, size_t num)
{
   return memcpy_asm(dst, src, num);
}

extern void *memset_asm(void *ptr, int value, size_t num);
void *memset(void *ptr, int value, size_t num)
{
   return memset_asm(ptr, value, num);
}

extern int memcmp_asm(const void *ptr1, const void *ptr2, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
   return memcmp_asm(ptr1, ptr2, num);
}

void *SegmentOffsetToLinear(void *addr)
{
   uint32_t offset = (uint32_t)(addr) & 0xffff;
   uint32_t segment = (uint32_t)(addr) >> 16;
   return (void *)(segment * 16 + offset);
}

void *memmove(void *dest, const void *src, size_t n)
{
   char *d = (char *)dest;
   const char *s = (const char *)src;

   if (d == s || n == 0)
   {
      return dest; // No copy needed if same or zero bytes
   }

   if (d < s)
   {
      // Destination is before source, copy forwards
      for (size_t i = 0; i < n; ++i)
      {
         d[i] = s[i];
      }
   }
   else
   {
      // Destination is after source, or overlaps in a way that requires
      // copying backwards to avoid overwriting source data before it's read.
      for (size_t i = n; i > 0; --i)
      {
         d[i - 1] = s[i - 1];
      }
   }
   return dest;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
   if (n == 0) return (0);
   do {
      if (*s1 != *s2++)
         /*
          * We could return *s1 - *--s2, but that's not
          * guaranteed to be in the range of int.  Better
          * to do the right thing instead.
          */
         return (*(unsigned char *)s1 - *(unsigned char *)--s2);
      if (*s1++ == 0) break;
   } while (--n != 0);
   return (0);
}

/**
 * Parse Multiboot memory map to detect total system memory
 * Returns total memory in bytes
 */
static uint32_t parse_multiboot_memory(multiboot_info_t *mbi)
{
   uint32_t total_mem = 0;
   const uint32_t default_mem = 256 * 1024 * 1024; /* Default: 256 MB */

   /* Validate pointer is in a reasonable range */
   if (!mbi || (uint32_t)mbi < 0x1000 || (uint32_t)mbi > 0x100000)
   {
      return default_mem;
   }

   /* Check if memory info is available (flags bit 0) */
   if (mbi->flags & 0x01)
   {
      /* mem_lower = KB below 1MB, mem_upper = KB above 1MB */
      total_mem = (mbi->mem_lower + mbi->mem_upper) * 1024;
      /* Sanity check: memory should be at least 16MB and less than 64GB */
      if (total_mem >= 16 * 1024 * 1024 && total_mem <= 0xFFFFFFFF)
      {
         return total_mem;
      }
   }

   /* Check if memory map is available (flags bit 6) */
   if (mbi->flags & 0x40)
   {
      /* Validate mmap_addr is reasonable */
      if (mbi->mmap_addr < 0x1000 || mbi->mmap_addr > 0x100000)
      {
         return default_mem;
      }

      multiboot_mmap_entry_t *mmap = (multiboot_mmap_entry_t *)mbi->mmap_addr;
      multiboot_mmap_entry_t *mmap_end =
          (multiboot_mmap_entry_t *)(mbi->mmap_addr + mbi->mmap_length);

      while (mmap < mmap_end)
      {
         if (mmap->type == 1) /* Available RAM */
         {
            uint64_t region_end = mmap->base_addr + mmap->length;
            if (region_end > total_mem)
            {
               total_mem = (uint32_t)region_end;
            }
         }
         mmap = (multiboot_mmap_entry_t *)((uint32_t)mmap + mmap->size +
                                           sizeof(mmap->size));
      }

      /* Sanity check result */
      if (total_mem >= 16 * 1024 * 1024 && total_mem <= 0xFFFFFFFF)
      {
         return total_mem;
      }
   }

   /* No valid memory info available, use default */
   return default_mem;
}

void MEM_Initialize(void *multiboot_info_ptr)
{
   /* Detect total memory from Multiboot info */
   uint32_t total_memory =
       parse_multiboot_memory((multiboot_info_t *)multiboot_info_ptr);

   Heap_Initialize();
   Heap_SelfTest();
   Stack_Initialize();
   Stack_SelfTest();

   // Initialize physical memory manager before paging so page tables can use it
   PMM_Initialize(total_memory);
   PMM_SelfTest();

   // Paging after PMM so alloc_frame can use real frames
   HAL_Paging_Initialize();
   HAL_Paging_SelfTest();

   // Virtual memory manager on top of paging
   VMM_Initialize();
   VMM_SelfTest();

   /* Populate memory info in SYS_Info */
   g_SysInfo->memory.total_memory = total_memory;
   g_SysInfo->memory.page_size = PAGE_SIZE;
   g_SysInfo->memory.kernel_start = (uint32_t)0x00A00000;
   g_SysInfo->memory.kernel_end =
       g_SysInfo->memory.kernel_start + 0x100000; /* Approximate */
   g_SysInfo->memory.user_start = (uint32_t)0x08000000;
   g_SysInfo->memory.user_end = (uint32_t)0xC0000000;
   g_SysInfo->memory.kernel_stack_size = 8192; /* 8KB kernel stack */
}