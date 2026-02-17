// SPDX-License-Identifier: GPL-3.0-only

#include "keyboard.h"
#include <drivers/tty/tty.h>
#include <fs/devfs/devfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>

/* Keyboard input buffer for devfs reads */
#define KEYBOARD_BUFFER_SIZE 256
static char g_KeyboardBuffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t g_KeyboardHead = 0;
static volatile uint32_t g_KeyboardTail = 0;

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

/* Push a character to the keyboard buffer for devfs reads */
static void keyboard_buffer_push(char c)
{
   uint32_t next = (g_KeyboardHead + 1) % KEYBOARD_BUFFER_SIZE;
   if (next != g_KeyboardTail)
   {
      g_KeyboardBuffer[g_KeyboardHead] = c;
      g_KeyboardHead = next;
   }
}

/* Pop a character from the keyboard buffer */
static int keyboard_buffer_pop(void)
{
   if (g_KeyboardTail == g_KeyboardHead)
   {
      return -1; /* Buffer empty */
   }
   char c = g_KeyboardBuffer[g_KeyboardTail];
   g_KeyboardTail = (g_KeyboardTail + 1) % KEYBOARD_BUFFER_SIZE;
   return (int)(unsigned char)c;
}

/* Get number of characters in buffer */
static uint32_t keyboard_buffer_count(void)
{
   if (g_KeyboardHead >= g_KeyboardTail)
   {
      return g_KeyboardHead - g_KeyboardTail;
   }
   return KEYBOARD_BUFFER_SIZE - g_KeyboardTail + g_KeyboardHead;
}

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

      /* Push to keyboard buffer for devfs reads */
      keyboard_buffer_push(out);

      /* Also push to TTY input for legacy support */
      TTY_InputPush(out);
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

/*
 * Devfs device operations for /dev/input/keyboard
 */

uint32_t Keyboard_DevfsRead(struct DEVFS_DeviceNode *node, uint32_t offset,
                            uint32_t size, void *buffer)
{
   (void)node;
   (void)offset;

   if (!buffer || size == 0) return 0;

   char *buf = (char *)buffer;
   uint32_t count = 0;

   while (count < size)
   {
      int c = keyboard_buffer_pop();
      if (c < 0) break;
      buf[count++] = (char)c;
   }

   return count;
}

uint32_t Keyboard_DevfsWrite(struct DEVFS_DeviceNode *node, uint32_t offset,
                             uint32_t size, const void *buffer)
{
   (void)node;
   (void)offset;
   (void)buffer;
   /* Keyboard is read-only, cannot write to it */
   return 0;
}

static DEVFS_DeviceOps keyboard_ops = {.read = Keyboard_DevfsRead,
                                       .write = Keyboard_DevfsWrite,
                                       .ioctl = NULL,
                                       .close = NULL};

/**
 * Initialize keyboard driver and register in devfs
 */
void Keyboard_Initialize(void)
{
   /* Clear the buffer */
   g_KeyboardHead = 0;
   g_KeyboardTail = 0;
   memset(g_KeyboardBuffer, 0, sizeof(g_KeyboardBuffer));

   /* Reset modifier state */
   shift = 0;
   caps = 0;
   extended = 0;

   /* Register keyboard as an input device in devfs
    * Major 13 is used for input devices in Linux
    * Minor 0 for keyboard
    */
   DEVFS_RegisterDevice("input/keyboard", DEVFS_TYPE_CHAR, 13, 0, 0,
                        &keyboard_ops, NULL);

   logfmt(LOG_INFO, "[KEYBOARD] Initialized and registered in devfs\n");
}
