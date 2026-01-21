// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_TLB_H
#define I686_TLB_H

#include <stdint.h>

/**
 * TLB (Translation Lookaside Buffer) Management for x86 i686
 *
 * The TLB is a hardware cache that speeds up virtual-to-physical address
 * translation. When the CPU needs to translate a virtual address, it first
 * checks the TLB:
 *  - TLB hit: Fast translation (nanoseconds)
 *  - TLB miss: Slow page table walk (hundreds of nanoseconds), then caches in
 * TLB
 *
 * We need to invalidate TLB entries when we modify page tables to ensure
 * the CPU doesn't use stale cached translations.
 */

/**
 * Invalidate a single TLB entry for a virtual address
 *
 * @param vaddr Virtual address to invalidate from TLB
 *
 * This is used when modifying page table entries for a specific address.
 * The inline assembly executes the INVLPG instruction on x86.
 */
static inline void tlb_invalidate_entry(uintptr_t vaddr)
{
   __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/**
 * Invalidate the entire TLB
 *
 * This is a heavy operation - used when:
 *  - Switching to a new process (new page directory)
 *  - Major memory subsystem changes
 *
 * Reloading CR3 invalidates all TLB entries.
 * It's slow but necessary for correctness.
 */
static inline void tlb_invalidate_all(void)
{
   uint32_t cr3;
   __asm__ __volatile__("movl %%cr3, %0" : "=r"(cr3));
   __asm__ __volatile__("movl %0, %%cr3" : : "r"(cr3));
}

/**
 * Invalidate TLB for a range of virtual addresses
 *
 * @param vaddr_start Start of virtual address range
 * @param vaddr_end   End of virtual address range (exclusive)
 *
 * For large ranges, it may be faster to use tlb_invalidate_all()
 * This is efficient for smaller ranges (e.g., unmapping a few pages).
 */
static inline void tlb_invalidate_range(uintptr_t vaddr_start,
                                        uintptr_t vaddr_end)
{
   for (uintptr_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += 0x1000)
   {
      tlb_invalidate_entry(vaddr);
   }
}

/**
 * Get current CR3 (page directory base address)
 *
 * CR3 contains the physical address of the currently active page directory.
 * Used to determine which process's address space is active.
 */
static inline uint32_t tlb_get_cr3(void)
{
   uint32_t cr3;
   __asm__ __volatile__("movl %%cr3, %0" : "=r"(cr3));
   return cr3;
}

/**
 * Set CR3 to a new page directory
 *
 * @param page_dir_phys Physical address of new page directory
 *
 * This switches the active page directory and automatically invalidates all TLB
 * entries. Used during process context switches.
 */
static inline void tlb_set_cr3(uint32_t page_dir_phys)
{
   __asm__ __volatile__("movl %0, %%cr3" : : "r"(page_dir_phys));
}

/**
 * Get TLB statistics (if implemented by CPU)
 *
 * Note: x86 does not provide direct access to TLB statistics.
 * This is architecture-dependent and not available on i386/i686.
 * For now, this file documents that TLB management is available.
 */

/**
 * Prefetch into TLB (hints the CPU to load a TLB entry)
 *
 * @param vaddr Virtual address to prefetch
 *
 * Some x86 variants support PREFETCHT0 which can help with TLB prefetching,
 * but it's not a direct TLB operation. This is rarely used in practice.
 */
static inline void tlb_prefetch(uintptr_t vaddr)
{
   __asm__ __volatile__("prefetcht0 (%0)" : : "r"(vaddr));
}

#endif