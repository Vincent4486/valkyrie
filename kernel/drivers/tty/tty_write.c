// SPDX-License-Identifier: GPL-3.0-only

#include "tty.h"
#include <mem/mm_kernel.h>

/* External API: Pascal-case functions other files call */
int TTY_Write(TTY_Device *dev, const void *buf, uint32_t count)
{
   extern int tty_write_impl(TTY_Device *dev, const void *buf, uint32_t count);
   if (!dev || !buf || count == 0) return 0;
   return tty_write_impl(dev, buf, count);
}
