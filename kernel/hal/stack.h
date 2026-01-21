// SPDX-License-Identifier: GPL-3.0-only

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

static inline void HAL_Stack_SetupProcess(Stack *stack, uint32_t entry_point)
{
   HAL_ARCH_Stack_SetupProcess(stack, entry_point);
}

static inline uint32_t HAL_Stack_GetESP(void)
{
   return HAL_ARCH_Stack_GetESP();
}

static inline uint32_t HAL_Stack_GetEBP(void)
{
   return HAL_ARCH_Stack_GetEBP();
}

static inline void HAL_Stack_SetRegisters(uint32_t esp, uint32_t ebp)
{
   HAL_ARCH_Stack_SetRegisters(esp, ebp);
}

static inline void HAL_Stack_GetRegisters(uint32_t *esp_out, uint32_t *ebp_out)
{
   HAL_ARCH_Stack_GetRegisters(esp_out, ebp_out);
}

static inline void HAL_Stack_SetupException(Stack *stack, uint32_t handler,
                                            uint32_t error_code)
{
   HAL_ARCH_Stack_SetupException(stack, handler, error_code);
}

static inline void HAL_Stack_InitializeKernel(void)
{
   HAL_ARCH_Stack_InitializeKernel();
}
#endif