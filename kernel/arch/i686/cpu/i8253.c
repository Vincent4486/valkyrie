// SPDX-License-Identifier: GPL-3.0-only

#include "i8253.h"
#include <arch/i686/io/io.h>
#include <sys/sys.h>

volatile uint64_t system_ticks = 0;

void i686_i8253_SetFrequency(uint32_t freq)
{
   uint32_t reload = PIT_FREQ / freq;
   if (reload > 0xFFFF) reload = 0xFFFF;
   if (reload == 0) reload = 1;

   // Command: channel 0, LSB then MSB, mode 3 (square wave), binary
   i686_outb(PIT_COMMAND, PIT_CH0 | PIT_LSB_MSB | PIT_MODE3 | PIT_BINARY);

   // Write reload value
   i686_outb(PIT_CH0_DATA, reload & 0xFF);        // LSB
   i686_outb(PIT_CH0_DATA, (reload >> 8) & 0xFF); // MSB
}

void i686_i8253_Initialize(uint32_t frequency)
{
   i686_i8253_SetFrequency(frequency);
}

void i686_i8253_TimerHandler(Registers *regs) { system_ticks++; }
