// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARCH_I686_VIDEO_VGA_H
#define ARCH_I686_VIDEO_VGA_H

#include <stdint.h>

/* VGA text-mode backend — implementation in vga.c.
 * These functions are referenced by the HAL_ARCH_Video_* macros in
 * hal/video.h and must not be called directly outside that layer. */

void i686_VGA_Initialize(void);
void i686_VGA_PutChar(char c, uint8_t color, int x, int y);
void i686_VGA_Clear(uint8_t color);
void i686_VGA_SetCursor(int x, int y);
void i686_VGA_UpdateBuffer(void *buffer);

/**
 * Switch to a supported VGA text mode.
 *
 * Supported sizes: 40×25, 80×25, 80×43, 80×50.
 *
 * @param cols  Number of text columns.
 * @param rows  Number of text rows.
 * @return  0 on success, -1 if the requested size is not a supported mode
 *          (current mode is left unchanged).
 */
int i686_VGA_SetDisplaySize(int cols, int rows);

#endif /* ARCH_I686_VIDEO_VGA_H */
