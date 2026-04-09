// SPDX-License-Identifier: GPL-3.0-only

#ifndef MEMORY_H
#define MEMORY_H

#include <cpu/process.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uintptr_t __stack_chk_guard;
void __stack_chk_fail_local(void);

/* Memory management information */
typedef struct
{
   uint32_t total_memory;      /* Total physical memory in bytes */
   uint32_t available_memory;  /* Available physical memory in bytes */
   uint32_t used_memory;       /* Used physical memory in bytes */
   uint32_t heap_start;        /* Kernel heap start address */
   uint32_t heap_end;          /* Kernel heap end address */
   uint32_t heap_size;         /* Total heap size in bytes */
   uint32_t page_size;         /* Memory page size (typically 4096) */
   uint32_t kernel_start;      /* Kernel memory start */
   uint32_t kernel_end;        /* Kernel memory end */
   uint32_t user_start;        /* User space start */
   uint32_t user_end;          /* User space end */
   uint32_t kernel_stack_size; /* Kernel stack size in bytes */
} MEM_Info;

/* Basic memory helpers (sizes in bytes) */
void *memcpy(void *dst, const void *src, size_t num);
void *memset(void *ptr, int value, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);
void *memmove(void *dest, const void *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);

void *SegmentOffsetToLinear(void *addr);

void MEM_Initialize(void); /* Reads boot params from g_SysInfo->boot */

/* Heap / allocator initialization */
void Heap_Initialize(void);

/* Core allocators */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
uintptr_t mem_heap_start(void);
uintptr_t mem_heap_end(void);

/* libc-like API backed by the kernel allocator */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* brk/sbrk style heap control */
int brk(void *addr);      /* returns 0 on success, -1 on failure */
void *sbrk(intptr_t inc); /* returns previous break or (void*)-1 on failure */

int PMM_IsInitialized(void);

#define PMM_OK 0
#define PMM_EINVAL (-1)
/* Self-test helper */
void Heap_SelfTest(void);

/* Physical Memory Manager (PMM) - Kernel level
 *
 * Tracks allocation of physical page frames.
 * Works with paging.c (which handles page table manipulation).
 */

/* Initialize PMM with available physical memory
 * Call before any PMM_AllocatePhysicalPage() calls
 * Accepts memory ranges from multiboot info or hardcoded.
 */
void PMM_Initialize(uint32_t total_mem_bytes);

/* Allocate a single 4K physical page frame
 * Returns physical address, or 0 on failure
 */
uint32_t PMM_AllocatePhysicalPage(void);

/* Free a previously allocated physical page
 * addr should be page-aligned (4K)
 */
void PMM_FreePhysicalPage(uint32_t addr);

/* Check if a physical page is free
 */
int PMM_IsPhysicalPageFree(uint32_t addr);

/* Get total physical memory tracked
 */
uint32_t PMM_TotalMemory(void);

/* Get number of free physical pages
 */
uint32_t PMM_FreePages(void);

/* Get number of allocated physical pages
 */
uint32_t PMM_AllocatedPages(void);

/* Self-test helper
 */
void PMM_SelfTest(void);

/* Virtual Memory Manager (VMM) - Kernel level
 *
 * High-level virtual memory operations.
 * Built on top of paging.c (page tables) and pmm.c (physical frames).
 */

/* Initialize VMM
 * Should be called after HAL_Paging_Initialize() and PMM_Initialize()
 */
void VMM_Initialize(void);

#define VMM_OK 0
#define VMM_EINVAL (-1)
#define VMM_EMAP (-2)

/* Kernel convenience wrappers for VMM operations
 */
void *VMM_Allocate(uint32_t size, uint32_t flags);
void VMM_Free(void *vaddr, uint32_t size);
int VMM_Map(uint32_t vaddr, uint32_t paddr, uint32_t size, uint32_t flags);
int VMM_Unmap(uint32_t vaddr, uint32_t size);
uint32_t VMM_GetPhys(uint32_t vaddr);
void *VMM_GetPageDirectory(void);

/* Flags for mapping (use with PAGE_* from paging.h)
 */
#define VMM_RW 0x002         // Read/Write
#define VMM_USER 0x004       // User-accessible
#define VMM_DEFAULT (VMM_RW) // Kernel R/W only

/* Self-test helper
 */
void VMM_SelfTest(void);

/* Stack Management - Kernel level */

/**
 * Stack information for a process or kernel context
 */
typedef struct
{
   uint32_t base;    // Base address (top of stack, high address on x86)
   uint32_t size;    // Stack size in bytes
   uint32_t current; // Current stack pointer
   uint8_t *data;    // Allocated stack memory
} Stack;

/**
 * Initialize stack subsystem for the OS
 * Sets up kernel stack infrastructure.
 * Called during kernel initialization (before creating processes).
 */
void Stack_Initialize(void);

/**
 * Initialize kernel stack
 * Sets up the kernel stack at a fixed location in kernel memory.
 * Called during kernel initialization.
 */
void Stack_InitializeKernel(void);

/**
 * Get kernel stack
 * @return Pointer to kernel stack structure
 */
Stack *Stack_GetKernel(void);

/**
 * Platform-specific: Get current ESP register
 */
uint32_t Stack_GetESP(void);

/**
 * Platform-specific: Get current EBP register
 */
uint32_t Stack_GetEBP(void);

/**
 * Platform-specific: Set ESP and EBP registers
 */
void Stack_SetRegisters(uint32_t esp, uint32_t ebp);

/**
 * Stack subsystem self-test
 * @return 1 on success, 0 on failure
 */
int Stack_SelfTest(void);

/* Architecture page size (4 KiB) */
#define PAGE_SIZE 0x1000u

// 0x00000000 - 0x000003FF - interrupt vector table
// 0x00000400 - 0x000004FF - BIOS data area

// 0x00030000 - 0x00080000 - free

// 0x00080000 - 0x0009FFFF - Extended BIOS data area
// 0x000A0000 - 0x000C7FFF - Video
// 0x000C8000 - 0x000FFFFF - BIOS

#define MEMORY_KERNEL_ADDR ((void *)0x00A00000)

/* ==========================================================================
 * Canonical 1 KiB-aligned kernel memory map
 *
 * Every region start/end must satisfy: address % K_MEM_BLOCK_SIZE == 0.
 * All fixed addresses are centralised here; no raw literals elsewhere.
 *
 * Reserved region: 0x00100000 – 0x00A00000
 *
 *  0x00100000 – 0x00800000  Dylib space          (7 MiB)
 *  0x00930000               g_SysInfo (SYS_Info)
 *  0x00931000               s_bootInfo (BOOT_Info)
 *  0x00A00000               Kernel load address
 * ========================================================================== */

#define K_MEM_BLOCK_SIZE 1024 /* 0x400 */

#define K_MEM_DYLIB_START (0x400u * K_MEM_BLOCK_SIZE) /* 0x00100000 */
#define K_MEM_DYLIB_END (0x2000u * K_MEM_BLOCK_SIZE)  /* 0x00800000 */

#define K_MEM_SYS_INFO_START (0x24C0u * K_MEM_BLOCK_SIZE) /* 0x00930000 */

#define K_MEM_BOOT_INFO_START                                                  \
   (K_MEM_SYS_INFO_START + 4 * K_MEM_BLOCK_SIZE) /* 0x00931000 */

/* VGA text-mode VRAM window – always mapped at this physical address.
 * Must not be placed inside the kernel-reserved range (0x00100000+).     */
#define K_MEM_VIDEO_START 0x000B8000u /* VGA text VRAM base (colour modes) */

/* Library registry placed in low memory (inside FAT area). Stage2 populates
 * this with loaded modules so the kernel can find them. */
#define LIB_NAME_MAX 32
typedef struct
{
   char name[LIB_NAME_MAX];
   void *base;
   void *entry;
   uint32_t size;
} LibRecord;

#define LIB_REGISTRY_ADDR ((LibRecord *)0x00028000)
#define LIB_REGISTRY_MAX 16

#endif