// SPDX-License-Identifier: GPL-3.0-only

#include "font.h"
#include "video.h"
#include <stddef.h>

static int s_Initialized = 0;
static int s_HasInfo = 0;
static int s_CursorX = 0;
static int s_CursorY = 0;
static int s_TextScale = 2;
static VBE_Info s_Info;

void VBE_SetInfo(const VBE_Info *info)
{
   if (!info) return;
   s_Info = *info;
   s_HasInfo = 1;
}

int VBE_HasInfo(void) { return s_HasInfo; }

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
   uint32_t rv = 0;
   uint32_t gv = 0;
   uint32_t bv = 0;

   if (s_Info.red_mask_size)
      rv = ((uint32_t)r * ((1u << s_Info.red_mask_size) - 1)) / 255u;
   if (s_Info.green_mask_size)
      gv = ((uint32_t)g * ((1u << s_Info.green_mask_size) - 1)) / 255u;
   if (s_Info.blue_mask_size)
      bv = ((uint32_t)b * ((1u << s_Info.blue_mask_size) - 1)) / 255u;

   return (rv << s_Info.red_field_position) |
          (gv << s_Info.green_field_position) |
          (bv << s_Info.blue_field_position);
}

static inline void put_pixel(int x, int y, uint32_t pixel)
{
   uint8_t *fb;
   uint32_t bytes_per_pixel;
   uint32_t offset;

   if (x < 0 || y < 0 || (uint32_t)x >= s_Info.width ||
       (uint32_t)y >= s_Info.height)
      return;

   bytes_per_pixel = (s_Info.bpp + 7u) / 8u;
   offset = (uint32_t)y * s_Info.pitch + (uint32_t)x * bytes_per_pixel;
   fb = (uint8_t *)(uintptr_t)s_Info.framebuffer_addr + offset;

   switch (bytes_per_pixel)
   {
   case 4:
      fb[0] = (uint8_t)(pixel & 0xFF);
      fb[1] = (uint8_t)((pixel >> 8) & 0xFF);
      fb[2] = (uint8_t)((pixel >> 16) & 0xFF);
      fb[3] = (uint8_t)((pixel >> 24) & 0xFF);
      break;
   case 3:
      fb[0] = (uint8_t)(pixel & 0xFF);
      fb[1] = (uint8_t)((pixel >> 8) & 0xFF);
      fb[2] = (uint8_t)((pixel >> 16) & 0xFF);
      break;
   case 2:
      fb[0] = (uint8_t)(pixel & 0xFF);
      fb[1] = (uint8_t)((pixel >> 8) & 0xFF);
      break;
   case 1:
      fb[0] = (uint8_t)(pixel & 0xFF);
      break;
   default:
      break;
   }
}

static void clear_screen(uint32_t pixel)
{
   uint32_t x, y;
   for (y = 0; y < s_Info.height; y++)
      for (x = 0; x < s_Info.width; x++) put_pixel((int)x, (int)y, pixel);
}

static void draw_glyph(uint8_t c, int x, int y, uint32_t fg)
{
   const uint8_t *glyph;
   int row, col;
   int scale = s_TextScale > 0 ? s_TextScale : 1;

   if (c < FONT_FIRST || c > FONT_LAST) c = '?';
   glyph = g_Font8x16[c - FONT_FIRST];

   for (row = 0; row < FONT_HEIGHT; row++)
   {
      uint8_t bits = glyph[row];
      for (col = 0; col < FONT_WIDTH; col++)
      {
         if (bits & (0x80 >> col))
         {
            int px = x + col * scale;
            int py = y + row * scale;
            for (int dy = 0; dy < scale; dy++)
               for (int dx = 0; dx < scale; dx++)
                  put_pixel(px + dx, py + dy, fg);
         }
      }
   }
}

int VBE_Initialize(void)
{
   uint32_t fg;

   if (!s_HasInfo) return ENODEV;

   s_CursorX = 0;
   s_CursorY = 0;
   s_Initialized = 1;

   fg = pack_rgb(0, 0, 0);
   clear_screen(fg);
   return SUCCESS;
}

int VBE_PutChar(char c, int x, int y, char color)
{
   uint32_t fg;
   int scale = s_TextScale > 0 ? s_TextScale : 1;
   int glyph_w = FONT_WIDTH * scale;
   int glyph_h = FONT_HEIGHT * scale;

   (void)color;
   if (!s_Initialized) return ENODEV;

   if (x < 0 && y < 0)
   {
      x = s_CursorX;
      y = s_CursorY;
   }
   else if ((x < 0) != (y < 0))
   {
      return EINVAL;
   }

   switch (c)
   {
   case '\n':
      s_CursorX = 0;
      s_CursorY += glyph_h;
      break;
   case '\r':
      s_CursorX = 0;
      break;
   case '\t':
      s_CursorX = (s_CursorX / (glyph_w * 4) + 1) * (glyph_w * 4);
      break;
   default:
      fg = pack_rgb(0x00, 0xFF, 0x00);
      draw_glyph((uint8_t)c, x, y, fg);
      s_CursorX = x + glyph_w;
      s_CursorY = y;
      break;
   }

   if ((uint32_t)(s_CursorY + glyph_h) > s_Info.height)
   {
      /* Move all pixel rows up by one line (glyph_h rows). */
      uint32_t scroll_pixels = (uint32_t)glyph_h;
      uint32_t row_bytes = s_Info.pitch;
      uint32_t copy_rows = s_Info.height - scroll_pixels;
      uint32_t fb_bytes = s_Info.height * row_bytes;
      uint32_t src_off = scroll_pixels * row_bytes;
      uint32_t y;
      uint8_t *fb = (uint8_t *)(uintptr_t)s_Info.framebuffer_addr;

      for (y = 0; y < copy_rows * row_bytes; y++) fb[y] = fb[y + src_off];

      /* Clear the newly exposed bottom rows. */
      for (y = fb_bytes - scroll_pixels * row_bytes; y < fb_bytes; y++)
         fb[y] = 0;

      s_CursorX = 0;
      s_CursorY = (int)(s_Info.height - scroll_pixels);
   }

   return SUCCESS;
}

int VBE_PutPixel(uint32_t pixel, int x, int y)
{
   if (!s_Initialized) return ENODEV;

   if (x < 0 || y < 0 || (uint32_t)x >= s_Info.width ||
       (uint32_t)y >= s_Info.height)
      return EINVAL;

   put_pixel(x, y, pixel);
   return SUCCESS;
}

uint32_t VBE_PackRGB(uint8_t r, uint8_t g, uint8_t b)
{
   if (!s_HasInfo) return 0;
   return pack_rgb(r, g, b);
}

uint32_t VBE_GetWidth(void) { return s_HasInfo ? s_Info.width : 0; }

uint32_t VBE_GetHeight(void) { return s_HasInfo ? s_Info.height : 0; }

void VBE_ClearScreen(uint32_t pixel) { clear_screen(pixel); }