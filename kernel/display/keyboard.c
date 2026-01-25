// SPDX-License-Identifier: GPL-3.0-only

#include "keyboard.h"
#include <drivers/tty/tty.h>
#include <std/stdio.h> // for printf if needed
#include <stdint.h>

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

   /* Handle simple extended keys (E0 prefix) such as arrow keys */
   if (extended)
   {
      /* capture device and position */
      TTY_Device *dev = TTY_GetDevice();
      int cx = 0, cy = 0;
      if (dev) TTY_GetCursor(dev, &cx, &cy);
      switch (scancode)
      {
         case 0x4B: /* left */
            if (dev && cx > 0) TTY_SetCursor(dev, cx - 1, cy);
            break;
         case 0x4D: /* right */
            if (dev) TTY_SetCursor(dev, cx + 1, cy);
            break;
         case 0x48: /* up */
            if (dev && cy > 0) TTY_SetCursor(dev, cx, cy - 1);
            break;
         case 0x50: /* down */
            if (dev) TTY_SetCursor(dev, cx, cy + 1);
            break;
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

      /* push to TTY input only; do not echo to the screen here */
      TTY_InputPush(out);
      // TTY_Device *dev = TTY_GetDevice();
      // if (dev) TTY_Write(dev, &out, 1);
   }
}

/**
 * Platform-independent non-blocking readline
 * Returns number of bytes written into buf, 0 if no line ready
 */
int Keyboard_ReadlineNb(char *buf, int bufsize)
{
   static char line_buf[256];
   static int line_len = 0;

   TTY_Device *dev = TTY_GetDevice();
   if (!dev) return 0;

   int ci;
   while ((ci = TTY_ReadChar()) >= 0)
   {
      char c = (char)ci;
      if (c == '\n' || line_len >= sizeof(line_buf) - 1)
      {
         line_buf[line_len] = '\0';
         int copy = line_len + 1; // include \0
         if (copy > bufsize) copy = bufsize;
         for (int i = 0; i < copy; i++) buf[i] = line_buf[i];
         line_len = 0;
         return copy - 1; // exclude \0
      }
      else if (c == '\b')
      {
         if (line_len > 0) line_len--;
      }
      else
      {
         line_buf[line_len++] = c;
      }
   }
   return 0;
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