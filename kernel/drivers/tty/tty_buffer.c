// SPDX-License-Identifier: GPL-3.0-only

/* Screen-backed TTY implementation derived from buffer_text.c
 * Provides a ring buffer and repainting logic usable by a tty device.
 */

#include "tty.h"
#include <hal/tty.h>
/* Header removed by request; declare extern symbol provided by tty_color.c */
extern void tty_color_apply_sgr(uint8_t *color, int *params, int pcount);
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <stdint.h>

/* Keep constants same as buffer_text */
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

typedef struct TTY_Device
{
   uint8_t color;
   /* lightweight ring buffer of lines */
   char (*buffer)[SCREEN_WIDTH];
   uint8_t (*color_buffer)[SCREEN_WIDTH];
   uint32_t head;
   uint32_t lines_used;
   int cursor_x;
   int cursor_y;
   uint32_t scroll;
   /* last-flushed visible snapshot for dirty writes (allocated separately)
    * Use pointers here to avoid enlarging the TTY_Device struct layout.
    */
   char (*last_screen)[SCREEN_WIDTH];
   uint8_t (*last_color)[SCREEN_WIDTH];
   int last_visible_start;
   /* simple input queue for reads */
   char input_buf[256];
   uint32_t input_head;
   uint32_t input_tail;
   /* partial CSI parser state */
   char esc_buf[64];
   int esc_len;
} TTY_Device;

static TTY_Device *g_tty = NULL;

bool tty_buffer_init(void)
{
   if (g_tty) return true;
   g_tty = (TTY_Device *)kmalloc(sizeof(TTY_Device));
   if (!g_tty) return false;
   memset(g_tty, 0, sizeof(TTY_Device));
   g_tty->color = 0x7;
   /* allocate buffers in kernel heap instead of fixed VGA area to simplify */
   g_tty->buffer = (char (*)[SCREEN_WIDTH])kmalloc(BUFFER_LINES * SCREEN_WIDTH);
   g_tty->color_buffer = (uint8_t (*)[SCREEN_WIDTH])kmalloc(BUFFER_LINES * SCREEN_WIDTH);
   if (!g_tty->buffer || !g_tty->color_buffer)
   {
      if (g_tty->buffer) free(g_tty->buffer);
      if (g_tty->color_buffer) free(g_tty->color_buffer);
      free(g_tty);
      g_tty = NULL;
      return false;
   }
   /* clear */
   memset(g_tty->buffer, 0, BUFFER_LINES * SCREEN_WIDTH);
   memset(g_tty->color_buffer, 0x7, BUFFER_LINES * SCREEN_WIDTH);
   g_tty->head = 0;
   g_tty->lines_used = 0;
   g_tty->cursor_x = 0;
   g_tty->cursor_y = 0;
   g_tty->scroll = 0;
   g_tty->esc_len = 0;
   g_tty->last_visible_start = -1;
   /* allocate last-screen snapshot buffers */
   g_tty->last_screen = (char (*)[SCREEN_WIDTH])kmalloc(SCREEN_HEIGHT * SCREEN_WIDTH);
   g_tty->last_color = (uint8_t (*)[SCREEN_WIDTH])kmalloc(SCREEN_HEIGHT * SCREEN_WIDTH);
   if (g_tty->last_screen && g_tty->last_color)
   {
      for (int y = 0; y < SCREEN_HEIGHT; ++y)
         for (int x = 0; x < SCREEN_WIDTH; ++x)
         {
            g_tty->last_screen[y][x] = 0;
            g_tty->last_color[y][x] = 0xFF;
         }
   }
   return true;
}

TTY_Device *tty_buffer_get_device(void) { return g_tty; }

static void ensure_line_exists(TTY_Device *d)
{
   if (d->lines_used == 0)
   {
      d->lines_used = 1;
      d->head = 0;
      for (int i = 0; i < SCREEN_WIDTH; i++) d->buffer[0][i] = '\0';
   }
}

static void push_newline_at_tail(TTY_Device *d)
{
   if (d->lines_used < BUFFER_LINES)
   {
      uint32_t idx = (d->head + d->lines_used) % BUFFER_LINES;
      memset(d->buffer[idx], 0, SCREEN_WIDTH);
      memset(d->color_buffer[idx], 0x7, SCREEN_WIDTH);
      d->lines_used++;
   }
   else
   {
      d->head = (d->head + 1) % BUFFER_LINES;
      uint32_t idx = (d->head + d->lines_used - 1) % BUFFER_LINES;
      memset(d->buffer[idx], 0, SCREEN_WIDTH);
      memset(d->color_buffer[idx], 0x7, SCREEN_WIDTH);
   }
}

void tty_buffer_flush(TTY_Device *d)
{
   if (!d || !g_HalTtyOperations) return;
   int visible_start = (d->lines_used > SCREEN_HEIGHT) ? (d->lines_used - SCREEN_HEIGHT) : 0;
   /* If visible window has changed (scroll), invalidate last snapshot to force full repaint */
   if (d->last_visible_start != visible_start)
   {
      d->last_visible_start = visible_start;
      for (int yy = 0; yy < SCREEN_HEIGHT; ++yy)
         for (int xx = 0; xx < SCREEN_WIDTH; ++xx)
            d->last_color[yy][xx] = 0xFF;
   }

   for (int y = 0; y < SCREEN_HEIGHT; y++)
   {
      uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)y;
      if (rel_pos >= d->lines_used)
      {
         /* Clear lines beyond buffer content */
         for (int x = 0; x < SCREEN_WIDTH; x++)
         {
            char want = ' ';
            uint8_t want_col = 0x7;
            if ((uint8_t)d->last_screen[y][x] != (uint8_t)want || d->last_color[y][x] != want_col)
            {
               g_HalTtyOperations->putc(x, y, want, want_col);
               d->last_screen[y][x] = want;
               d->last_color[y][x] = want_col;
            }
         }
      }
      else
      {
         uint32_t idx = (d->head + rel_pos) % BUFFER_LINES;
         for (int x = 0; x < SCREEN_WIDTH; x++)
         {
            char c = d->buffer[idx][x];
            if (c == 0) c = ' ';
            uint8_t col = d->color_buffer[idx][x];
            if ((uint8_t)d->last_screen[y][x] != (uint8_t)c || d->last_color[y][x] != col)
            {
               g_HalTtyOperations->putc(x, y, c, col);
               d->last_screen[y][x] = c;
               d->last_color[y][x] = col;
            }
         }
      }
   }
   g_HalTtyOperations->set_cursor(d->cursor_x, d->cursor_y);
}

int tty_buffer_write(TTY_Device *d, const void *buf, uint32_t count)
{
   if (!d || !buf || count == 0) return 0;

   /* If we have a pending partial CSI sequence from a previous write,
    * prepend it to the incoming buffer (up to a safe limit) so the
    * parser can complete across calls.
    */
   const char *s = (const char *)buf;
   char *tmp_buf = NULL;
   bool using_tmp = false;
   if (d->esc_len > 0)
   {
      size_t need = (size_t)d->esc_len + (size_t)count;
      /* try to allocate a temporary buffer to prepend the partial CSI */
      tmp_buf = (char *)kmalloc(need);
      if (tmp_buf)
      {
         memcpy(tmp_buf, d->esc_buf, d->esc_len);
         memcpy(tmp_buf + d->esc_len, s, count);
         s = tmp_buf;
         count = (uint32_t)need;
         d->esc_len = 0;
         using_tmp = true;
      }
      else
      {
         /* allocation failed: keep existing esc_buf and proceed without prepending */
      }
   }

   for (uint32_t i = 0; i < count; i++)
   {
      char c = s[i];
      /* Handle ANSI escape sequences (CSI) beginning with ESC '[' */
      if (c == '\x1B')
      {
         /* If ESC is the last byte in this write, save it and wait for the
          * following bytes in the next call (so sequences split across
          * writes are handled). */
         if (i + 1 >= count)
         {
            if ((int)sizeof(d->esc_buf) > 0)
            {
               d->esc_buf[0] = '\x1B';
               d->esc_len = 1;
            }
            if (using_tmp && tmp_buf) free(tmp_buf);
            return (int)count;
         }
         /* If next byte is not '[' we don't support that escape here; skip it */
         if (s[i+1] != '[') { continue; }

         /* parse parameters until final byte (alpha) */
         uint32_t j = i + 2;
         int params[8];
         int pcount = 0;
         int cur = 0;
         bool has_num = false;
         while (j < count)
         {
            char ch = s[j];
            if (ch >= '0' && ch <= '9')
            {
               cur = cur * 10 + (ch - '0');
               has_num = true;
            }
            else if (ch == ';')
            {
               if (pcount < 8) params[pcount++] = has_num ? cur : 0;
               cur = 0; has_num = false;
            }
            else
            {
               /* final byte */
               if (pcount < 8) params[pcount++] = has_num ? cur : 0;
               /* handle common terminators */
               switch (ch)
               {
               case 'm': /* SGR - color */
                  /* delegate detailed SGR handling to tty_color module */
                  tty_color_apply_sgr(&d->color, params, pcount);
                  /* Apply color change immediately so VGA reflects new SGR state. */
                  if (g_HalTtyOperations) tty_buffer_flush(d);
                  break;

               case 'H': /* Cursor position: row;col */
               case 'f':
                  {
                     int row = (pcount >= 1) ? params[0] : 1;
                     int col = (pcount >= 2) ? params[1] : 1;
                     /* convert 1-based to 0-based and clamp */
                     if (row < 1) row = 1; if (col < 1) col = 1;
                     d->cursor_y = row - 1;
                     d->cursor_x = col - 1;
                  }
                  break;

               case 'J': /* Erase in display */
                  /* support 2 (clear whole screen) */
                     if (pcount == 0 || params[0] == 0)
                  {
                     /* clear from cursor to end - approximate by clearing whole screen */
                     for (int yy = d->cursor_y; yy < SCREEN_HEIGHT; ++yy)
                        for (int xx = 0; xx < SCREEN_WIDTH; ++xx) g_HalTtyOperations->putc(xx, yy, ' ', 0x7);
                  }
                  else if (params[0] == 2)
                  {
                     for (int yy = 0; yy < SCREEN_HEIGHT; ++yy)
                        for (int xx = 0; xx < SCREEN_WIDTH; ++xx) g_HalTtyOperations->putc(xx, yy, ' ', 0x7);
                     d->cursor_x = 0; d->cursor_y = 0;
                  }
                  break;

               case 'K': /* Erase in line */
                  if (pcount == 0 || params[0] == 0)
                  {
                     for (int xx = d->cursor_x; xx < SCREEN_WIDTH; ++xx) g_HalTtyOperations->putc(xx, d->cursor_y, ' ', 0x7);
                  }
                  else if (params[0] == 2)
                  {
                     for (int xx = 0; xx < SCREEN_WIDTH; ++xx) g_HalTtyOperations->putc(xx, d->cursor_y, ' ', 0x7);
                  }
                  break;

               case 'A': /* CUU - up */
                  {
                     int n = (pcount >=1) ? params[0] : 1;
                     d->cursor_y -= n; if (d->cursor_y < 0) d->cursor_y = 0;
                  }
                  break;
               case 'B': /* CUD - down */
                  {
                     int n = (pcount >=1) ? params[0] : 1;
                     d->cursor_y += n; if (d->cursor_y >= SCREEN_HEIGHT) d->cursor_y = SCREEN_HEIGHT-1;
                  }
                  break;
               case 'C': /* CUF - forward */
                  {
                     int n = (pcount >=1) ? params[0] : 1;
                     d->cursor_x += n; if (d->cursor_x >= SCREEN_WIDTH) d->cursor_x = SCREEN_WIDTH-1;
                  }
                  break;
               case 'D': /* CUB - back */
                  {
                     int n = (pcount >=1) ? params[0] : 1;
                     d->cursor_x -= n; if (d->cursor_x < 0) d->cursor_x = 0;
                  }
                  break;

               default:
                  break;
               }

               /* advance i to consumed bytes */
               i = j;
               break;
            }
            j++;
         }

         /* If we reached the end of the provided buffer without finding a
          * final byte for the CSI sequence, save the partial sequence so it
          * can be completed on the next write call.
          */
         if (j >= count)
         {
            int rem = (int)(count - i);
            if (rem > (int)sizeof(d->esc_buf)) rem = (int)sizeof(d->esc_buf);
            memcpy(d->esc_buf, s + i, rem);
            d->esc_len = rem;
            /* we've consumed the provided input; return as success */
            if (using_tmp && tmp_buf) free(tmp_buf);
            return (int)count;
         }
         continue;
      }
      if (c == '\n')
      {
         d->scroll = 0;
         ensure_line_exists(d);
         int visible_start = (d->lines_used > SCREEN_HEIGHT) ? (d->lines_used - SCREEN_HEIGHT) : 0;
         uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)d->cursor_y;
         if (rel_pos >= d->lines_used) rel_pos = d->lines_used - 1;
         uint32_t idx = (d->head + rel_pos) % BUFFER_LINES;
         /* Clear remainder of the current line so shorter subsequent lines do not
          * leave trailing characters from previous longer lines. */
         for (int k = d->cursor_x; k < SCREEN_WIDTH; ++k)
         {
            d->buffer[idx][k] = 0;
            d->color_buffer[idx][k] = 0x7;
         }
         d->cursor_x = 0;
         if (d->cursor_y < SCREEN_HEIGHT - 1)
         {
            d->cursor_y++;
         }
         else
         {
            /* We're at the bottom of the visible screen: append a new line at
             * the buffer tail to implement scrolling, and keep cursor on the
             * bottom row. The flush will compute the visible window from the
             * tail so the display scrolls up. */
            push_newline_at_tail(d);
         }
      }
      else if (c == '\r')
      {
         d->cursor_x = 0;
      }
      else if (c == '\t')
      {
         int n = 4 - (d->cursor_x % 4);
         for (int j = 0; j < n; j++) tty_buffer_write(d, " ", 1);
      }
      else if (c == '\b')
      {
         if (d->cursor_x > 0)
         {
            int visible_start = (d->lines_used > SCREEN_HEIGHT) ? (d->lines_used - SCREEN_HEIGHT) : 0;
            uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)d->cursor_y;
            uint32_t idx = (d->head + rel_pos) % BUFFER_LINES;
            if (d->cursor_x > 0) { d->cursor_x--; d->buffer[idx][d->cursor_x] = 0; }
         }
      }
      else
      {
         ensure_line_exists(d);
         int visible_start = (d->lines_used > SCREEN_HEIGHT) ? (d->lines_used - SCREEN_HEIGHT) : 0;
         uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)d->cursor_y;
         while (rel_pos >= d->lines_used) push_newline_at_tail(d);
         uint32_t idx = (d->head + rel_pos) % BUFFER_LINES;
         int len = 0; while (len < SCREEN_WIDTH && d->buffer[idx][len]) len++;
         if (d->cursor_x > len) d->cursor_x = len;
         if (d->cursor_x < SCREEN_WIDTH)
         {
            d->buffer[idx][d->cursor_x] = c;
            d->color_buffer[idx][d->cursor_x] = d->color;
         }
         d->cursor_x++;
         if (d->cursor_x >= SCREEN_WIDTH) { d->cursor_x = 0; if (d->cursor_y < SCREEN_HEIGHT - 1) d->cursor_y++; else push_newline_at_tail(d); }
      }
   }
   tty_buffer_flush(d);
   if (using_tmp && tmp_buf) free(tmp_buf);
   return (int)count;
}

int tty_buffer_read(TTY_Device *d, void *buf, uint32_t count)
{
   if (!d || !buf || count == 0) return 0;
   uint32_t available = 0;
   if (d->input_tail >= d->input_head)
      available = d->input_tail - d->input_head;
   else
      available = (uint32_t)(sizeof(d->input_buf) - d->input_head + d->input_tail);

   if (available == 0) return 0; /* non-blocking */

   uint32_t to_read = (count < available) ? count : available;
   char *out = (char *)buf;
   for (uint32_t i = 0; i < to_read; i++)
   {
      out[i] = d->input_buf[d->input_head];
      d->input_head = (d->input_head + 1) % sizeof(d->input_buf);
   }
   return (int)to_read;
}

/* Stream-aware write wrapper: set a default stream color (e.g., stderr in red)
 * and call the regular write path. This allows callers to request stderr and
 * get default red formatting for non-ANSI content while still honoring any
 * explicit SGR sequences in the buffer.
 */
int tty_buffer_write_stream(TTY_Device *d, int stream, const void *buf, uint32_t count)
{
   if (!d) return 0;
   /* If the buffer contains explicit ANSI escapes, let the parser
    * update d->color and persist that change. Only apply the stderr
    * default color when there are no escape sequences in the provided
    * buffer (so plain stderr text appears in bright red by default).
    */
   bool has_escape = false;
   for (uint32_t i = 0; i < count; ++i)
   {
      if (((const char *)buf)[i] == '\x1B') { has_escape = true; break; }
   }
   uint8_t old = d->color;
   bool applied_default = false;
   if (stream == TTY_STREAM_STDERR && !has_escape)
   {
      d->color = 0x0C; /* bright red default */
      applied_default = true;
   }
   int ret = tty_buffer_write(d, buf, count);
   if (applied_default)
      d->color = old;
   return ret;
}

/* Push a character into the input queue (called by keyboard driver). */
int tty_buffer_input_push(char c)
{
   if (!g_tty) return -1;
   TTY_Device *d = g_tty;
   uint32_t next = (d->input_tail + 1) % sizeof(d->input_buf);
   if (next == d->input_head)
   {
      /* buffer full */
      return -1;
   }
   d->input_buf[d->input_tail] = c;
   d->input_tail = next;
   return 0;
}

void tty_buffer_set_color(TTY_Device *d, uint8_t color) { if (d) d->color = color; }
void tty_buffer_set_cursor(TTY_Device *d, int x, int y) { if (!d) return; d->cursor_x = x; d->cursor_y = y; if (g_HalTtyOperations) g_HalTtyOperations->set_cursor(x, y); }
void tty_buffer_get_cursor(TTY_Device *d, int *x, int *y) { if (!d) { if (x) *x = 0; if (y) *y = 0; return; } if (x) *x = d->cursor_x; if (y) *y = d->cursor_y; }
void tty_buffer_clear(TTY_Device *d)
{
   if (!d) return;
   memset(d->buffer, 0, BUFFER_LINES * SCREEN_WIDTH);
   memset(d->color_buffer, 0x7, BUFFER_LINES * SCREEN_WIDTH);
   d->head = 0; d->lines_used = 0; d->cursor_x = 0; d->cursor_y = 0; d->scroll = 0;
   if (g_HalTtyOperations) g_HalTtyOperations->clear();
}

