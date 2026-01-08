// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef I686_IRQ_H
#define I686_IRQ_H

#include "isr.h"
#include <stdint.h>

typedef void (*IRQHandler)(Registers *regs);

/* Interrupt/IRQ information */
typedef struct
{
   uint8_t irq_count;   /* Number of IRQ lines */
   uint8_t pic_type;    /* PIC type (8259, APIC, etc) */
   uint32_t timer_freq; /* Timer frequency in Hz */
} IRQ_Info;

void i686_IRQ_Initialize();
void i686_IRQ_RegisterHandler(int irq, IRQHandler handler);
void i686_IRQ_Unmask(int irq);

#endif