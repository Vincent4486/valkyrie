// SPDX-License-Identifier: GPL-3.0-only
/*
 * kernel/arch/i686/boot/parser.c
 *
 * Pre-kernel Multiboot v1 → BOOT_Info translator.
 *
 * Execution context: called from entry.S before start() / kernel_main.
 * Constraints:
 *   - Heap is NOT yet available.
 *   - BSS may NOT yet be zeroed (the BSS wipe lives inside start()).
 *   - Only static/stack storage and hand-written loops are safe here.
 *
 * Design: a single file-scope static BOOT_Info (s_bootInfo) acts as the
 * pre-heap buffer.  It is explicitly zeroed at the top of Parser_Multiboot
 * to avoid relying on BSS initialisation.
 */

#include "parser.h"
#include <mem/mm_kernel.h>
#include <stdint.h>
#include <valkyrie/system.h>

/* -------------------------------------------------------------------------
 * Forward declaration: the kernel entry point defined in main.c.
 * ------------------------------------------------------------------------- */
extern void start(BOOT_Info *boot);

/* -------------------------------------------------------------------------
 * Pre-heap storage for the parsed boot parameters.
 * Explicitly zeroed inside Parser_Multiboot rather than relying on BSS.
 * ------------------------------------------------------------------------- */
static BOOT_Info *s_bootInfo = (BOOT_Info *)(uintptr_t)K_MEM_BOOT_INFO_START;

/* -------------------------------------------------------------------------
 * CopyString
 *
 * Copies at most (maxLen - 1) bytes from a physical-address C string into
 * dst[], always null-terminating the result.  Safe to call before the heap
 * or C runtime are initialised.
 * ------------------------------------------------------------------------- */
static void CopyString(char *dst, const char *src, uint32_t maxLen)
{
   uint32_t i = 0;

   if (!dst || maxLen == 0) return;

   if (src)
   {
      while (i < (maxLen - 1) && src[i] != '\0')
      {
         dst[i] = src[i];
         i++;
      }
   }

   dst[i] = '\0';
}

/* -------------------------------------------------------------------------
 * ZeroBytes
 *
 * Portable byte-loop memset substitute; avoids any dependency on the kernel
 * memory subsystem during the pre-heap phase.
 * ------------------------------------------------------------------------- */
static void ZeroBytes(void *ptr, uint32_t len)
{
   uint8_t *p = (uint8_t *)ptr;
   for (uint32_t i = 0; i < len; i++) p[i] = 0;
}

/* -------------------------------------------------------------------------
 * Parser_Multiboot
 *
 * Translates a Multiboot v1 information structure into a kernel-internal
 * BOOT_Info, then hands control to start().
 *
 * Called (cdecl) from entry.S with:
 *   pushl %ebx   ; 2nd arg: mbi pointer
 *   pushl %eax   ; 1st arg: magic
 *   call  Parser_Multiboot
 * ------------------------------------------------------------------------- */
void Parser_Multiboot(uint32_t magic, multiboot_info_t *mbi)
{
   /* --- Explicit zero-init (BSS may not be cleared yet) ----------------- */
   ZeroBytes(s_bootInfo, sizeof(BOOT_Info));

   /* --- Validate Multiboot magic number --------------------------------- */
   if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
   {
      /*
       * Not a Multiboot-compliant boot; halt the processor.
       * No panic/printf available at this stage.
       */
      for (;;) __asm__ volatile("hlt");
   }

   /* --- Sanity-check the info pointer ----------------------------------- */
   if (!mbi || (uint32_t)mbi < 0x1000)
   {
      for (;;) __asm__ volatile("hlt");
   }

   /* --- Command line (Multiboot flags bit 2) ----------------------------- */
   if (mbi->flags & (1u << 2))
   {
      CopyString(s_bootInfo->commandLine, (const char *)(uintptr_t)mbi->cmdline,
                 sizeof(s_bootInfo->commandLine));
   }

   /* --- Bootloader name (Multiboot flags bit 9) ------------------------- */
   if (mbi->flags & (1u << 9))
   {
      CopyString(s_bootInfo->bootLoaderName,
                 (const char *)(uintptr_t)mbi->boot_loader_name,
                 sizeof(s_bootInfo->bootLoaderName));
   }

   /* --- Basic memory bounds (Multiboot flags bit 0) --------------------- */
   if (mbi->flags & (1u << 0))
   {
      /*
       * mem_upper: kilobytes of contiguous RAM starting at 1 MB.
       * $TotalPages = \frac{g\_SysInfo.boot.totalMemoryUpper \times
       * 1024}{4096}$
       */
      s_bootInfo->totalMemoryUpper = mbi->mem_upper;
   }

   /* --- Memory map (Multiboot flags bit 6) ------------------------------ */
   if (mbi->flags & (1u << 6))
   {
      s_bootInfo->memMapAddr = mbi->mmap_addr;
      s_bootInfo->memMapLength = mbi->mmap_length;
   }

   /* --- Hand off to the kernel ------------------------------------------ */
   start(s_bootInfo);

   /* Should never reach here; loop defensively. */
   for (;;) __asm__ volatile("hlt");
}
