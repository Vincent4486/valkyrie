// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARCH_I686_VIDEO_VGA_H
#define ARCH_I686_VIDEO_VGA_H

#include <stdint.h>

/* VGA 80×25 text-mode backend — implementation in vga.c.
 * These functions are referenced by the HAL_ARCH_Video_* macros in
 * hal/video.h and must not be called directly outside that layer. */

void i686_VGA_Initialize(void);
void i686_VGA_PutChar(char c, uint8_t color, int x, int y);
void i686_VGA_Clear(uint8_t color);
void i686_VGA_SetCursor(int x, int y);
void i686_VGA_UpdateBuffer(void *buffer);

#endif /* ARCH_I686_VIDEO_VGA_H */
