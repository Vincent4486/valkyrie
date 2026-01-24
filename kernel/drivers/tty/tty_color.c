// SPDX-License-Identifier: GPL-3.0-only

#include <stddef.h>
#include <stdint.h>

/* Map ANSI colors to VGA palette indices.
 * This mapping follows the previous kernel mapping used elsewhere.
 */
static inline int ansi_to_vga(int c)
{
   static const int map[8] = {0, 4, 2, 6, 1, 5, 3, 7};
   if (c < 0 || c > 7) return 7;
   return map[c] & 0x7;
}

void tty_color_apply_sgr(uint8_t *color, int *params, int pcount)
{
   if (!color) return;
   if (pcount == 0)
   {
      *color = 0x7; /* default */
      return;
   }

   for (int i = 0; i < pcount; ++i)
   {
      int v = params[i];
      if (v == 0)
      {
         *color = 0x7;
      }
      else if (v == 1)
      {
         /* intensity/bold: set bright bit on foreground */
         *color |= 0x08;
      }
      else if (v == 22)
      {
         /* normal intensity */
         *color &= ~0x08;
      }
      else if (v == 39)
      {
         /* default foreground */
         *color = (*color & 0xF0) | 0x07;
      }
      else if (v == 49)
      {
         /* default background */
         *color = (*color & 0x0F) | (0x00 << 4);
      }
      else if (v >= 30 && v <= 37)
      {
         int fg = ansi_to_vga(v - 30) & 0x7;
         if (*color & 0x08) fg |= 0x8;
         *color = (*color & 0xF0) | (fg & 0x0F);
      }
      else if (v >= 40 && v <= 47)
      {
         int bg = ansi_to_vga(v - 40) & 0x7;
         *color = (*color & 0x0F) | (bg << 4);
      }
      else if (v >= 90 && v <= 97)
      {
         /* bright foreground 90-97 */
         int fg = ansi_to_vga(v - 90) & 0x7;
         fg |= 0x8;
         *color = (*color & 0xF0) | (fg & 0x0F);
      }
      else if (v >= 100 && v <= 107)
      {
         /* bright background (attempt) 100-107 */
         int bg = ansi_to_vga(v - 100) & 0x7;
         /* mark as bright if possible */
         bg |= 0x8;
         *color = (*color & 0x0F) | ((bg & 0x0F) << 4);
      }
      /* ignore unsupported SGR codes here (underline, reverse, etc.) */
   }
}
