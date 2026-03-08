// SPDX-License-Identifier: GPL-3.0-only

#include "tty.h"
#include <fs/devfs/devfs.h>
#include <hal/video.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>

/*
 * Linux-like TTY subsystem implementation
 *
 * Features:
 * - Multiple TTY device instances
 * - Canonical (line-buffered) and raw input modes
 * - Echo control
 * - Line editing with backspace, kill line, etc.
 * - ANSI escape sequence support
 * - Scrollback buffer
 */

/* TTY device array */
static TTY_Device *g_TTYDevices[TTY_MAX_DEVICES];
static TTY_Device *g_ActiveTTY = NULL;
static bool g_TTYInitialized = false;

/*
 * Static BSS pools for all TTY devices – no kmalloc for buffers.
 *   display: 8 × (TTY_MAX_COLS × TTY_MAX_ROWS × 2) = 128 KiB
 *   input:   8 ×  4096                             =  32 KiB
 * screen_buf is embedded directly inside each TTY_Device struct.
 * Buffers are sized for the maximum supported VGA text mode so that a mode
 * switch never requires reallocation.
 *
 * g_TTYDisplayBufs is aligned to K_MEM_BLOCK_SIZE (1024 bytes) so that each
 * device's shadow buffer starts on an aligned boundary.  An 80×25 blit is
 * 80 × 25 × 2 = 4000 bytes, which fits within 4 consecutive 1 KiB blocks;
 * the alignment guarantee prevents the DMA-style memcpy to VGA VRAM
 * (i686_VGA_UpdateBuffer) from spanning an unexpected page boundary.
 */
static uint16_t g_TTYDisplayBufs[TTY_MAX_DEVICES][TTY_MAX_ROWS * TTY_MAX_COLS]
   __attribute__((aligned(1024)));
static char     g_TTYInputBufs[TTY_MAX_DEVICES][TTY_INPUT_SIZE];

/* ANSI color mapping */
static const uint8_t ansi_to_vga_fg[] = {0x0, 0x4, 0x2, 0x6,
                                         0x1, 0x5, 0x3, 0x7};
static const uint8_t ansi_to_vga_bg[] = {0x00, 0x40, 0x20, 0x60,
                                         0x10, 0x50, 0x30, 0x70};

/*
 * Buffer operations
 */

static void buffer_init(TTY_Buffer *buf, char *data, uint32_t size)
{
   buf->data = data;
   buf->size = size;
   buf->head = 0;
   buf->tail = 0;
   buf->count = 0;
}

static bool buffer_push(TTY_Buffer *buf, char c)
{
   if (buf->count >= buf->size) return false;
   buf->data[buf->tail] = c;
   buf->tail = (buf->tail + 1) % buf->size;
   buf->count++;
   return true;
}

static int buffer_pop(TTY_Buffer *buf)
{
   if (buf->count == 0) return -1;
   char c = buf->data[buf->head];
   buf->head = (buf->head + 1) % buf->size;
   buf->count--;
   return (int)(unsigned char)c;
}

static void buffer_clear(TTY_Buffer *buf)
{
   buf->head = 0;
   buf->tail = 0;
   buf->count = 0;
}

/*
 * Display helper functions
 */

static inline void mark_dirty(TTY_Device *tty, int row)
{
   if (row < 0 || row >= tty->rows) return;
   if (row < tty->dirty_start) tty->dirty_start = row;
   if (row > tty->dirty_end) tty->dirty_end = row;
}

static inline void mark_all_dirty(TTY_Device *tty)
{
   tty->dirty_start = 0;
   tty->dirty_end = tty->rows - 1;
}

static inline void reset_dirty(TTY_Device *tty)
{
   tty->dirty_start = tty->rows;
   tty->dirty_end = -1;
}

/*
 * scroll_up – shift rows 1…24 up by one, clear the last row.
 * Called whenever cursor_y reaches SCREEN_HEIGHT.
 */
static void scroll_up(TTY_Device *tty)
{
   /* Each row in screen_buf occupies TTY_MAX_COLS uint16_t in memory
    * regardless of the active mode width – use the real array stride. */
   memmove(tty->screen_buf[0], tty->screen_buf[1],
           (tty->rows - 1) * TTY_MAX_COLS * sizeof(uint16_t));
   memset(tty->screen_buf[tty->rows - 1], 0,
          TTY_MAX_COLS * sizeof(uint16_t));
   tty->cursor_y = tty->rows - 1;
}

/*
 * ANSI escape sequence handling
 */

static void handle_ansi_sgr(TTY_Device *tty)
{
   for (int i = 0; i < tty->ansi_param_count; i++)
   {
      int code = tty->ansi_params[i];

      if (code == 0)
      {
         tty->color = tty->default_color;
      }
      else if (code == 1)
      {
         tty->color |= 0x08; /* Bold */
      }
      else if (code >= 30 && code <= 37)
      {
         tty->color = (tty->color & 0xF0) | ansi_to_vga_fg[code - 30];
      }
      else if (code >= 40 && code <= 47)
      {
         tty->color = (tty->color & 0x0F) | ansi_to_vga_bg[code - 40];
      }
      else if (code >= 90 && code <= 97)
      {
         tty->color = (tty->color & 0xF0) | (ansi_to_vga_fg[code - 90] | 0x08);
      }
   }
}

static void handle_ansi_command(TTY_Device *tty, char cmd)
{
   int n = (tty->ansi_param_count > 0) ? tty->ansi_params[0] : 1;
   if (n < 1) n = 1;

   switch (cmd)
   {
   case 'A': /* Cursor up */
      tty->cursor_y -= n;
      if (tty->cursor_y < 0) tty->cursor_y = 0;
      break;
   case 'B': /* Cursor down */
      tty->cursor_y += n;
      if (tty->cursor_y >= tty->rows) tty->cursor_y = tty->rows - 1;
      break;
   case 'C': /* Cursor right */
      tty->cursor_x += n;
      if (tty->cursor_x >= tty->cols) tty->cursor_x = tty->cols - 1;
      break;
   case 'D': /* Cursor left */
      tty->cursor_x -= n;
      if (tty->cursor_x < 0) tty->cursor_x = 0;
      break;
   case 'H':
   case 'f': /* Cursor position */
   {
      int row = (tty->ansi_param_count > 0) ? tty->ansi_params[0] : 1;
      int col = (tty->ansi_param_count > 1) ? tty->ansi_params[1] : 1;
      if (row < 1) row = 1;
      if (col < 1) col = 1;
      tty->cursor_y = row - 1;
      tty->cursor_x = col - 1;
      if (tty->cursor_y >= tty->rows) tty->cursor_y = tty->rows - 1;
      if (tty->cursor_x >= tty->cols) tty->cursor_x = tty->cols - 1;
   }
   break;
   case 'J': /* Erase display */
      if (n == 2) TTY_ClearDevice(tty);
      break;
   case 'K': /* Erase line */
   {
      uint16_t *row = tty->screen_buf[tty->cursor_y];
      if (n == 0)
         memset(&row[tty->cursor_x], 0,
                (tty->cols - tty->cursor_x) * sizeof(uint16_t));
      else if (n == 1)
         memset(row, 0, (tty->cursor_x + 1) * sizeof(uint16_t));
      else
         memset(row, 0, tty->cols * sizeof(uint16_t));
      mark_dirty(tty, tty->cursor_y);
   }
   break;
   case 'm': /* SGR - colors */
      handle_ansi_sgr(tty);
      break;
   }
}

static bool process_ansi(TTY_Device *tty, char c)
{
   if (tty->ansi_state == 0)
   {
      if (c == '\x1B')
      {
         tty->ansi_state = 1;
         return true;
      }
      return false;
   }

   if (tty->ansi_state == 1)
   {
      if (c == '[')
      {
         tty->ansi_state = 2;
         tty->ansi_param_count = 0;
         tty->ansi_params[0] = 0;
         return true;
      }
      tty->ansi_state = 0;
      return true;
   }

   if (tty->ansi_state == 2)
   {
      if (c >= '0' && c <= '9')
      {
         tty->ansi_params[tty->ansi_param_count] =
             tty->ansi_params[tty->ansi_param_count] * 10 + (c - '0');
         return true;
      }
      else if (c == ';')
      {
         tty->ansi_param_count++;
         if (tty->ansi_param_count >= 16) tty->ansi_param_count = 15;
         tty->ansi_params[tty->ansi_param_count] = 0;
         return true;
      }
      else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
      {
         tty->ansi_param_count++;
         handle_ansi_command(tty, c);
         tty->ansi_state = 0;
         return true;
      }
      else if (c == '?')
      {
         return true; /* Skip DEC private mode prefix */
      }
      tty->ansi_state = 0;
      return true;
   }

   return false;
}

/*
 * Output character to screen buffer
 */

static void tty_output_char(TTY_Device *tty, char c)
{
   /* Handle ANSI sequences */
   if (process_ansi(tty, c)) return;

   /* ---- control characters ---- */
   if (c == '\n')
   {
      tty->cursor_x = 0;
      tty->cursor_y++;
      if (tty->cursor_y >= tty->rows) scroll_up(tty);
      mark_all_dirty(tty);
      TTY_Repaint(tty);
      return;
   }

   if (c == '\r')
   {
      tty->cursor_x = 0;
      return;
   }

   if (c == '\t')
   {
      int spaces = 4 - (tty->cursor_x % 4);
      for (int i = 0; i < spaces && tty->cursor_x < tty->cols; i++)
         tty_output_char(tty, ' ');
      return;
   }

   if (c == '\b')
   {
      if (tty->cursor_x > 0)
      {
         tty->cursor_x--;
         memmove(&tty->screen_buf[tty->cursor_y][tty->cursor_x],
                 &tty->screen_buf[tty->cursor_y][tty->cursor_x + 1],
                 (tty->cols - tty->cursor_x - 1) * sizeof(uint16_t));
         tty->screen_buf[tty->cursor_y][tty->cols - 1] = 0;
         mark_dirty(tty, tty->cursor_y);
      }
      TTY_Repaint(tty);
      return;
   }

   /* ---- printable character ---- */
   tty->screen_buf[tty->cursor_y][tty->cursor_x] =
       ((uint16_t)tty->color << 8) | (uint8_t)c;
   mark_dirty(tty, tty->cursor_y);
   tty->cursor_x++;

   if (tty->cursor_x >= tty->cols)
   {
      tty->cursor_x = 0;
      tty->cursor_y++;
      if (tty->cursor_y >= tty->rows) scroll_up(tty);
      mark_all_dirty(tty);
   }

   TTY_Repaint(tty);
}

/*
 * Line discipline - canonical mode input handling
 */

static void line_flush(TTY_Device *tty)
{
   /* Push line buffer contents to input buffer */
   for (uint32_t i = 0; i < tty->line_len; i++)
   {
      buffer_push(&tty->input, tty->line_buf[i]);
   }
   buffer_push(&tty->input, '\n');
   tty->line_len = 0;
   tty->line_pos = 0;
   tty->line_ready = true;
}

static void line_erase_char(TTY_Device *tty)
{
   if (tty->line_len > 0)
   {
      tty->line_len--;
      if (tty->line_pos > tty->line_len)
      {
         tty->line_pos = tty->line_len;
      }
      /* Echo backspace */
      if (TTY_IsEcho(tty))
      {
         tty_output_char(tty, '\b');
         tty_output_char(tty, ' ');
         tty_output_char(tty, '\b');
      }
   }
}

static void line_kill(TTY_Device *tty)
{
   /* Erase entire line */
   while (tty->line_len > 0)
   {
      line_erase_char(tty);
   }
}

static void line_add_char(TTY_Device *tty, char c)
{
   if (tty->line_len < TTY_LINE_SIZE - 1)
   {
      tty->line_buf[tty->line_len++] = c;
      tty->line_pos = tty->line_len;
      /* Echo if enabled */
      if (TTY_IsEcho(tty))
      {
         tty_output_char(tty, c);
      }
   }
}

/*
 * Public API Implementation
 */

void TTY_Initialize(void)
{
   if (g_TTYInitialized) return;

   /* Clear device array */
   for (int i = 0; i < TTY_MAX_DEVICES; i++)
   {
      g_TTYDevices[i] = NULL;
   }

   /* Create TTY0 (console) with static buffers */
   TTY_Device *tty0 = TTY_Create(0);
   if (tty0)
   {
      g_ActiveTTY = tty0;
   }

   g_TTYInitialized = true;
   TTY_Clear();
}

TTY_Device *TTY_Create(uint32_t id)
{
   if (id >= TTY_MAX_DEVICES) return NULL;
   if (g_TTYDevices[id] != NULL) return g_TTYDevices[id];

   /* kzalloc zeros the struct, including the embedded screen_buf */
   TTY_Device *tty = (TTY_Device *)kzalloc(sizeof(TTY_Device));
   if (!tty) return NULL;

   /* All auxiliary buffers live in BSS – no extra kmalloc */
   tty->display_buf = g_TTYDisplayBufs[id];
   buffer_init(&tty->input, g_TTYInputBufs[id], TTY_INPUT_SIZE);

   /* Initialize TTY state */
   tty->id            = id;
   tty->active        = true;
   tty->line_len      = 0;
   tty->line_pos      = 0;
   tty->line_ready    = false;
   tty->eof_pending   = false;
   tty->cursor_x      = 0;
   tty->cursor_y      = 0;
   tty->color         = 0x07;
   tty->default_color = 0x07;
   tty->flags         = TTY_DEFAULT_FLAGS;
   tty->ansi_state    = 0;
   tty->ansi_param_count = 0;
   tty->cols          = SCREEN_WIDTH;
   tty->rows          = SCREEN_HEIGHT;
   tty->dirty_start   = SCREEN_HEIGHT;
   tty->dirty_end     = -1;
   tty->bytes_read    = 0;
   tty->bytes_written = 0;
   /* screen_buf already zeroed by kzalloc */

   g_TTYDevices[id] = tty;
   return tty;
}

void TTY_Destroy(TTY_Device *tty)
{
   if (!tty) return;
   if (tty->id >= TTY_MAX_DEVICES) return;

   g_TTYDevices[tty->id] = NULL;

   if (g_ActiveTTY == tty)
      g_ActiveTTY = g_TTYDevices[0];

   /* display_buf and input.data point into BSS – nothing to free */
   free(tty);
}

TTY_Device *TTY_GetDevice(void) { return g_ActiveTTY; }

TTY_Device *TTY_GetDeviceById(uint32_t id)
{
   if (id >= TTY_MAX_DEVICES) return NULL;
   return g_TTYDevices[id];
}

void TTY_SetActive(TTY_Device *tty)
{
   if (!tty) return;
   g_ActiveTTY = tty;
   mark_all_dirty(tty);
   TTY_Repaint(tty);
}

/*
 * Input handling
 */

void TTY_InputChar(TTY_Device *tty, char c)
{
   if (!tty) return;

   /* Handle CR->NL mapping */
   if ((tty->flags & TTY_FLAG_ICRNL) && c == '\r')
   {
      c = '\n';
   }

   /* Handle signals */
   if (tty->flags & TTY_FLAG_ISIG)
   {
      if (c == TTY_CHAR_INTR)
      {
         /* TODO: Send SIGINT to foreground process */
         if (TTY_IsEcho(tty))
         {
            tty_output_char(tty, '^');
            tty_output_char(tty, 'C');
            tty_output_char(tty, '\n');
         }
         tty->line_len = 0;
         tty->line_pos = 0;
         return;
      }
      if (c == TTY_CHAR_EOF)
      {
         if (tty->line_len == 0)
         {
            tty->eof_pending = true;
            tty->line_ready = true;
         }
         else
         {
            line_flush(tty);
         }
         return;
      }
      if (c == TTY_CHAR_SUSP)
      {
         /* TODO: Send SIGTSTP to foreground process */
         if (TTY_IsEcho(tty))
         {
            tty_output_char(tty, '^');
            tty_output_char(tty, 'Z');
            tty_output_char(tty, '\n');
         }
         return;
      }
   }

   /* Canonical mode - line editing */
   if (TTY_IsCanonical(tty))
   {
      if (c == '\b' || c == TTY_CHAR_ERASE)
      {
         line_erase_char(tty);
         return;
      }
      if (c == TTY_CHAR_KILL)
      {
         line_kill(tty);
         return;
      }
      if (c == '\n')
      {
         if (TTY_IsEcho(tty))
         {
            tty_output_char(tty, '\n');
         }
         line_flush(tty);
         return;
      }

      /* Regular character */
      line_add_char(tty, c);
   }
   else
   {
      /* Raw mode - pass through directly */
      buffer_push(&tty->input, c);
      if (TTY_IsEcho(tty))
      {
         tty_output_char(tty, c);
      }
   }
}

void TTY_InputPush(char c)
{
   if (g_ActiveTTY)
   {
      TTY_InputChar(g_ActiveTTY, c);
   }
}

/*
 * Output handling
 */

void TTY_WriteChar(TTY_Device *tty, char c)
{
   if (!tty) return;

   /* Output processing */
   if (tty->flags & TTY_FLAG_OPOST)
   {
      if ((tty->flags & TTY_FLAG_ONLCR) && c == '\n')
      {
         tty_output_char(tty, '\r');
      }
   }

   tty_output_char(tty, c);
   tty->bytes_written++;
}

void TTY_Write(TTY_Device *tty, const char *data, size_t len)
{
   if (!tty || !data) return;

   for (size_t i = 0; i < len; i++)
   {
      TTY_WriteChar(tty, data[i]);
   }
}

void TTY_PutChar(char c)
{
   if (g_ActiveTTY)
   {
      TTY_WriteChar(g_ActiveTTY, c);
   }
}

void TTY_PutString(const char *s)
{
   if (!s) return;
   while (*s)
   {
      TTY_PutChar(*s++);
   }
}

/*
 * Reading
 */

int TTY_Read(TTY_Device *tty, char *buf, size_t count)
{
   if (!tty || !buf || count == 0) return 0;

   /* In canonical mode, wait for a complete line */
   if (TTY_IsCanonical(tty))
   {
      /* Wait for line_ready - in real system this would block */
      if (!tty->line_ready && tty->input.count == 0)
      {
         return 0; /* Would block */
      }
   }

   /* Check for EOF */
   if (tty->eof_pending && tty->input.count == 0)
   {
      tty->eof_pending = false;
      tty->line_ready = false;
      return 0; /* EOF */
   }

   size_t bytes_read = 0;
   while (bytes_read < count)
   {
      int c = buffer_pop(&tty->input);
      if (c < 0) break;
      buf[bytes_read++] = (char)c;

      /* In canonical mode, stop at newline */
      if (TTY_IsCanonical(tty) && c == '\n')
      {
         break;
      }
   }

   if (tty->input.count == 0)
   {
      tty->line_ready = false;
   }

   tty->bytes_read += bytes_read;
   return (int)bytes_read;
}

int TTY_ReadNonBlock(TTY_Device *tty, char *buf, size_t count)
{
   return TTY_Read(tty, buf, count);
}

int TTY_ReadChar(void)
{
   if (!g_ActiveTTY) return -1;

   char c;
   int n = TTY_Read(g_ActiveTTY, &c, 1);
   if (n <= 0) return -1;
   return (int)(unsigned char)c;
}

/*
 * Display control
 */

void TTY_ClearDevice(TTY_Device *tty)
{
   if (!tty) return;

   memset(tty->screen_buf, 0, sizeof(tty->screen_buf));
   tty->cursor_x = 0;
   tty->cursor_y = 0;
   mark_all_dirty(tty);
   TTY_Repaint(tty);
}

void TTY_Clear(void)
{
   if (g_ActiveTTY)
   {
      TTY_ClearDevice(g_ActiveTTY);
   }
}

void TTY_Scroll(int lines)
{
   (void)lines; /* No scrollback buffer – scroll is a no-op */
}

void TTY_Repaint(TTY_Device *tty)
{
   if (!tty) return;
   if (tty != g_ActiveTTY) return; /* Only repaint active TTY */

   const HAL_VideoOperations *vdev = g_HalVideoOperations;

   if (tty->dirty_start > tty->dirty_end)
   {
      if (vdev && vdev->SetCursor)
         vdev->SetCursor(tty->cursor_x, tty->cursor_y);
      return;
   }

   uint16_t blank = ((uint16_t)tty->color << 8) | ' ';

   /* Pack dirty rows into display_buf at tty->cols stride.
    * screen_buf rows are TTY_MAX_COLS wide in memory; we only copy tty->cols
    * cells per row so the display_buf is packed for the current VGA mode. */
   for (int row = tty->dirty_start; row <= tty->dirty_end; row++)
   {
      uint16_t *dest = &tty->display_buf[row * tty->cols];
      uint16_t *src  = tty->screen_buf[row]; /* stride TTY_MAX_COLS in memory */
      for (int col = 0; col < tty->cols; col++)
         dest[col] = src[col] ? src[col] : blank;
   }

   if (vdev && vdev->UpdateBuffer) vdev->UpdateBuffer(tty->display_buf);

   reset_dirty(tty);
   if (vdev && vdev->SetCursor) vdev->SetCursor(tty->cursor_x, tty->cursor_y);
}

void TTY_Flush(TTY_Device *tty)
{
   if (!tty) return;
   mark_all_dirty(tty);
   TTY_Repaint(tty);
}

/*
 * Cursor control
 */

void TTY_SetCursor(TTY_Device *tty, int x, int y)
{
   if (!tty) return;

   if (x < 0) x = 0;
   if (x >= tty->cols) x = tty->cols - 1;
   if (y < 0) y = 0;
   if (y >= tty->rows) y = tty->rows - 1;

   tty->cursor_x = x;
   tty->cursor_y = y;

   if (tty == g_ActiveTTY)
   {
      const HAL_VideoOperations *vdev = g_HalVideoOperations;
      if (vdev && vdev->SetCursor) vdev->SetCursor(x, y);
   }
}

void TTY_GetCursor(TTY_Device *tty, int *x, int *y)
{
   if (!tty) return;
   if (x) *x = tty->cursor_x;
   if (y) *y = tty->cursor_y;
}

/*
 * Attributes
 */

void TTY_SetColor(uint8_t color)
{
   if (g_ActiveTTY)
   {
      g_ActiveTTY->color = color;
   }
}

void TTY_SetFlags(TTY_Device *tty, uint32_t flags)
{
   if (!tty) return;
   tty->flags = flags;
}

uint32_t TTY_GetFlags(TTY_Device *tty)
{
   if (!tty) return 0;
   return tty->flags;
}

/*
 * Query functions
 */

int TTY_GetVisibleLineLength(int y)
{
   TTY_Device *tty = g_ActiveTTY;
   if (!tty || y < 0 || y >= tty->rows) return 0;
   int len = 0;
   while (len < tty->cols && (tty->screen_buf[y][len] & 0xFF)) len++;
   return len;
}

int TTY_GetMaxScroll(void) { return 0; /* no scrollback */ }

uint32_t TTY_GetVisibleStart(void) { return 0; /* always at row 0 */ }

/*
 * tty_reflow_shrink – wrap overflow content to the next row when cols shrinks.
 *
 * For every row that has non-empty cells at column positions >= new_cols, the
 * trailing content is prepended to the beginning of the following row (the
 * following row's existing cells shift right to make room).  Overflow that
 * cannot fit because the next row is already full is discarded – this is the
 * same behaviour used by common terminal emulators.
 *
 * The last row cannot overflow anywhere, so its excess is simply cleared.
 *
 * Called before tty->cols is updated so old_cols still reflects the previous
 * screen width (and therefore the region of screen_buf that was meaningful).
 */
static void tty_reflow_shrink(TTY_Device *tty, int old_cols, int new_cols)
{
   int rows = tty->rows;

   for (int r = 0; r < rows; r++)
   {
      uint16_t *row = tty->screen_buf[r];
      uint16_t overflow[TTY_MAX_COLS];

      /* Find the last non-empty cell in the overflow zone [new_cols, old_cols). */
      int ov_len = 0;
      for (int c = old_cols - 1; c >= new_cols; c--)
      {
         if (row[c] != 0) { ov_len = c - new_cols + 1; break; }
      }
      if (ov_len == 0) continue; /* this row fits – nothing to wrap */

      /* Snapshot overflow before clearing source cells. */
      memcpy(overflow, &row[new_cols], (size_t)ov_len * sizeof(uint16_t));

      /* Erase overflow from current row. */
      memset(&row[new_cols], 0, (size_t)(old_cols - new_cols) * sizeof(uint16_t));

      if (r + 1 >= rows) break; /* last row – overflow is discarded */

      /* Determine how many cells are already used in the next row. */
      uint16_t *next = tty->screen_buf[r + 1];
      int next_used = 0;
      for (int c = new_cols - 1; c >= 0; c--)
         if (next[c] != 0) { next_used = c + 1; break; }

      /* Clamp overflow to available space. */
      int can_fit = new_cols - next_used;
      if (can_fit <= 0) continue; /* next row is full – discard overflow */
      if (ov_len > can_fit) ov_len = can_fit;

      /* Shift next row's content right by ov_len to make room. */
      memmove(&next[ov_len], &next[0], (size_t)next_used * sizeof(uint16_t));

      /* Prepend overflow. */
      memcpy(&next[0], overflow, (size_t)ov_len * sizeof(uint16_t));
   }
}

/*
 * TTY_SetVideoMode – switch the VGA text mode and re-render all active TTYs.
 *
 * Order of operations:
 *  1. Ask the HAL to reprogram the CRTC (returns -1 for unsupported modes).
 *  2. If cols is shrinking, reflow screen content before applying new width.
 *  3. Update cols/rows on every created TTY device.
 *  4. Clamp any cursor that now sits outside the new visible area.
 *  5. Force a full repaint of the active TTY so VGA VRAM is coherent.
 */
int TTY_SetVideoMode(int cols, int rows)
{
   const HAL_VideoOperations *vdev = g_HalVideoOperations;
   if (!vdev || !vdev->SetDisplaySize) return -1;

   /* 1. Reprogram hardware – bail early if the mode is not supported. */
   if (vdev->SetDisplaySize(cols, rows) != 0) return -1;

   /* 2, 3 & 4. Update every active device. */
   for (int i = 0; i < TTY_MAX_DEVICES; i++)
   {
      TTY_Device *tty = g_TTYDevices[i];
      if (!tty) continue;

      int old_cols = tty->cols;

      /* Reflow before narrowing so overflow is not silently discarded. */
      if (cols < old_cols)
         tty_reflow_shrink(tty, old_cols, cols);

      tty->cols = cols;
      tty->rows = rows;

      /* Clamp cursor into the new visible area. */
      if (tty->cursor_x >= cols) tty->cursor_x = cols - 1;
      if (tty->cursor_y >= rows) tty->cursor_y = rows - 1;

      /* Reset dirty tracking to the new row count. */
      tty->dirty_start = rows;
      tty->dirty_end   = -1;
   }

   /* 5. Full repaint of the active TTY – rebuilds display_buf at new stride. */
   if (g_ActiveTTY)
   {
      mark_all_dirty(g_ActiveTTY);
      TTY_Repaint(g_ActiveTTY);
   }

   return 0;
}

/*
 * Devfs operations
 */

uint32_t TTY_DevfsRead(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                       void *buffer)
{
   (void)offset;
   if (!buffer || size == 0) return 0;

   /* Get TTY from node's private_data, or use active TTY */
   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) tty = g_ActiveTTY;
   if (!tty) return 0;

   return (uint32_t)TTY_Read(tty, (char *)buffer, size);
}

uint32_t TTY_DevfsWrite(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                        const void *buffer)
{
   (void)offset;
   if (!buffer || size == 0) return 0;

   /* Get TTY from node's private_data, or use active TTY */
   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) tty = g_ActiveTTY;
   if (!tty) return 0;

   TTY_Write(tty, (const char *)buffer, size);
   return size;
}

int TTY_DevfsIoctl(DEVFS_DeviceNode *node, uint32_t cmd, void *arg)
{
   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) return -1;

   switch (cmd)
   {
   case TTY_IOCTL_GETFLAGS:
      if (arg) *(uint32_t *)arg = tty->flags;
      return 0;
   case TTY_IOCTL_SETFLAGS:
      if (arg) tty->flags = *(uint32_t *)arg;
      return 0;
   case TTY_IOCTL_FLUSH:
      buffer_clear(&tty->input);
      tty->line_len = 0;
      tty->line_pos = 0;
      tty->line_ready = false;
      return 0;
   case TTY_IOCTL_GETSIZE:
      if (arg)
      {
         uint16_t *size = (uint16_t *)arg;
         size[0] = (uint16_t)tty->cols;
         size[1] = (uint16_t)tty->rows;
      }
      return 0;
   default:
      return -1;
   }
}
