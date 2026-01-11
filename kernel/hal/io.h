// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef HAL_IO_H
#define HAL_IO_H

#include <stdbool.h>
#include <stdint.h>

// Map architecture-specific primitives to generic names
#if defined(I686)
#include <arch/i686/io/io.h>
#define HAL_ARCH_outb i686_outb
#define HAL_ARCH_outw i686_outw
#define HAL_ARCH_outl i686_outl
#define HAL_ARCH_inb i686_inb
#define HAL_ARCH_inw i686_inw
#define HAL_ARCH_inl i686_inl
#define HAL_ARCH_EnableInterrupts i686_EnableInterrupts
#define HAL_ARCH_DisableInterrupts i686_DisableInterrupts
#define HAL_ARCH_iowait i686_iowait
#define HAL_ARCH_Halt i686_Halt
#define HAL_ARCH_Panic i686_Panic
#else
#error "Unsupported architecture for HAL I/O"
#endif

typedef struct HAL_IoOperations
{
   void (*outb)(uint16_t port, uint8_t value);
   void (*outw)(uint16_t port, uint16_t value);
   void (*outl)(uint16_t port, uint32_t value);
   uint8_t (*inb)(uint16_t port);
   uint16_t (*inw)(uint16_t port);
   uint32_t (*inl)(uint16_t port);
   uint8_t (*EnableInterrupts)();
   uint8_t (*DisableInterrupts)();
   void (*iowait)();
   void (*Halt)();
   void (*Panic)();
} HAL_IoOperations;

extern const HAL_IoOperations *g_HalIoOperations;

#endif