// SPDX-License-Identifier: GPL-3.0-only

/* Simple tty device glue exposing a VFS-friendly interface could be added
 * later. For now provide a small driver wrapper that registers the backend
 * and offers init/read/write functions used by higher layers.
 */

#include "tty.h"
#include <mem/mm_kernel.h>
#include <std/stdio.h>

/* Forward declarations of internal buffer functions implemented in
 * tty_buffer.c (these have been renamed to avoid symbol conflicts).
 */
bool tty_buffer_init(void);
TTY_Device *tty_buffer_get_device(void);
int tty_buffer_write(TTY_Device *d, const void *buf, uint32_t count);
int tty_buffer_read(TTY_Device *d, void *buf, uint32_t count);
void tty_buffer_set_color(TTY_Device *d, uint8_t color);
void tty_buffer_set_cursor(TTY_Device *d, int x, int y);
void tty_buffer_get_cursor(TTY_Device *d, int *x, int *y);
void tty_buffer_clear(TTY_Device *d);
void tty_buffer_flush(TTY_Device *d);

bool TTY_Initialize(void)
{
   return tty_buffer_init();
}

TTY_Device *TTY_GetDevice(void)
{
   return tty_buffer_get_device();
}

int tty_write_impl(TTY_Device *dev, const void *buf, uint32_t count)
{
   return tty_buffer_write(dev, buf, count);
}

/* Stream-aware wrapper */
int tty_write_stream_impl(TTY_Device *dev, int stream, const void *buf, uint32_t count)
{
   extern int tty_buffer_write_stream(TTY_Device *d, int stream, const void *buf, uint32_t count);
   return tty_buffer_write_stream(dev, stream, buf, count);
}

int tty_read_impl(TTY_Device *dev, void *buf, uint32_t count)
{
   return tty_buffer_read(dev, buf, count);
}

void TTY_SetColor(TTY_Device *dev, uint8_t color) { tty_buffer_set_color(dev, color); }
void TTY_SetCursor(TTY_Device *dev, int x, int y) { tty_buffer_set_cursor(dev, x, y); }
void TTY_GetCursor(TTY_Device *dev, int *x, int *y) { if (dev) tty_buffer_get_cursor(dev, x, y); if (x && *x < 0) *x = 0; if (y && *y < 0) *y = 0; }
void TTY_Clear(TTY_Device *dev) { tty_buffer_clear(dev); }

void TTY_Flush(TTY_Device *dev) { if (dev) tty_buffer_flush(dev); }

int TTY_WriteStream(TTY_Device *dev, int stream, const void *buf, uint32_t count) { return tty_write_stream_impl(dev, stream, buf, count); }
