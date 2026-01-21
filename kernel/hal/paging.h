// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_PAGING_H
#define HAL_PAGING_H
#include <stdbool.h>
#include <stdint.h>
#if defined(I686)
#include <arch/i686/mem/paging.h>
#define HAL_PAGE_PRESENT 0x001
#define HAL_PAGE_RW 0x002
#define HAL_PAGE_USER 0x004

#define HAL_ARCH_Paging_Initialize i686_Paging_Initialize
#define HAL_ARCH_Paging_Enable i686_Paging_Enable
#define HAL_ARCH_Paging_CreatePageDirectory i686_Paging_CreatePageDirectory
#define HAL_ARCH_Paging_DestroyPageDirectory i686_Paging_DestroyPageDirectory
#define HAL_ARCH_Paging_MapPage i686_Paging_MapPage
#define HAL_ARCH_Paging_UnmapPage i686_Paging_UnmapPage
#define HAL_ARCH_Paging_GetPhysicalAddress i686_Paging_GetPhysicalAddress
#define HAL_ARCH_Paging_IsPageMapped i686_Paging_IsPageMapped
#define HAL_ARCH_Paging_PageFaultHandler i686_Paging_PageFaultHandler
#define HAL_ARCH_Paging_InvalidateTlbEntry i686_Paging_InvalidateTlbEntry
#define HAL_ARCH_Paging_FlushTlb i686_Paging_FlushTlb
#define HAL_ARCH_Paging_SwitchPageDirectory i686_Paging_SwitchPageDirectory
#define HAL_ARCH_Paging_GetCurrentPageDirectory                                \
   i686_Paging_GetCurrentPageDirectory
#define HAL_ARCH_Paging_AllocateKernelPages i686_Paging_AllocateKernelPages
#define HAL_ARCH_Paging_FreeKernelPages i686_Paging_FreeKernelPages
#define HAL_ARCH_Paging_SelfTest i686_Paging_SelfTest
#else
#error "Unsupported architecture for HAL IRQ"
#endif

static inline void HAL_Paging_Initialize(void) { HAL_ARCH_Paging_Initialize(); }

static inline void HAL_Paging_Enable(void) { HAL_ARCH_Paging_Enable(); }

static inline void *HAL_Paging_CreatePageDirectory(void)
{
   return HAL_ARCH_Paging_CreatePageDirectory();
}

static inline void HAL_Paging_DestroyPageDirectory(void *page_dir)
{
   HAL_ARCH_Paging_DestroyPageDirectory(page_dir);
}

static inline bool HAL_Paging_MapPage(void *page_dir, uint32_t vaddr,
                                      uint32_t paddr, uint32_t flags)
{
   return HAL_ARCH_Paging_MapPage(page_dir, vaddr, paddr, flags);
}

static inline bool HAL_Paging_UnmapPage(void *page_dir, uint32_t vaddr)
{
   return HAL_ARCH_Paging_UnmapPage(page_dir, vaddr);
}

static inline uint32_t HAL_Paging_GetPhysicalAddress(void *page_dir,
                                                     uint32_t vaddr)
{
   return HAL_ARCH_Paging_GetPhysicalAddress(page_dir, vaddr);
}

static inline bool HAL_Paging_IsPageMapped(void *page_dir, uint32_t vaddr)
{
   return HAL_ARCH_Paging_IsPageMapped(page_dir, vaddr);
}

static inline void HAL_Paging_PageFaultHandler(uint32_t fault_address,
                                               uint32_t error_code)
{
   HAL_ARCH_Paging_PageFaultHandler(fault_address, error_code);
}

static inline void HAL_Paging_InvalidateTlbEntry(uint32_t vaddr)
{
   HAL_ARCH_Paging_InvalidateTlbEntry(vaddr);
}

static inline void HAL_Paging_FlushTlb(void) { HAL_ARCH_Paging_FlushTlb(); }

static inline void HAL_Paging_SwitchPageDirectory(void *page_dir)
{
   HAL_ARCH_Paging_SwitchPageDirectory(page_dir);
}

static inline void *HAL_Paging_GetCurrentPageDirectory(void)
{
   return HAL_ARCH_Paging_GetCurrentPageDirectory();
}

static inline void *HAL_Paging_AllocateKernelPages(int page_count)
{
   return HAL_ARCH_Paging_AllocateKernelPages(page_count);
}

static inline void HAL_Paging_FreeKernelPages(void *addr, int page_count)
{
   HAL_ARCH_Paging_FreeKernelPages(addr, page_count);
}

static inline void HAL_Paging_SelfTest(void) { HAL_ARCH_Paging_SelfTest(); }

#endif