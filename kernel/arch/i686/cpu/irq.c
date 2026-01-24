// SPDX-License-Identifier: GPL-3.0-only

#include "irq.h"
#include "i8259.h"
#include "pic.h"
#include <arch/i686/io/io.h>
#include <display/keyboard.h>
#include <std/arrays.h>
#include <std/stdio.h>
#include <stddef.h>
#include <sys/sys.h>

#define PIC_REMAP_OFFSET 0x20

IRQHandler g_IRQHandlers[16];
static const PICDriver *g_Driver = NULL;

void i686_IRQ_Handler(Registers *regs)
{
   int irq = regs->interrupt - PIC_REMAP_OFFSET;

   // Bounds check to prevent array out-of-bounds access
   if (irq < 0 || irq >= 16)
   {
      printf("IRQ out of bounds: interrupt=%d, irq=%d\n", regs->interrupt, irq);
      return;
   }

   if (g_IRQHandlers[irq] != NULL)
   {
      // handle IRQ
      g_IRQHandlers[irq](regs);
   }
   else
   {
      printf("Unhandled IRQ %d...\n", irq);
   }

   // send EOI
   g_Driver->SendEndOfInterrupt(irq);
}

void i686_IRQ_Initialize()
{
   printf("[IRQ] initialized\n");
   const PICDriver *drivers[] = {
       i8259_GetDriver(),
   };

   for (int i = 0; i < SIZE(drivers); i++)
   {
      if (drivers[i]->Probe())
      {
         g_Driver = drivers[i];
      }
   }

   if (g_Driver == NULL)
   {
      printf("Warning: No PIC found!\n");
      return;
   }

   printf("[IRQ] Found %s.\n", g_Driver->Name);
   g_Driver->Initialize(PIC_REMAP_OFFSET, PIC_REMAP_OFFSET + 8, false);

   // register ISR handlers for each of the 16 irq lines
   for (int i = 0; i < 16; i++)
      i686_ISR_RegisterHandler(PIC_REMAP_OFFSET + i, i686_IRQ_Handler);

   // enable interrupts
   i686_EnableInterrupts();

   g_Driver->Unmask(0);
   g_Driver->Unmask(1);

   /* Populate IRQ info in SYS_Info */
   g_SysInfo->irq.irq_count = 16;
   g_SysInfo->irq.pic_type = 1;      /* 8259 PIC */
   g_SysInfo->irq.timer_freq = 1000; /* 1000 Hz timer */
}

void i686_IRQ_RegisterHandler(int irq, IRQHandler handler)
{
   if (irq < 0 || irq >= 16)
   {
      printf("i686_IRQ_RegisterHandler: invalid IRQ %d\n", irq);
      return;
   }
   g_IRQHandlers[irq] = handler;
}

void i686_IRQ_Unmask(int irq)
{
   if (irq < 0 || irq >= 16)
   {
      printf("i686_IRQ_Unmask: invalid IRQ %d\n", irq);
      return;
   }
   if (g_Driver != NULL)
   {
      g_Driver->Unmask(irq);
   }
}
