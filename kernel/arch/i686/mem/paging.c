// SPDX-License-Identifier: AGPL-3.0-or-later

#include "paging.h"
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uint8_t __end; // provided by linker, end of kernel image

#define PAGE_TABLE_ENTRIES 1024
#define PAGE_DIR_ENTRIES 1024

// Identity-map a low window so existing kernel code keeps working.
#define IDENTITY_MAP_LIMIT (64 * 1024 * 1024u) // 64 MiB

static uint32_t *kernel_page_directory = NULL;
static uint32_t *current_page_directory = NULL;
static uintptr_t phys_alloc_ptr = 0;

static inline uintptr_t align_up(uintptr_t v, size_t a)
{
   return (v + (a - 1)) & ~(uintptr_t)(a - 1);
}

static inline void load_cr3(uint32_t phys_addr)
{
   __asm__ __volatile__("mov %0, %%cr3" ::"r"(phys_addr) : "memory");
}

static inline void enable_paging_hw(void)
{
   uint32_t cr0;
   __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
   cr0 |= 0x80000000u; // set PG bit
   __asm__ __volatile__("mov %0, %%cr0" ::"r"(cr0) : "memory");
}

static inline void invlpg(uint32_t addr)
{
   __asm__ __volatile__("invlpg (%0)" ::"r"(addr) : "memory");
}

static inline uint32_t read_cr3(void)
{
   uint32_t val;
   __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
   return val;
}

// Very small physical page allocator backed by the kernel's BSS end.
// This is sufficient for testing and bootstrap; replace with a real PMM later.
static uint32_t alloc_frame(void)
{
   if (!phys_alloc_ptr)
   {
      phys_alloc_ptr = align_up((uintptr_t)&__end, PAGE_SIZE);
   }
   uint32_t frame = (uint32_t)phys_alloc_ptr;
   phys_alloc_ptr += PAGE_SIZE;
   return frame;
}

static uint32_t *alloc_page_table(void)
{
   uint32_t phys = alloc_frame();
   uint32_t *tbl = (uint32_t *)phys; // identity mapped
   memset(tbl, 0, PAGE_SIZE);
   return tbl;
}

static uint32_t *alloc_page_directory(void)
{
   uint32_t phys = alloc_frame();
   uint32_t *pd = (uint32_t *)phys; // identity mapped
   memset(pd, 0, PAGE_SIZE);
   return pd;
}

static void identity_map_range(uint32_t *pd, uint32_t start, uint32_t end)
{
   for (uint32_t addr = start; addr < end; addr += PAGE_SIZE)
   {
      uint32_t pd_idx = addr >> 22;           // top 10 bits
      uint32_t pt_idx = (addr >> 12) & 0x3FF; // middle 10 bits

      if (!(pd[pd_idx] & PAGE_PRESENT))
      {
         uint32_t *pt = alloc_page_table();
         pd[pd_idx] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
      }

      uint32_t *pt = (uint32_t *)(pd[pd_idx] & 0xFFFFF000u);
      pt[pt_idx] = (addr & 0xFFFFF000u) | PAGE_PRESENT | PAGE_RW;
   }
}

void i686_Paging_Initialize(void)
{
   // Bootstrap identity-mapped kernel directory
   kernel_page_directory = alloc_page_directory();
   identity_map_range(kernel_page_directory, 0, IDENTITY_MAP_LIMIT);

   // Set current directory and enable
   current_page_directory = kernel_page_directory;
   load_cr3((uint32_t)kernel_page_directory);
   i686_Paging_Enable();
}

void i686_Paging_Enable(void) { enable_paging_hw(); }

void *i686_Paging_CreatePageDirectory(void)
{
   uint32_t *pd = alloc_page_directory();
   // Copy kernel mappings so shared kernel space stays accessible
   for (size_t i = 0; i < PAGE_DIR_ENTRIES; ++i)
   {
      pd[i] = kernel_page_directory[i];
   }
   return pd;
}

void i686_Paging_DestroyPageDirectory(void *page_dir)
{
   // No-op for now; simple allocator cannot free.
   (void)page_dir;
}

static uint32_t *get_page_table(uint32_t *pd, uint32_t vaddr, bool create)
{
   uint32_t pd_idx = vaddr >> 22;
   uint32_t pde = pd[pd_idx];
   if (!(pde & PAGE_PRESENT))
   {
      if (!create) return NULL;
      uint32_t *pt = alloc_page_table();
      pd[pd_idx] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
      return pt;
   }
   return (uint32_t *)(pde & 0xFFFFF000u);
}

bool i686_Paging_MapPage(void *page_dir, uint32_t vaddr, uint32_t paddr,
                         uint32_t flags)
{
   uint32_t *pd = (uint32_t *)page_dir;
   uint32_t *pt = get_page_table(pd, vaddr, true);
   if (!pt) return false;

   uint32_t pt_idx = (vaddr >> 12) & 0x3FF;
   pt[pt_idx] = (paddr & 0xFFFFF000u) | (flags & 0xFFF) | PAGE_PRESENT;
   invlpg(vaddr);
   return true;
}

bool i686_Paging_UnmapPage(void *page_dir, uint32_t vaddr)
{
   uint32_t *pd = (uint32_t *)page_dir;
   uint32_t *pt = get_page_table(pd, vaddr, false);
   if (!pt) return false;
   uint32_t pt_idx = (vaddr >> 12) & 0x3FF;
   pt[pt_idx] = 0;
   invlpg(vaddr);
   return true;
}

uint32_t i686_Paging_GetPhysicalAddress(void *page_dir, uint32_t vaddr)
{
   uint32_t *pd = (uint32_t *)page_dir;
   uint32_t *pt = get_page_table(pd, vaddr, false);
   if (!pt) return 0;
   uint32_t pt_idx = (vaddr >> 12) & 0x3FF;
   uint32_t pte = pt[pt_idx];
   if (!(pte & PAGE_PRESENT)) return 0;
   return (pte & 0xFFFFF000u) | (vaddr & 0xFFF);
}

bool i686_Paging_IsPageMapped(void *page_dir, uint32_t vaddr)
{
   return i686_Paging_GetPhysicalAddress(page_dir, vaddr) != 0;
}

void i686_Paging_PageFaultHandler(uint32_t fault_address, uint32_t error_code)
{
   printf("Page fault at 0x%08x, error=0x%x\n", fault_address, error_code);
   printf("  present=%d rw=%d user=%d reserved=%d fetch=%d\n",
          (error_code & 1) != 0, (error_code & 2) != 0, (error_code & 4) != 0,
          (error_code & 8) != 0, (error_code & 16) != 0);
   // In a real kernel, handle or panic. For now, halt.
   for (;;) __asm__ __volatile__("hlt");
}

void i686_Paging_InvalidateTlbEntry(uint32_t vaddr) { invlpg(vaddr); }

void i686_Paging_FlushTlb(void) { load_cr3(read_cr3()); }

void i686_Paging_SwitchPageDirectory(void *page_dir)
{
   current_page_directory = (uint32_t *)page_dir;
   load_cr3((uint32_t)page_dir);
}

void *i686_Paging_GetCurrentPageDirectory(void)
{
   return current_page_directory;
}

void *i686_Paging_AllocateKernelPages(int page_count)
{
   if (page_count <= 0) return NULL;
   uint32_t first_phys = 0;
   for (int i = 0; i < page_count; ++i)
   {
      uint32_t phys = alloc_frame();
      if (i == 0) first_phys = phys;
      // identity map each page to keep it accessible
      i686_Paging_MapPage(kernel_page_directory, phys, phys,
                          PAGE_RW | PAGE_PRESENT);
   }
   return (void *)first_phys;
}

void i686_Paging_FreeKernelPages(void *addr, int page_count)
{
   // Not implemented for this simple allocator
   (void)addr;
   (void)page_count;
}

void i686_Paging_SelfTest(void)
{
   const uint32_t test_va = 0x40000000u; // 1 GiB virtual address
   uint32_t *pd = (uint32_t *)i686_Paging_GetCurrentPageDirectory();
   void *phys_page = i686_Paging_AllocateKernelPages(1);
   if (!phys_page)
   {
      printf("[paging] self-test: failed to alloc frame\n");
      return;
   }

   if (!i686_Paging_MapPage(pd, test_va, (uint32_t)phys_page,
                            PAGE_RW | PAGE_PRESENT))
   {
      printf("[paging] self-test: map failed\n");
      return;
   }

   volatile uint32_t *v = (volatile uint32_t *)test_va;
   *v = 0x12345678u;
   uint32_t val = *v;

   if (val == 0x12345678u)
   {
      printf("[paging] self-test: PASS (wrote/read 0x%08x)\n", val);
   }
   else
   {
      printf("[paging] self-test: FAIL (got 0x%08x)\n", val);
   }

   // Unmap to confirm no crash; ignore result
   i686_Paging_UnmapPage(pd, test_va);
}