// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef I686_VM_LAYOUT_H
#define I686_VM_LAYOUT_H

#include <stdint.h>

/**
 * x86 32-bit Virtual Memory Layout (4GB total)
 *
 * This file defines the virtual address space organization for ValkyrieOS.
 * The layout divides 4GB between kernel and user space with specific regions
 * for different purposes.
 */

/* ========== KERNEL SPACE (High addresses, 3GB - 4GB) ========== */

/** Kernel base address - identity mapped at 3GB (0xC0000000) */
#define KERNEL_BASE 0xC0000000UL

/** Kernel code/data section start (loaded at 10MiB physical) */
#define KERNEL_CODE_START (KERNEL_BASE + 0x00A00000UL) // 3GB + 10MiB

/** Kernel code/data section end (before reserved area) */
#define KERNEL_CODE_END                                                        \
   (KERNEL_BASE + 0x00600000UL) // 3GB + 6MiB (1MiB reserved before)

/* ========== SYSTEM RESERVED REGION (1MiB - 10MiB, mapped at 3GB + 1MiB)
 * ========== */

/** System reserved region start (for bootloader, BIOS data) */
#define SYSTEM_RESERVED_START (KERNEL_BASE + 0x00100000UL) // 3GB + 1MiB

/** System reserved region end (before kernel code) */
#define SYSTEM_RESERVED_END (KERNEL_BASE + 0x00A00000UL) // 3GB + 10MiB

/* ========== VIDEO/DISPLAY BUFFER (8MiB physical) ========== */

/** Video memory buffer location (text mode buffer at 8MiB physical)
 *  Mapped to kernel virtual space */
#define VIDEO_MEMORY_PHYS 0x00800000UL                 // 8MiB physical
#define VIDEO_MEMORY_VIRT (KERNEL_BASE + 0x00800000UL) // 3GB + 8MiB virtual

/** Video buffer size (text mode: 80x25 chars * 2 bytes = 4KB minimum) */
#define VIDEO_BUFFER_SIZE 0x1000UL // 4KB

/* ========== DYNAMIC LIBRARIES (1MiB - 8MiB physical) ========== */

/** Dynamic library region start (physical) */
#define DYLIB_REGION_PHYS_START 0x00100000UL // 1MiB physical
#define DYLIB_REGION_VIRT_START                                                \
   (KERNEL_BASE + 0x00100000UL) // 3GB + 1MiB virtual

/** Dynamic library region end (before video buffer) */
#define DYLIB_REGION_PHYS_END 0x00800000UL                 // 8MiB physical
#define DYLIB_REGION_VIRT_END (KERNEL_BASE + 0x00800000UL) // 3GB + 8MiB virtual

/** Dynamic library region size */
#define DYLIB_REGION_SIZE (DYLIB_REGION_PHYS_END - DYLIB_REGION_PHYS_START)

/* ========== KERNEL HEAP (High kernel space, just below user space) ==========
 */

/** Kernel heap start (after identity-mapped kernel code) */
#define KERNEL_HEAP_START (KERNEL_BASE + 0x01000000UL) // 3GB + 16MiB

/** Kernel heap end (before user space boundary) */
#define KERNEL_HEAP_END (KERNEL_BASE + 0x3F000000UL) // Just below 4GB

/** Kernel heap size (approximately 1GB) */
#define KERNEL_HEAP_SIZE (KERNEL_HEAP_END - KERNEL_HEAP_START)

/* ========== USER SPACE (Low addresses, 0 - 3GB) ========== */

/** User space start */
#define USER_SPACE_START 0x00000000UL

/** User space end (before kernel space) */
#define USER_SPACE_END KERNEL_BASE // 3GB

/** User space size (3GB) */
#define USER_SPACE_SIZE USER_SPACE_END

/* ========== PER-PROCESS MEMORY REGIONS ========== */

/** Per-process user heap start (low in user space, typical malloc starts here)
 *  Allows user processes to grow heap upward from this address */
#define USER_HEAP_START 0x10000000UL // 256MiB

/** Per-process stack start (high in user space, grows downward)
 *  Each process has its own stack */
#define USER_STACK_START 0xBFFF0000UL // Just below kernel space, ~3GB - 64KB

/** Per-process stack size (default, can be adjusted per process) */
#define USER_STACK_SIZE 0x10000UL // 64KB

/** Per-process code/data region (typically loaded from 0x08048000 on x86 Linux
 * convention) */
#define USER_CODE_START 0x08048000UL // 128MiB + 16KB (standard x86 32-bit)

/* Pull in common memory constants (PAGE_SIZE, etc.) */
#include <mem/mm_kernel.h>

/* ========== PAGE ALIGNMENT ========== */

/** Page size for x86 (4KB) */
#define PAGE_SHIFT 12

/** Align address down to page boundary */
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

/** Align address up to page boundary */
#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

/** Check if address is page-aligned */
#define IS_PAGE_ALIGNED(addr) (((addr) & (PAGE_SIZE - 1)) == 0)

/* ========== MEMORY REGION CHECKS ========== */

/** Check if address is in kernel space */
static inline int is_kernel_address(uintptr_t addr)
{
   return addr >= KERNEL_BASE;
}

/** Check if address is in user space */
static inline int is_user_address(uintptr_t addr)
{
   return addr < KERNEL_BASE && addr >= USER_SPACE_START;
}

/** Check if address is in kernel heap */
static inline int is_kernel_heap_address(uintptr_t addr)
{
   return addr >= KERNEL_HEAP_START && addr < KERNEL_HEAP_END;
}

/** Check if address is in user heap region */
static inline int is_user_heap_address(uintptr_t addr)
{
   return addr >= USER_HEAP_START && addr < USER_STACK_START;
}

/** Check if address is in user stack region */
static inline int is_user_stack_address(uintptr_t addr)
{
   return addr >= USER_STACK_START && addr < USER_SPACE_END;
}

/** Check if address is in system reserved region */
static inline int is_system_reserved_address(uintptr_t addr)
{
   return addr >= SYSTEM_RESERVED_START && addr < SYSTEM_RESERVED_END;
}

/** Check if address is in video memory region */
static inline int is_video_memory_address(uintptr_t addr)
{
   return addr >= VIDEO_MEMORY_VIRT &&
          addr < (VIDEO_MEMORY_VIRT + VIDEO_BUFFER_SIZE);
}

/** Check if address is in dynamic library region */
static inline int is_dylib_region_address(uintptr_t addr)
{
   return addr >= DYLIB_REGION_VIRT_START && addr < DYLIB_REGION_VIRT_END;
}

#endif