// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ps2.h"
#include <arch/i686/cpu/irq.h>
#include <arch/i686/io/io.h>
#include <display/keyboard.h>
#include <stdint.h>

/* PS/2 keyboard port */
#define PS2_DATA_PORT 0x60

/* Input line buffer for simple line editing */
#define KB_LINE_BUF 256
static char kb_line[KB_LINE_BUF];
static int kb_len = 0;
static int kb_ready = 0; /* 1 when a full line (\n) is available */

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

   /* Process scancode through generic keyboard handler
      which manages buffering and line editing */
   Keyboard_HandleScancode(scancode);
}

/**
 * Register PS/2 keyboard handler for i686 IRQ1
 */
void i686_PS2_Initialize(void)
{
   i686_IRQ_RegisterHandler(1, ps2_keyboard_irq);
}

/**
 * Non-blocking readline: returns number of bytes written into buf, 0 if no line
 * ready. This is the i686-specific wrapper.
 */
int i686_PS2_ReadLineNb(char *buf, int bufsize)
{
   return Keyboard_ReadlineNb(buf, bufsize);
}

/**
 * Blocking readline for i686 with platform-specific idle (HLT instruction)
 */
int i686_PS2_ReadLine(char *buf, int bufsize)
{
   int n;
   while ((n = i686_PS2_ReadLineNb(buf, bufsize)) == 0)
   {
      /* i686-specific idle: execute HLT to reduce busy spin and wait for
       * interrupts */
      __asm__ volatile("sti; hlt; cli");
   }
   return n;
}
