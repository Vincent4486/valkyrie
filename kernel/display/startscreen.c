// SPDX-License-Identifier: GPL-3.0-only

#include "startscreen.h"
#include <stdbool.h>
#include <stdint.h>

/* Simple VGA text-mode helper for stage2 (80x25, color attributes).
   This is intentionally small and self-contained so it can be used in
   a freestanding environment during early boot. */

/* VGA text mode constants */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

/* Display box constants */
#define BOX_WIDTH 60
#define BOX_HEIGHT 15
#define BOX_OFFSET_Y 1

/* Animation timing */
#define ANIMATION_DELAY_MS 300
#define CHAR_PRINT_DELAY_MS 300

/* Delay calibration (iterations per millisecond for typical emulator) */
#define DELAY_ITERS_PER_MS 40000UL

static volatile uint16_t *const VGA = VGA_BUFFER;
static int cur_x = 0;
static int cur_y = 0;
static uint8_t cur_attr = 0x07; /* light gray on black */

static inline int clamp_x(int x)
{
   if (x < 0) return 0;
   if (x >= VGA_WIDTH) return VGA_WIDTH - 1;
   return x;
}

static inline int clamp_y(int y)
{
   if (y < 0) return 0;
   if (y >= VGA_HEIGHT) return VGA_HEIGHT - 1;
   return y;
}

static void scroll_up_if_needed(void)
{
   if (cur_y < VGA_HEIGHT) return;
   /* move lines 1..24 to 0..23 */
   for (int row = 0; row < VGA_HEIGHT - 1; ++row)
   {
      for (int col = 0; col < VGA_WIDTH; ++col)
      {
         VGA[row * VGA_WIDTH + col] = VGA[(row + 1) * VGA_WIDTH + col];
      }
   }
   /* clear last row */
   for (int col = 0; col < VGA_WIDTH; ++col)
   {
      VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
          (uint16_t)(' ') | ((uint16_t)cur_attr << 8);
   }
   cur_y = VGA_HEIGHT - 1;
}

void draw_start_screen(bool showBoot)
{
   if (showBoot)
   {
      draw_outline();
      draw_text();
   }
}

void draw_outline()
{
   /* rainbow border using spaces with different background colors.
      Draw each cell and pause after each to create a per-character animation.
    */
   /* compute centered box dimensions */
   const int box_width = BOX_WIDTH; /* interior including borders */
   const int box_height = BOX_HEIGHT;
   const int left = (VGA_WIDTH - box_width) / 2;
   int top = (VGA_HEIGHT - box_height) / 2;
   /* move the whole outline up by BOX_OFFSET_Y rows, clamp to screen */
   top -= BOX_OFFSET_Y;
   if (top < 0) top = 0;
   const int right = left + box_width - 1;
   const int bottom = top + box_height - 1;

   const uint8_t palette[] = {0x04, 0x06, 0x02, 0x03, 0x01, 0x05, 0x0E, 0x0C};
   const int pcount = sizeof(palette) / sizeof(palette[0]);

   int idx = 0;
   /* top row: draw in horizontal pairs so adjacent characters match */
   for (int x = left; x <= right; x += 2)
   {
      uint8_t bg = palette[idx % pcount];
      uint8_t attr = (bg << 4) | 0x00;
      VGA[top * VGA_WIDTH + x] = (uint16_t)(' ') | ((uint16_t)attr << 8);
      delay_ms(ANIMATION_DELAY_MS);
      if (x + 1 <= right)
      {
         VGA[top * VGA_WIDTH + (x + 1)] =
             (uint16_t)(' ') | ((uint16_t)attr << 8);
         delay_ms(ANIMATION_DELAY_MS);
      }
      ++idx;
   }
   /* bottom row */
   /* bottom row: horizontal pairs as well */
   for (int x = left; x <= right; x += 2)
   {
      uint8_t bg = palette[idx % pcount];
      uint8_t attr = (bg << 4) | 0x00;
      VGA[bottom * VGA_WIDTH + x] = (uint16_t)(' ') | ((uint16_t)attr << 8);
      delay_ms(ANIMATION_DELAY_MS);
      if (x + 1 <= right)
      {
         VGA[bottom * VGA_WIDTH + (x + 1)] =
             (uint16_t)(' ') | ((uint16_t)attr << 8);
         delay_ms(ANIMATION_DELAY_MS);
      }
      ++idx;
   }
   /* left and right columns (two characters thick) */
   for (int y = top + 1; y < bottom; ++y)
   {
      /* left pair (same color horizontally) */
      uint8_t bg_left = palette[idx % pcount];
      uint8_t attr_left = (bg_left << 4) | 0x00;
      VGA[y * VGA_WIDTH + left] = (uint16_t)(' ') | ((uint16_t)attr_left << 8);
      delay_ms(ANIMATION_DELAY_MS);
      VGA[y * VGA_WIDTH + (left + 1)] =
          (uint16_t)(' ') | ((uint16_t)attr_left << 8);
      delay_ms(ANIMATION_DELAY_MS);
      ++idx;

      /* right pair (same color horizontally) */
      uint8_t bg_right = palette[idx % pcount];
      uint8_t attr_right = (bg_right << 4) | 0x00;
      VGA[y * VGA_WIDTH + right] =
          (uint16_t)(' ') | ((uint16_t)attr_right << 8);
      delay_ms(ANIMATION_DELAY_MS);
      VGA[y * VGA_WIDTH + (right - 1)] =
          (uint16_t)(' ') | ((uint16_t)attr_right << 8);
      delay_ms(ANIMATION_DELAY_MS);
      ++idx;
   }
   /* corners are already filled by the top/bottom and left/right pairs */
}

void draw_text()
{
   const char *title = "Valkyrie OS";
   const char *line2 = "Loading...";

   /* center title on box */
   int title_len = 0;
   while (title[title_len]) ++title_len;
   int x = (80 - title_len) / 2;
   int y = 10;
   gotoxy(x, y);
   for (int i = 0; title[i]; ++i) printChar(title[i], 0x0F);

   gotoxy((80 - 10) / 2, y + 2);
   for (int i = 0; line2[i]; ++i) printChar(line2[i], 0x0F);
}

void gotoxy(int x, int y)
{
   cur_x = clamp_x(x);
   cur_y = clamp_y(y);
}

void printChar(char character, uint8_t color)
{
   cur_attr = color;
   if (character == '\n')
   {
      cur_x = 0;
      ++cur_y;
      scroll_up_if_needed();
      return;
   }

   VGA[cur_y * 80 + cur_x] = (uint16_t)character | ((uint16_t)cur_attr << 8);
   ++cur_x;
   if (cur_x >= 80)
   {
      cur_x = 0;
      ++cur_y;
      scroll_up_if_needed();
   }

   /* Busy-wait ~300 ms. This is approximate and depends on CPU speed.
   If you need precise timing, use PIT calibration or the PIT itself. */
   extern void delay_ms(unsigned int ms);
   delay_ms(300);
}

/* Very small, approximate busy-wait delay. Calibrate or replace with PIT
   for accurate timings. This loops a simple number of cycles; value chosen
   to be reasonable for typical emulator speeds. */
void delay_ms(unsigned int ms)
{
   /* inner loop iterations per millisecond; tuned for QEMU/typical x86 speed.
      If this is too slow/fast on your machine, reduce/increase the factor. */
   volatile unsigned long outer = ms;
   const unsigned long iters_per_ms = 40000UL; /* rough, emulator-friendly */
   for (; outer; --outer)
   {
      volatile unsigned long i = iters_per_ms;
      while (i--)
      {
         __asm__ volatile("nop");
      }
   }
}
