// SPDX-License-Identifier: GPL-3.0-only

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

static inline void HAL_outb(uint16_t port, uint8_t value)
{
   HAL_ARCH_outb(port, value);
}

static inline void HAL_outw(uint16_t port, uint16_t value)
{
   HAL_ARCH_outw(port, value);
}

static inline void HAL_outl(uint16_t port, uint32_t value)
{
   HAL_ARCH_outl(port, value);
}

static inline uint8_t HAL_inb(uint16_t port) { return HAL_ARCH_inb(port); }

static inline uint16_t HAL_inw(uint16_t port) { return HAL_ARCH_inw(port); }

static inline uint32_t HAL_inl(uint16_t port) { return HAL_ARCH_inl(port); }

static inline uint8_t HAL_EnableInterrupts()
{
   return HAL_ARCH_EnableInterrupts();
}

static inline uint8_t HAL_DisableInterrupts()
{
   return HAL_ARCH_DisableInterrupts();
}

static inline void HAL_IOWait() { HAL_ARCH_iowait(); }

static inline void HAL_Halt() { HAL_ARCH_Halt(); }

static inline void HAL_Panic() { HAL_ARCH_Panic(); }
#endif