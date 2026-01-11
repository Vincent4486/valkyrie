// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef HAL_STACK_H
#define HAL_STACK_H
#include <mem/mm_kernel.h>
#include <stdint.h>
#if defined(I686)
#include <arch/i686/mem/stack.h>
#define HAL_ARCH_Stack_SetupProcess i686_Stack_SetupProcess
#define HAL_ARCH_Stack_GetESP i686_Stack_GetESP
#define HAL_ARCH_Stack_GetEBP i686_Stack_GetEBP
#define HAL_ARCH_Stack_SetRegisters i686_Stack_SetRegisters
#define HAL_ARCH_Stack_GetRegisters i686_Stack_GetRegisters
#define HAL_ARCH_Stack_SetupException i686_Stack_SetupException
#define HAL_ARCH_Stack_InitializeKernel i686_Stack_InitializeKernel
#else
#error "Unsupported architecture for HAL Stack"
#endif

typedef struct HAL_StackOperations
{
   void (*SetupProcess)(Stack *stack, uint32_t entry_point);
   uint32_t (*GetESP)(void);
   uint32_t (*GetEBP)(void);
   void (*SetRegisters)(uint32_t esp, uint32_t ebp);
   void (*GetRegisters)(uint32_t *esp_out, uint32_t *ebp_out);
   void (*SetupException)(Stack *stack, uint32_t handler, uint32_t error_code);
   void (*InitializeKernel)(void);
} HAL_StackOperations;

extern const HAL_StackOperations *g_HalStackOperations;
#endif