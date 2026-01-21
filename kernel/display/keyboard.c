// SPDX-License-Identifier: GPL-3.0-only

#include "keyboard.h"
#include <display/buffer_text.h>
#include <std/stdio.h> // for putc/printf
#include <stdint.h>

/* Input line buffer for simple line editing */
#define KB_LINE_BUF 256
static char kb_line[KB_LINE_BUF];
static int kb_len = 0;
static int kb_ready = 0; /* 1 when a full line (\n) is available */

/* modifier state */
static int shift = 0;
static int caps = 0;
static int extended = 0; /* set when 0xE0 prefix received */

/* Minimal set-1 scancode -> ASCII map for printable keys.
   Extend as needed (this is not full; handles letters, digits, space, enter,
   backspace). */
static const char scancode_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    '\b', /* 0x00 - 0x0F */
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, /* 0x10 - 0x1F */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', /*0x20-0x2F*/
    'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
    ' ', /* 0x30 - 0x3B (space at 0x39) */
    /* rest zeros */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/**
 * Generic scancode handler (platform-independent)
 * Processes PS/2 scancodes and manages line buffering
 */
void Keyboard_HandleScancode(uint8_t scancode)
{
   /* handle key releases and modifier keys */

   /* handle 0xE0 extended prefix */
   if (scancode == 0xE0)
   {
      extended = 1;
      return;
   }

   /* key release */
   if (scancode & 0x80)
   {
      /* key release: clear shift if shift released */
      uint8_t key = scancode & 0x7F;
      if (key == 0x2A || key == 0x36) shift = 0; /* left/right shift */
      /* if this was an extended key release, clear extended state */
      if (extended)
      {
         extended = 0;
         return;
      }
      return;
   }

   /* check for shift press */
   if (scancode == 0x2A || scancode == 0x36)
   {
      shift = 1;
      return;
   }

   /* caps lock toggle (make only) */
   if (scancode == 0x3A)
   {
      caps = !caps;
      return;
   }

   /* If we previously saw 0xE0, handle extended keys (arrows) here */
   if (extended)
   {
      switch (scancode)
      {
      case 0x48: /* up */
      {
         int x, y;
         Buffer_GetCursor(&x, &y);
         /* Compute whether there is older content above the visible start
          * + current cursor row. If so, prefer to scroll the view rather
          * than moving the cursor further up within the window. */
         uint32_t visible_start = Buffer_GetVisibleStart();
         uint32_t logical_index = visible_start + (uint32_t)y;

         if (y > 0)
         {
            /* still room to move up within visible window */
            y--;
            Buffer_SetCursor(x, y);
         }
         else if (logical_index > 0)
         {
            /* There is older content above the visible window: scroll up by
             * one line instead of jumping to the very top. This lets users
             * reach the middle lines incrementally. */
            Buffer_Scroll(1);
            /* keep cursor at the top row of the visible window */
            Buffer_SetCursor(x, y);
         }
         else
         {
            /* at very top already */
         }
         break;
      }
      case 0x50: /* down */
      {
         int x, y;
         Buffer_GetCursor(&x, &y);
         uint32_t visible_start = Buffer_GetVisibleStart();
         uint32_t logical_index = visible_start + (uint32_t)y;
         uint32_t max_scroll = Buffer_GetMaxScroll();

         if (y < SCREEN_HEIGHT - 1)
         {
            /* move down within visible window */
            y++;
            Buffer_SetCursor(x, y);
         }
         else if (visible_start < max_scroll)
         {
            /* There is newer content below: scroll down by one line rather
             * than jumping straight to the bottom so the user can traverse
             * the middle region. */
            Buffer_Scroll(-1);
            Buffer_SetCursor(x, y);
         }
         else
         {
            /* at very bottom already */
         }
         break;
      }
      case 0x4B: /* left */
      {
         int x, y;
         Buffer_GetCursor(&x, &y);
         if (x > 0)
         {
            x--;
            Buffer_SetCursor(x, y);
         }
         else if (y > 0)
         {
            /* move to end of previous visible line */
            int prev_len = Buffer_GetVisibleLineLength(y - 1);
            y--;
            x = prev_len;
            Buffer_SetCursor(x, y);
         }
         else
         {
            /* at top: scroll up */
            Buffer_Scroll(1);
            Buffer_SetCursor(0, 0);
         }
         break;
      }
      case 0x4D: /* right */
      {
         int x, y;
         Buffer_GetCursor(&x, &y);
         int len = Buffer_GetVisibleLineLength(y);
         if (x < len)
         {
            x++;
            Buffer_SetCursor(x, y);
         }
         else
         {
            /* At end of current visible line. Only move to the next visible
               line if that line actually exists (has characters). Otherwise
               stay at the end of the current line. */
            if (y < SCREEN_HEIGHT - 1)
            {
               int next_len = Buffer_GetVisibleLineLength(y + 1);
               if (next_len > 0)
               {
                  y++;
                  x = 0;
                  Buffer_SetCursor(x, y);
               }
               else
               {
                  /* no next visible content: keep cursor at end */
                  Buffer_SetCursor(len, y);
               }
            }
            else
            {
               /* bottom row: don't create lines by scrolling; keep cursor at
                  end of current line */
               Buffer_SetCursor(len, y);
            }
         }
         break;
      }
      default:
         break;
      }
      extended = 0;
      return;
   }

   /* map scancode to character, apply shift/caps */
   if (scancode < sizeof(scancode_map))
   {
      char base = scancode_map[scancode];
      if (!base) return;

      char out = base;
      /* simple alphabetic handling for caps/shift */
      if (out >= 'a' && out <= 'z')
      {
         if ((caps && !shift) || (shift && !caps))
         {
            out = out - 'a' + 'A';
         }
      }
      else
      {
         /* rudimentary shifted symbols for digits/punctuation */
         if (shift)
         {
            switch (out)
            {
            case '1':
               out = '!';
               break;
            case '2':
               out = '@';
               break;
            case '3':
               out = '#';
               break;
            case '4':
               out = '$';
               break;
            case '5':
               out = '%';
               break;
            case '6':
               out = '^';
               break;
            case '7':
               out = '&';
               break;
            case '8':
               out = '*';
               break;
            case '9':
               out = '(';
               break;
            case '0':
               out = ')';
               break;
            case '-':
               out = '_';
               break;
            case '=':
               out = '+';
               break;
            case '\\':
               out = '|';
               break;
            case ';':
               out = ':';
               break;
            case '\'':
               out = '"';
               break;
            case ',':
               out = '<';
               break;
            case '.':
               out = '>';
               break;
            case '/':
               out = '?';
               break;
            case '`':
               out = '~';
               break;
            case '[':
               out = '{';
               break;
            case ']':
               out = '}';
               break;
            default:
               break;
            }
         }
      }

      /* handle backspace specially to update buffer */
      if (out == '\b')
      {
         if (kb_len > 0)
         {
            kb_len--;
            putc('\b');
         }
         return;
      }

      /* append to buffer if printable; on newline mark ready */
      if (out == '\n')
      {
         if (kb_len < KB_LINE_BUF - 1)
         {
            kb_line[kb_len++] = '\n';
         }
         kb_ready = 1;
         putc('\n');
      }
      else
      {
         if (kb_len < KB_LINE_BUF - 1)
         {
            kb_line[kb_len++] = out;
            putc(out);
         }
      }
   }
}

/**
 * Platform-independent non-blocking readline
 * Returns number of bytes written into buf, 0 if no line ready
 */
int Keyboard_ReadlineNb(char *buf, int bufsize)
{
   if (!kb_ready) return 0;
   int copy = kb_len;
   if (copy > bufsize - 1) copy = bufsize - 1;
   for (int i = 0; i < copy; ++i) buf[i] = kb_line[i];
   buf[copy] = '\0';

   /* reset buffer */
   kb_len = 0;
   kb_ready = 0;
   return copy;
}

/**
 * Platform-independent blocking readline
 * Note: The actual idle implementation is in the platform-specific driver
 */
int Keyboard_Readline(char *buf, int bufsize)
{
   /* This should not be called directly - use platform-specific wrapper instead
    */
   return Keyboard_ReadlineNb(buf, bufsize);
}