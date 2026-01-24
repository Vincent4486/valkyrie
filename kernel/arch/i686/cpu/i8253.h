// SPDX-License-Identifier: GPL-3.0-only

#ifndef I8253_H
#define I8253_H

#include <arch/i686/cpu/isr.h>
#include <stdint.h>

// PIT ports
#define PIT_CH0_DATA 0x40
#define PIT_CH1_DATA 0x41
#define PIT_CH2_DATA 0x42
#define PIT_COMMAND 0x43

// PIT command bits
#define PIT_CH0 0x00
#define PIT_CH1 0x40
#define PIT_CH2 0x80
#define PIT_READBACK 0xC0

#define PIT_MODE0 0x00 // Interrupt on terminal count
#define PIT_MODE1 0x02 // Hardware retriggerable one-shot
#define PIT_MODE2 0x04 // Rate generator
#define PIT_MODE3 0x06 // Square wave generator
#define PIT_MODE4 0x08 // Software triggered strobe
#define PIT_MODE5 0x0A // Hardware triggered strobe

#define PIT_BINARY 0x00
#define PIT_BCD 0x01

#define PIT_LATCH 0x00
#define PIT_LSB 0x10
#define PIT_MSB 0x20
#define PIT_LSB_MSB 0x30

// PIT input frequency
#define PIT_FREQ 1193182

// Global tick counter
extern volatile uint64_t system_ticks;

// Functions
void i686_i8253_Initialize(uint32_t frequency);
void i686_i8253_SetFrequency(uint32_t freq);
void i686_i8253_TimerHandler(Registers *regs);

#endif
