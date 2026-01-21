// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_H
#define HAL_H

#include <stdint.h>

#if defined(I686)
#include <arch/i686/cpu/gdt.h>
#include <arch/i686/cpu/i8253.h>
#include <arch/i686/cpu/idt.h>
#include <arch/i686/cpu/irq.h>
#include <arch/i686/cpu/isr.h>

#include <arch/i686/drivers/ps2.h>
#include <arch/i686/syscall/syscall.h>
#else
#error "Unsupported architecture for HAL"
#endif

void HAL_Initialize();

#endif