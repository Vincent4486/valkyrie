// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include <stdint.h>

/*
 * HAL video interface.
 *
 * Follows the same pattern as hal/io.h and hal/irq.h: arch-specific
 * function names are mapped to HAL_ARCH_Video_* macros here, then the
 * single global g_HalVideoOperations struct is populated with those
 * macros in hal/hal.c so the rest of the kernel never mentions
 * architecture-specific symbols.
 *
 * For pixel-addressed framebuffer modes the target address of any cell is:
 *   LinearAddress = Base + (y * Pitch + x * BytesPerPixel)
 */

#if defined(I686)
#include <arch/i686/video/vga.h>
#define HAL_ARCH_Video_PutChar       i686_VGA_PutChar
#define HAL_ARCH_Video_Clear         i686_VGA_Clear
#define HAL_ARCH_Video_SetCursor     i686_VGA_SetCursor
#define HAL_ARCH_Video_UpdateBuffer  i686_VGA_UpdateBuffer
#define HAL_ARCH_Video_SetDisplaySize i686_VGA_SetDisplaySize
#else
#error "Unsupported architecture for HAL Video"
#endif

typedef struct HAL_VideoOperations
{
   /* Write one character to grid cell (x, y) with the given colour byte. */
   void (*PutChar)(char c, uint8_t color, int x, int y);

   /* Fill the entire visible surface with the given background colour. */
   void (*Clear)(uint8_t color);

   /* Move the hardware text cursor to column x, row y. */
   void (*SetCursor)(int x, int y);

   /* Blit a pre-composed shadow buffer to VRAM.
    *   VGA text mode  – buffer is uint16_t[80 * 25]
    *   VESA/VBE       – buffer is a full linear framebuffer
    */
   void (*UpdateBuffer)(void *buffer);

   /* Switch to a supported text mode (cols × rows).
    * Supported: 40×25, 80×25, 80×43, 80×50.
    * Returns 0 on success, -1 if the size is not a recognised mode.
    */
   int (*SetDisplaySize)(int cols, int rows);
} HAL_VideoOperations;

extern const HAL_VideoOperations *g_HalVideoOperations;

#endif /* HAL_VIDEO_H */
