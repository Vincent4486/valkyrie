// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_PAGING_H
#define HAL_PAGING_H
#include <stdbool.h>
#include <stdint.h>

/**
 * HAL Memory and Paging Abstraction
 *
 * This header provides architecture-independent access to:
 * 1. Memory layout constants via architecture-specific vm_layout.h
 * 2. Paging operations via architecture-specific paging.h
 *
 * Each architecture must provide:
 * - arch/<arch>/mem/vm_layout.h with memory constants
 * - arch/<arch>/mem/paging.h with paging functions
 */

#if defined(I686)
#include <arch/i686/mem/vm_layout.h>
#include <arch/i686/mem/paging.h>

/* Paging flags */
#define HAL_PAGE_PRESENT 0x001
#define HAL_PAGE_RW 0x002
#define HAL_PAGE_USER 0x004

/* Paging operations abstraction */
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

/* Memory layout constants */
#define HAL_ARCH_BASE KERNEL_BASE
#define HAL_ARCH_CODE_START USER_CODE_START
#define HAL_ARCH_HEAP_START USER_HEAP_START
#define HAL_ARCH_STACK_START USER_STACK_START
#define HAL_ARCH_STACK_SIZE USER_STACK_SIZE
#define HAL_ARCH_SPACE_END USER_SPACE_END
#define HAL_ARCH_HEAP_START KERNEL_HEAP_START
#define HAL_ARCH_HEAP_END KERNEL_HEAP_END
#define HAL_ARCH_PAGE_SIZE PAGE_SIZE

#else
#error "Unsupported architecture for HAL Paging and Memory"
#endif

#define HAL_PAGING_OK 0
#define HAL_PAGING_EINVAL (-1)
#define HAL_PAGING_EMAP (-2)

typedef struct HAL_PagingOperations
{
   void (*Initialize)(void);
   void (*Enable)(void);
   void *(*CreatePageDirectory)(void);
   void (*DestroyPageDirectory)(void *page_dir);
   int (*MapPage)(void *page_dir, uint32_t vaddr, uint32_t paddr,
                  uint32_t flags);
   int (*UnmapPage)(void *page_dir, uint32_t vaddr);
   uint32_t (*GetPhysicalAddress)(void *page_dir, uint32_t vaddr);
   int (*IsPageMapped)(void *page_dir, uint32_t vaddr);
   void (*PageFaultHandler)(uint32_t fault_address, uint32_t error_code);
   void (*InvalidateTlbEntry)(uint32_t vaddr);
   void (*FlushTlb)(void);
   void (*SwitchPageDirectory)(void *page_dir);
   void *(*GetCurrentPageDirectory)(void);
   void *(*AllocateKernelPages)(int page_count);
   void (*FreeKernelPages)(void *addr, int page_count);
   void (*SelfTest)(void);
} HAL_PagingOperations;

extern const HAL_PagingOperations *g_HalPagingOperations;

#endif