// SPDX-License-Identifier: GPL-3.0-only

#include "video.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

#define VGATEXT_BUFFER ((volatile char *)0xB8000)

static int s_Initialized = 0;
static int s_CursorX = 0;
static int s_CursorY = 0;
static char s_Color = VGATEXT_DEFAULT_COLOR;

/* ------------------------------------------------------------------ */
/*  Scroll the buffer up by one line.                                  */
/* ------------------------------------------------------------------ */

static void scroll(void)
{
   volatile char *buf = VGATEXT_BUFFER;
   int row, col;

   /* Copy each row up by one */
   for (row = 1; row < VGATEXT_HEIGHT; row++)
   {
      for (col = 0; col < VGATEXT_WIDTH; col++)
      {
         int src_off = (row * VGATEXT_WIDTH + col) * 2;
         int dst_off = ((row - 1) * VGATEXT_WIDTH + col) * 2;
         buf[dst_off] = buf[src_off];
         buf[dst_off + 1] = buf[src_off + 1];
      }
   }

   /* Clear last line */
   for (col = 0; col < VGATEXT_WIDTH; col++)
   {
      int off = ((VGATEXT_HEIGHT - 1) * VGATEXT_WIDTH + col) * 2;
      buf[off] = ' ';
      buf[off + 1] = s_Color;
   }

   s_CursorY = VGATEXT_HEIGHT - 1;
}

void move_cursor(int x, int y)
{
   unsigned short position = (unsigned short)((y * 80) + x);

   /* VGA CRT Controller registers: index 0x3D4, data 0x3D5 */
   outb(0x3D4, 0x0F);
   outb(0x3D5, (uint8_t)(position & 0xFF));
   outb(0x3D4, 0x0E);
   outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

int VGATEXT_Initialize(void)
{
   volatile char *buf = VGATEXT_BUFFER;
   int i;

   /* Clear the entire VGA buffer */
   for (i = 0; i < VGATEXT_WIDTH * VGATEXT_HEIGHT * 2; i += 2)
   {
      buf[i] = ' ';
      buf[i + 1] = s_Color;
   }

   s_CursorX = 0;
   s_CursorY = 0;
   move_cursor(s_CursorX, s_CursorY);

   s_Initialized = 1;
   return SUCCESS;
}

int VGATEXT_PutChar(char c, int x, int y, char color)
{
   volatile char *buf;
   int pos;

   /* Must be initialized */
   if (!s_Initialized) return -ENODEV;

   /* Exactly one of x / y negative → invalid */
   if ((x < 0) != (y < 0)) return -EINVAL;

   /* Both negative → write at cursor, then advance */
   if (x < 0 && y < 0)
   {
      x = s_CursorX;
      y = s_CursorY;
   }
   else
   {
      /* Clamp to screen bounds */
      if (x < 0) x = 0;
      if (x >= VGATEXT_WIDTH) x = VGATEXT_WIDTH - 1;
      if (y < 0) y = 0;
      if (y >= VGATEXT_HEIGHT) y = VGATEXT_HEIGHT - 1;
   }

   buf = VGATEXT_BUFFER;

   switch (c)
   {
   case '\n':
      /* Newline: carriage-return + line-feed */
      s_CursorX = 0;
      s_CursorY = y + 1;
      if (s_CursorY >= VGATEXT_HEIGHT) scroll();
      break;

   case '\r':
      /* Carriage return: go to column 0 on the same line */
      s_CursorX = 0;
      s_CursorY = y;
      break;

   case '\t':
      /* Tab: advance to next 8-column boundary */
      {
         int tab_stop = (x / 8 + 1) * 8;
         if (tab_stop >= VGATEXT_WIDTH) tab_stop = VGATEXT_WIDTH - 1;
         s_CursorX = tab_stop;
         s_CursorY = y;
      }
      break;

   case '\b':
      /* Backspace: move cursor left one, don't erase */
      if (x > 0)
      {
         s_CursorX = x - 1;
         s_CursorY = y;
      }
      break;

   default:
      /* Regular character: write to buffer */
      pos = (y * VGATEXT_WIDTH + x) * 2;
      buf[pos] = c;
      buf[pos + 1] = color;

      /* Advance cursor */
      s_CursorX = x + 1;
      s_CursorY = y;
      if (s_CursorX >= VGATEXT_WIDTH)
      {
         s_CursorX = 0;
         s_CursorY++;
         if (s_CursorY >= VGATEXT_HEIGHT) scroll();
      }
      break;
   }
   move_cursor(s_CursorX, s_CursorY);

   return SUCCESS;
}

int VGATEXT_PutPixel(int pixel, int x, int y)
{
   (void)pixel;
   (void)x;
   (void)y;
   return -EINVAL;
}