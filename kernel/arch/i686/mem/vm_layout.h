// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_VM_LAYOUT_H
#define I686_VM_LAYOUT_H

#include <stdint.h>
#include <mem/mm_kernel.h>

extern uint8_t __end;

/** Kernel base address - identity mapped at 3GB (0xC0000000) */
#define KERNEL_BASE 0xC0000000UL

/* ========== USER SPACE (Low addresses, 0 - 3GB) ========== */

/** User space end (before kernel space) */
#define USER_SPACE_END 0xC0000000UL // 3GB

/* ========== PER-PROCESS MEMORY REGIONS ========== */

/** Per-process user heap start (low in user space, typical malloc starts here)
 *  Allows user processes to grow heap upward from this address */
#define USER_HEAP_START 0x10000000UL // 256MiB

/** Per-process stack start (high in user space, grows downward)
 *  Each process has its own stack */
#define USER_STACK_START 0xBFFF0000UL // Just below kernel space, ~3GB - 64KB

/** Per-process stack top (initial ESP before first push) */
#define USER_STACK_TOP USER_STACK_START

/** Per-process stack size (default, can be adjusted per process) */
#define USER_STACK_SIZE 0x10000UL // 64KB

/** Per-process code/data region (typically loaded from 0x08048000 on x86 Linux
 * convention) */
#define USER_CODE_START 0x08048000UL // 128MiB + 16KB (standard x86 32-bit)

/* ========== MODULE LOADING CONSTANTS ========== */

/** User trampoline exit code location (exit trampoline for process termination)
 *  Must be in user-accessible but protected region */
#define USER_EXIT_TRAMPOLINE_VA (USER_STACK_START + PAGE_SIZE)

#endif