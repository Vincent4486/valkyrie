// SPDX-License-Identifier: GPL-3.0-only

#include "logo_gen.h"
#include "video.h"

#ifndef CORE
void LOGO_Draw(void)
{
   uint32_t palette[VALECIUM_LOGO_PALETTE_SIZE];
   uint32_t screen_w;
   uint32_t screen_h;
   uint32_t i;
   uint32_t target_w;
   uint32_t target_h;
   int origin_x, origin_y;
   int x, y;

   if (!VBE_HasInfo()) return;

   for (i = 0; i < VALECIUM_LOGO_PALETTE_SIZE; i++)
   {
      uint8_t r = g_ValeciumLogo_PaletteRGB[i * 3u + 0u];
      uint8_t g = g_ValeciumLogo_PaletteRGB[i * 3u + 1u];
      uint8_t b = g_ValeciumLogo_PaletteRGB[i * 3u + 2u];
      palette[i] = VBE_PackRGB(r, g, b);
   }

   screen_w = VBE_GetWidth();
   screen_h = VBE_GetHeight();

   target_h = (screen_h * 6u) / 10u;
   if (target_h < 1u) target_h = 1u;
   target_w =
       (uint32_t)(((uint64_t)VALECIUM_LOGO_W * target_h) / VALECIUM_LOGO_H);
   if (target_w < 1u) target_w = 1u;

   origin_x = ((int)screen_w - (int)target_w) / 2;
   origin_y = ((int)screen_h - (int)target_h) / 2;

   for (y = 0; y < (int)target_h; y++)
   {
      int dest_y = origin_y + y;
      uint32_t src_y;

      if (dest_y < 0 || dest_y >= (int)screen_h) continue;

      src_y = (uint32_t)(((uint64_t)y * VALECIUM_LOGO_H) / target_h);

      for (x = 0; x < (int)target_w; x++)
      {
         int dest_x = origin_x + x;
         uint32_t src_x;
         uint32_t src_i;
         uint8_t b;
         uint8_t idx;
         uint32_t color;

         if (dest_x < 0 || dest_x >= (int)screen_w) continue;

         src_x = (uint32_t)(((uint64_t)x * VALECIUM_LOGO_W) / target_w);
         src_i = src_y * (uint32_t)VALECIUM_LOGO_W + src_x;
         b = g_ValeciumLogo_Data4bpp[src_i >> 1];
         idx = (src_i & 1u) ? (b & 0x0Fu) : (uint8_t)((b >> 4) & 0x0Fu);
         color = palette[idx & 0x0Fu];

         VBE_PutPixel(color, dest_x, dest_y);
      }
   }
}
#endif /* !CORE */
