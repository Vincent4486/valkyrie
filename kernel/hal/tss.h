// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_TSS_H
#define HAL_TSS_H

#include <stdint.h>

#if defined(I686)
#include <arch/i686/cpu/tss.h>
#define HAL_ARCH_TSS_Initialize i686_TSS_Initialize
#define HAL_ARCH_TSS_SetKernelStack i686_TSS_SetKernelStack
#define HAL_ARCH_TSS_GetKernelStack i686_TSS_GetKernelStack
#else
#error "Unsupported architecture for HAL TSS"
#endif

typedef struct HAL_TssOperations
{
   void (*Initialize)(void);
   void (*SetKernelStack)(uint32_t esp0);
   uint32_t (*GetKernelStack)(void);
} HAL_TssOperations;

extern const HAL_TssOperations *g_HalTssOperations;

#endif