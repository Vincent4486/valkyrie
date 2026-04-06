// SPDX-License-Identifier: GPL-3.0-only

#include "ps2.h"
#include <arch/i686/cpu/irq.h>
#include <arch/i686/io/io.h>
#include <drivers/keyboard/keyboard.h>
#include <stdint.h>

/* PS/2 keyboard port */
#define PS2_DATA_PORT 0x60

/* Global counter for keypress events for debugging (incremented in IRQ). */
volatile uint32_t g_kb_count = 0;

/**
 * i686-specific IRQ handler for PS/2 keyboard (IRQ1)
 * Reads scancode from port 0x60 and processes it
 */
void ps2_keyboard_irq(Registers *regs)
{
   (void)regs;
   uint8_t scancode = i686_inb(PS2_DATA_PORT);
   g_kb_count++;

   /* Process scancode through generic keyboard handler. */
   Keyboard_HandleScancode(scancode);
}

/**
 * Register PS/2 keyboard handler for i686 IRQ1
 */
void i686_PS2_Initialize(void)
{
   i686_IRQ_RegisterHandler(1, ps2_keyboard_irq);
}

