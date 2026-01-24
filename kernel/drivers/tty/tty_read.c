// SPDX-License-Identifier: GPL-3.0-only

#include "tty.h"
#include <mem/mm_kernel.h>

/* External API: Pascal-case functions other files call */
int TTY_Read(TTY_Device *dev, void *buf, uint32_t count)
{
   extern int tty_read_impl(TTY_Device *dev, void *buf, uint32_t count);
   if (!dev || !buf || count == 0) return 0;
   return tty_read_impl(dev, buf, count);
}

int TTY_InputPush(char c)
{
   extern int tty_buffer_input_push(char c);
   return tty_buffer_input_push(c);
}
