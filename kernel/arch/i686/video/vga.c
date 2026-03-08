// SPDX-License-Identifier: GPL-3.0-only

/*
 * kernel/arch/i686/video/vga.c
 *
 * VGA 80x25 text-mode backend for the HAL video interface.
 *
 * This translation unit is the ONLY place in the i686 port that touches
 * VGA VRAM (0xB8000) and the CRT controller I/O ports (0x3D4/0x3D5).
 * All other code reaches hardware rendering through g_HalVideoOperations.
 */

#include "vga.h"
#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <std/string.h>
#include <stdint.h>

/* ── Hardware constants ──────────────────────────────────────────────────── */

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

/* CRT controller: index port and data port */
#define VGA_CRTC_ADDR 0x3D4
#define VGA_CRTC_DATA 0x3D5

/* CRTC cursor position registers */
#define VGA_CRTC_CURSOR_HI 0x0E
#define VGA_CRTC_CURSOR_LO 0x0F

/* CRTC cursor-shape registers */
#define VGA_CRTC_CURSOR_START 0x0A
#define VGA_CRTC_CURSOR_END   0x0B

/* ── Backend implementation ──────────────────────────────────────────────── */

/*
 * VGA_Initialize — one-time hardware setup.
 *
 * Programs the CRT controller cursor-shape registers so that the blinking
 * underline cursor is visible in 80×25 text mode:
 *   - Register 0x0A (Cursor Start): scan line 14, bit 5 clear (cursor on).
 *   - Register 0x0B (Cursor End):   scan line 15.
 * Then homes the cursor to (0, 0).
 */
void i686_VGA_Initialize(void)
{
   /* Cursor Start: enable cursor (bit 5 = 0), start scan line 14 */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_START);
   g_HalIoOperations->outb(VGA_CRTC_DATA, 0x0E);

   /* Cursor End: end scan line 15 */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_END);
   g_HalIoOperations->outb(VGA_CRTC_DATA, 0x0F);

   i686_VGA_SetCursor(0, 0);
}

/*
 * VGA_PutChar — write one character directly into VGA VRAM.
 *
 * Low-level primitive for early-boot or panic output.  The TTY driver
 * normally composes a full uint16_t[80*25] shadow buffer in RAM and blits
 * it in one shot via VGA_UpdateBuffer.
 */
void i686_VGA_PutChar(char c, uint8_t color, int x, int y)
{
   if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
   VGA_BUFFER[y * VGA_COLS + x] = ((uint16_t)color << 8) | (uint8_t)c;
}

/*
 * VGA_Clear — fill all 80×25 cells with spaces in the requested colour.
 */
void i686_VGA_Clear(uint8_t color)
{
   uint16_t blank = ((uint16_t)color << 8) | ' ';
   for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) VGA_BUFFER[i] = blank;
}

/*
 * VGA_SetCursor — program the CRT controller to move the hardware cursor.
 *
 * The CRT controller expects a linear offset:
 *   offset = y × VGA_COLS + x
 * sent as two 8-bit writes to index port 0x3D4 / data port 0x3D5.
 */
void i686_VGA_SetCursor(int x, int y)
{
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   uint16_t pos = (uint16_t)(y * VGA_COLS + x);

   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_HI);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));

   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_LO);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
}

/*
 * VGA_UpdateBuffer — blit a pre-composed shadow buffer to VGA VRAM.
 *
 * The TTY driver keeps a uint16_t[VGA_COLS * VGA_ROWS] copy in normal RAM
 * and calls this once per repaint so VRAM is updated in one memcpy.
 */
void i686_VGA_UpdateBuffer(void *buffer)
{
   memcpy((void *)VGA_BUFFER, buffer, VGA_COLS * VGA_ROWS * sizeof(uint16_t));
}
