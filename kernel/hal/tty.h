// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_TTY_H
#define HAL_TTY_H

#include <stdbool.h>
#include <stdint.h>

// Map architecture-specific primitives to generic names
#if defined(I686)
#include <arch/i686/drivers/tty.h>
#define HAL_ARCH_Tty_putc i686_tty_putc
#define HAL_ARCH_Tty_getc i686_tty_getc
#define HAL_ARCH_Tty_set_cursor i686_tty_set_cursor
#define HAL_ARCH_Tty_clear i686_tty_clear
#else
#error "Unsupported architecture for HAL TTY"
#endif

typedef struct HAL_TtyOperations
{
   void (*putc)(int x, int y, char c, uint8_t color);
   char (*getc)(int x, int y);
   void (*set_cursor)(int x, int y);
   void (*clear)(void);
} HAL_TtyOperations;

extern const HAL_TtyOperations *g_HalTtyOperations;

#endif
