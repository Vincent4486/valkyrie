// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_PAGING_H
#define I686_PAGING_H

#include <mem/mm_kernel.h>
#include <stdbool.h>
#include <stdint.h>

// Page flag helpers
#define PAGE_PRESENT 0x001
#define PAGE_RW 0x002
#define PAGE_USER 0x004

// Page table initialization
void i686_Paging_Initialize(void);
void i686_Paging_Enable(void);

// Page table management
void *i686_Paging_CreatePageDirectory(void);
void i686_Paging_DestroyPageDirectory(void *page_dir);

// Page mapping
bool i686_Paging_MapPage(void *page_dir, uint32_t vaddr, uint32_t paddr,
                         uint32_t flags);
bool i686_Paging_UnmapPage(void *page_dir, uint32_t vaddr);

// Page lookup
uint32_t i686_Paging_GetPhysicalAddress(void *page_dir, uint32_t vaddr);
bool i686_Paging_IsPageMapped(void *page_dir, uint32_t vaddr);

// Page fault handling
void i686_Paging_PageFaultHandler(uint32_t fault_address, uint32_t error_code);

// TLB management
void i686_Paging_InvalidateTlbEntry(uint32_t vaddr);
void i686_Paging_FlushTlb(void);

// Process page directory switching
void i686_Paging_SwitchPageDirectory(void *page_dir);
void *i686_Paging_GetCurrentPageDirectory(void);

// Memory region allocation
void *i686_Paging_AllocateKernelPages(int page_count);
void i686_Paging_FreeKernelPages(void *addr, int page_count);

// Simple built-in self-test
void i686_Paging_SelfTest(void);

#endif