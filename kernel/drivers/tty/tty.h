// SPDX-License-Identifier: GPL-3.0-only

#ifndef TTY_H
#define TTY_H

#include <stdbool.h>
#include <stdint.h>

/* Simple TTY device interface for VFS/driver use. */
typedef struct TTY_Device TTY_Device;

/* Create/init the global tty device backend */
bool TTY_Initialize(void);

/* Get singleton device handle */
TTY_Device *TTY_GetDevice(void);

/* Device operations */
int TTY_Read(TTY_Device *dev, void *buf, uint32_t count);
int TTY_Write(TTY_Device *dev, const void *buf, uint32_t count);

/* Stream-aware write: stream 0=stdin, 1=stdout, 2=stderr */
#define TTY_STREAM_STDIN  0
#define TTY_STREAM_STDOUT 1
#define TTY_STREAM_STDERR 2
int TTY_WriteStream(TTY_Device *dev, int stream, const void *buf, uint32_t count);

int TTY_InputPush(char c);
void TTY_SetColor(TTY_Device *dev, uint8_t color);
void TTY_SetCursor(TTY_Device *dev, int x, int y);
void TTY_GetCursor(TTY_Device *dev, int *x, int *y);
void TTY_Clear(TTY_Device *dev);
/* Flush any buffered TTY content to the display */
void TTY_Flush(TTY_Device *dev);

#endif
