// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_TTY_H
#define HAL_TTY_H

#include <stdbool.h>
#include <stdint.h>

// Map architecture-specific primitives to generic names
#if defined(I686)
#include <arch/i686/drivers/tty.h>
#define HAL_ARCH_TTY_UPDATE_VGA i686_TTY_UpdateVga
#else
#error "Unsupported architecture for HAL TTY"
#endif

typedef struct HAL_TtyOperations
{
   void (*UpdateVga)(uint16_t *buff);
} HAL_TtyOperations;

extern const HAL_TtyOperations *g_HalTtyOperations;

#endif
