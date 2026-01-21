// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include <stdint.h>

#if defined(I686)
#include <arch/i686/cpu/irq.h>
typedef IRQHandler HAL_IRQHandler;
typedef IRQ_Info HAL_IRQ_Info;
#define HAL_ARCH_IRQ_RegisterHandler i686_IRQ_RegisterHandler
#define HAL_ARCH_IRQ_Unmask i686_IRQ_Unmask
#else
#error "Unsupported architecture for HAL IRQ"
#endif

typedef struct HAL_IrqOperations
{
   void (*RegisterHandler)(int irq, void (*handler)(Registers *));
   void (*Unmask)(int irq);
} HAL_IrqOperations;

extern const HAL_IrqOperations *g_HalIrqOperations;

#endif