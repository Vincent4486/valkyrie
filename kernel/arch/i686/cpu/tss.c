// SPDX-License-Identifier: GPL-3.0-only

#include "tss.h"
#include "gdt.h"
#include <mem/mm_kernel.h>
#include <std/string.h>

typedef struct
{
   uint32_t prev_tss;
   uint32_t esp0;
   uint32_t ss0;
   uint32_t esp1;
   uint32_t ss1;
   uint32_t esp2;
   uint32_t ss2;
   uint32_t cr3;
   uint32_t eip;
   uint32_t eflags;
   uint32_t eax;
   uint32_t ecx;
   uint32_t edx;
   uint32_t ebx;
   uint32_t esp;
   uint32_t ebp;
   uint32_t esi;
   uint32_t edi;
   uint32_t es;
   uint32_t cs;
   uint32_t ss;
   uint32_t ds;
   uint32_t fs;
   uint32_t gs;
   uint32_t ldt;
   uint16_t trap;
   uint16_t iomap_base;
} __attribute__((packed)) i686_TSS;

static i686_TSS g_Tss;

void i686_TSS_Initialize(void)
{
   memset(&g_Tss, 0, sizeof(g_Tss));
   g_Tss.ss0 = i686_GDT_DATA_SEGMENT;
   g_Tss.iomap_base = sizeof(g_Tss);

   Stack *kernel_stack = Stack_GetKernel();
   g_Tss.esp0 = kernel_stack ? kernel_stack->base : 0;

   i686_GDT_SetTSSEntry((uint32_t)&g_Tss, sizeof(g_Tss) - 1);

   uint16_t selector = i686_GDT_TSS_SEGMENT;
   __asm__ __volatile__("ltr %0" : : "r"(selector));
}

void i686_TSS_SetKernelStack(uint32_t esp0) { g_Tss.esp0 = esp0; }

uint32_t i686_TSS_GetKernelStack(void) { return g_Tss.esp0; }