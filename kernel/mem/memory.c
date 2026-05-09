// SPDX-License-Identifier: GPL-3.0-only

#include "mm_kernel.h"
#include <hal/io.h>
#include <hal/mem.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/sys.h>

extern uint8_t __kernel_image_start;
extern uint8_t __end;

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
 * PollTotalMemory
 *
 * Derives total physical memory from the bootloader-agnostic BOOT_Info
 * stored in g_SysInfo->boot.
 *
 * Priority:
 *  1. Walk the BOOT_MemMapEntry table (memMapAddr / memMapLength) and find
 *     the highest physical address covered by a type-1 (available RAM) region.
 *  2. Fall back to totalMemoryUpper * 1024 (simple mem_upper KB field).
 *  3. Hard-coded 256 MB default if neither source is usable.
 *
 * Returns total addressable RAM in bytes (clamped to 32-bit space).
 */
static uint32_t PollTotalMemory(void)
{
   const uint32_t defaultMem = 256 * 1024 * 1024; /* 256 MB fallback */

   /* --- Prefer the full memory map when available ----------------------- */
   uint32_t mapAddr = g_SysInfo->boot.memMapAddr;
   uint32_t mapLength = g_SysInfo->boot.memMapLength;

   if (mapAddr >= 0x1000 && mapLength > 0)
   {
      BOOT_MemMapEntry *entry = (BOOT_MemMapEntry *)mapAddr;
      BOOT_MemMapEntry *mapEnd = (BOOT_MemMapEntry *)(mapAddr + mapLength);
      uint32_t highMark = 0;

      while (entry < mapEnd)
      {
         if (entry->type == 1) /* Available RAM */
         {
            /* Clamp to 32-bit address space; ignore regions above 4 GB. */
            if (entry->baseAddr < 0x100000000ULL)
            {
               uint64_t regionEnd = entry->baseAddr + entry->length;
               if (regionEnd > 0x100000000ULL) regionEnd = 0x100000000ULL;
               if ((uint32_t)regionEnd > highMark)
                  highMark = (uint32_t)regionEnd;
            }
         }
         /* Advance by (entry->size + sizeof(entry->size)) per spec. */
         entry = (BOOT_MemMapEntry *)((uint32_t)entry + entry->size +
                                      sizeof(entry->size));
      }
      if (highMark >= 16u * 1024u * 1024u) return highMark;
   }

   /* --- Fall back to the simple mem_upper field (KB above 1 MB) --------- */
   if (g_SysInfo->boot.totalMemoryUpper > 0)
   {
      uint32_t total = g_SysInfo->boot.totalMemoryUpper * 1024;
      if (total >= 16u * 1024u * 1024u) return total;
   }
   return defaultMem;
}

void MEM_Initialize(void)
{
   /* Derive total memory from g_SysInfo->boot (bootloader-agnostic) */
   uint32_t total_memory = PollTotalMemory();

   Heap_Initialize();
   Heap_SelfTest();
   Stack_Initialize();
   Stack_SelfTest();

   // Initialize physical memory manager before paging so page tables can use it
   PMM_Initialize(total_memory);
   PMM_SelfTest();

   // Paging after PMM so alloc_frame can use real frames
   g_HalPagingOperations->Initialize();
   g_HalPagingOperations->SelfTest();

   // Virtual memory manager on top of paging
   VMM_Initialize();
   VMM_SelfTest();

   /* Populate memory info in SYS_Info */
   g_SysInfo->memory.total_memory = total_memory;
   g_SysInfo->memory.page_size = HAL_ARCH_PAGE_SIZE;
   g_SysInfo->memory.kernel_start = (uint32_t)(uintptr_t)&__kernel_image_start;
   g_SysInfo->memory.kernel_end = (uint32_t)(uintptr_t)&__end;
   g_SysInfo->memory.user_start = (uint32_t)HAL_ARCH_CODE_START;
   g_SysInfo->memory.user_end = (uint32_t)HAL_ARCH_SPACE_END;
   g_SysInfo->memory.kernel_stack_size = 65536; /* 64KB kernel stack */
}
