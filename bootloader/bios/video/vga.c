// SPDX-License-Identifier: GPL-3.0-only

#include "font.h"
#include "video.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* VGA Mode 0x13 constants                                            */
/* ------------------------------------------------------------------ */

#define VGA_FB ((volatile uint8_t *)0xA0000)

#define VGA_WIDTH 320
#define VGA_HEIGHT 200

/* VGA I/O ports */
#define VGA_MISC_OUT 0x3C2

#define VGA_SEQ_IDX 0x3C4
#define VGA_SEQ_DATA 0x3C5

#define VGA_CRTC_IDX 0x3D4
#define VGA_CRTC_DATA 0x3D5

#define VGA_GC_IDX 0x3CE
#define VGA_GC_DATA 0x3CF

#define VGA_AC_IDX 0x3C0
#define VGA_INSTAT_1 0x3DA

/* ------------------------------------------------------------------ */
/* Shadow framebuffer                                                 */
/* ------------------------------------------------------------------ */

static uint8_t s_Shadow[VGA_WIDTH * VGA_HEIGHT];

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static int s_Initialized = 0;
static int s_CursorX = 0;
static int s_CursorY = 0;

/* ------------------------------------------------------------------ */
/* VGA register helpers                                               */
/* ------------------------------------------------------------------ */

static inline void seq_w(uint8_t idx, uint8_t val)
{
   outb(VGA_SEQ_IDX, idx);
   outb(VGA_SEQ_DATA, val);
}

static inline void crtc_w(uint8_t idx, uint8_t val)
{
   outb(VGA_CRTC_IDX, idx);
   outb(VGA_CRTC_DATA, val);
}

static inline void gc_w(uint8_t idx, uint8_t val)
{
   outb(VGA_GC_IDX, idx);
   outb(VGA_GC_DATA, val);
}

/* ------------------------------------------------------------------ */
/* Proper VGA Mode 13h setup                                          */
/* ------------------------------------------------------------------ */

static void set_mode_0x13(void)
{
   static const uint8_t misc = 0x63;

   /* Standard VGA mode 13h CRTC values for 320x200x256 */
   /* NOTE: CRTC[9] (Max Scan Line) uses 0x01, NOT 0x41.
    * Bit 6 in 0x41 enables 200-to-400 line conversion (doublescan)
    * on Cirrus and other VGA chips, which breaks the frame buffer
    * mapping and causes only partial screen updates. */
   static const uint8_t crtc[] = {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF,
                                  0x1F, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40,
                                  0x96, 0xB9, 0xA3, 0xFF};

   static const uint8_t gc[] = {0x00, 0x00, 0x00, 0x00, 0x00,
                                0x40, 0x05, 0x0F, 0xFF};

   static const uint8_t ac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14,
                                0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
                                0x3E, 0x3F, 0x41, 0x00, 0x0F, 0x00, 0x00};

   outb(VGA_MISC_OUT, misc);

   /* Sequencer */
   for (uint8_t i = 0; i < 5; i++) seq_w(i, 0x03);
   seq_w(0x01, 0x01);
   seq_w(0x02, 0x0F);
   seq_w(0x03, 0x00);
   seq_w(0x04, 0x0E);

   /* Unlock CRTC */
   crtc_w(0x11, crtc[0x11] & ~0x80);

   for (uint8_t i = 0; i < 25; i++) crtc_w(i, crtc[i]);

   for (uint8_t i = 0; i < 9; i++) gc_w(i, gc[i]);

   for (uint8_t i = 0; i < 21; i++)
   {
      inb(VGA_INSTAT_1);
      outb(VGA_AC_IDX, i);
      outb(VGA_AC_IDX, ac[i]);
   }

   inb(VGA_INSTAT_1);
   outb(VGA_AC_IDX, 0x20);
}

/* ------------------------------------------------------------------ */
/* Pixel operations                                                   */
/* ------------------------------------------------------------------ */

static inline void put_pixel(int x, int y, uint8_t colour)
{
   if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;

   int idx = y * VGA_WIDTH + x;

   s_Shadow[idx] = colour;
   VGA_FB[idx] = colour;
}

/* ------------------------------------------------------------------ */
/* Glyph drawing                                                      */
/* ------------------------------------------------------------------ */

static void draw_glyph(uint8_t c, int x, int y, uint8_t fg)
{
   if (c < FONT_FIRST || c > FONT_LAST) c = '?';

   const uint8_t *glyph = g_Font8x16[c - FONT_FIRST];

   for (int row = 0; row < FONT_HEIGHT; row++)
   {
      uint8_t bits = glyph[row];

      for (int col = 0; col < FONT_WIDTH; col++)
      {
         if (bits & (0x80 >> col)) put_pixel(x + col, y + row, fg);
      }
   }
}

/* ------------------------------------------------------------------ */
/* Clear screen                                                       */
/* ------------------------------------------------------------------ */

static void clear_screen(uint8_t colour)
{
   uint32_t val = (uint32_t)colour * 0x01010101u;
   volatile uint32_t *fb32 = (volatile uint32_t *)VGA_FB;

   for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) s_Shadow[i] = colour;

   /* Write to VGA framebuffer using 32-bit accesses for reliability
    * with Cirrus chain-4 mode.  dword writes send 4 bytes at once,
    * one to each VGA plane, ensuring the full 64KB is covered. */
   for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT / 4; i++) fb32[i] = val;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int VGA_Initialize(void)
{
   set_mode_0x13();
   clear_screen(0);

   s_CursorX = 0;
   s_CursorY = 0;
   s_Initialized = 1;

   return SUCCESS;
}

int VGA_PutChar(char c, int x, int y, char color)
{
   if (!s_Initialized) return ENODEV;

   /* * FIX: Force stream mode behavior if the coordinate input is detected
    * to have wrapped around or gone out of bounds due to bootloader row
    * metrics.
    */
   if ((x < 0 && y < 0) || y >= VGA_HEIGHT ||
       (s_CursorY >= (VGA_HEIGHT - FONT_HEIGHT) && y == 0))
   {
      x = s_CursorX;
      y = s_CursorY;
   }
   else if ((x < 0) != (y < 0))
   {
      return EINVAL;
   }

   /* Control characters */
   switch (c)
   {
   case '\n':
      x = 0;
      y += FONT_HEIGHT;
      break;

   case '\r':
      x = 0;
      break;

   case '\t':
      x = (x / (FONT_WIDTH * 4) + 1) * (FONT_WIDTH * 4);
      if (x >= VGA_WIDTH)
      {
         x = 0;
         y += FONT_HEIGHT;
      }
      break;

   default:
      if (x + FONT_WIDTH > VGA_WIDTH)
      {
         x = 0;
         y += FONT_HEIGHT;
      }
      break;
   }

   /* Clear the entire buffer and restart at origin when the bottom is reached.
    */
   if (y + FONT_HEIGHT > VGA_HEIGHT)
   {
      clear_screen(0);
      x = 0;
      y = 0;
   }

   /* Draw glyph */
   if (c != '\n' && c != '\r' && c != '\t')
   {
      draw_glyph((uint8_t)c, x, y, (uint8_t)color);
      x += FONT_WIDTH;
   }

   /* Always preserve coordinates internally */
   s_CursorX = x;
   s_CursorY = y;

   return SUCCESS;
}

int VGA_PutPixel(int pixel, int x, int y)
{
   if (!s_Initialized) return ENODEV;

   if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return EINVAL;

   put_pixel(x, y, (uint8_t)pixel);
   return SUCCESS;
}