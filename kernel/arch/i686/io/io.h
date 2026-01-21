// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_IO_H
#define I686_IO_H
#include <stdbool.h>
#include <stdint.h>

void __attribute__((cdecl)) i686_outb(uint16_t port, uint8_t value);
void __attribute__((cdecl)) i686_outw(uint16_t port, uint16_t value);
void __attribute__((cdecl)) i686_outl(uint16_t port, uint32_t value);
uint8_t __attribute__((cdecl)) i686_inb(uint16_t port);
uint16_t __attribute__((cdecl)) i686_inw(uint16_t port);
uint32_t __attribute__((cdecl)) i686_inl(uint16_t port);

uint8_t __attribute__((cdecl)) i686_EnableInterrupts();
uint8_t __attribute__((cdecl)) i686_DisableInterrupts();

void i686_iowait();
void __attribute__((cdecl)) i686_Panic();

void __attribute__((cdecl)) i686_Halt();

#endif