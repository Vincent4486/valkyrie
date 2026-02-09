// SPDX-License-Identifier: GPL-3.0-only

#include "tty.h"
#include <fs/devfs/devfs.h>
#include <hal/tty.h>
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

/* Memory locations for display buffers */
#define BUFFER_BASE_ADDR   0x00110000
#define BUFFER_DISP_ADDR   0xB8000

/* External cursor function from stdio */
extern void setcursor(int x, int y);

/* TTY device array */
static TTY_Device *g_TTYDevices[TTY_MAX_DEVICES];
static TTY_Device *g_ActiveTTY = NULL;
static bool g_TTYInitialized = false;

/* Static buffers for TTY0 (console) - avoid early kmalloc */
static char g_TTY0ScreenBuf[TTY_SCROLLBACK][SCREEN_WIDTH];
static char g_TTY0InputBuf[TTY_INPUT_SIZE];
static uint16_t *g_TTY0DisplayBuf = (uint16_t *)BUFFER_DISP_ADDR;

/* ANSI color mapping */
static const uint8_t ansi_to_vga_fg[] = {
   0x0, 0x4, 0x2, 0x6, 0x1, 0x5, 0x3, 0x7
};
static const uint8_t ansi_to_vga_bg[] = {
   0x00, 0x40, 0x20, 0x60, 0x10, 0x50, 0x30, 0x70
};

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
   if (row < 0 || row >= SCREEN_HEIGHT) return;
   if (row < tty->dirty_start) tty->dirty_start = row;
   if (row > tty->dirty_end) tty->dirty_end = row;
}

static inline void mark_all_dirty(TTY_Device *tty)
{
   tty->dirty_start = 0;
   tty->dirty_end = SCREEN_HEIGHT - 1;
}

static inline void reset_dirty(TTY_Device *tty)
{
   tty->dirty_start = SCREEN_HEIGHT;
   tty->dirty_end = -1;
}

/* Compute visible start line index */
static int compute_visible_start(TTY_Device *tty)
{
   int base = (tty->buf_lines > SCREEN_HEIGHT) 
              ? (int)(tty->buf_lines - SCREEN_HEIGHT) : 0;
   int start = base - (int)tty->scroll_offset;
   if (start < 0) start = 0;
   return start;
}

/* Ensure at least one line exists in buffer */
static void ensure_line_exists(TTY_Device *tty)
{
   if (tty->buf_lines == 0) {
      tty->buf_lines = 1;
      tty->buf_head = 0;
      memset(tty->screen_buf[0], 0, SCREEN_WIDTH);
   }
}

/* Add a new line at the bottom of the buffer */
static void push_newline(TTY_Device *tty)
{
   if (tty->buf_lines < TTY_SCROLLBACK) {
      uint32_t idx = (tty->buf_head + tty->buf_lines) % TTY_SCROLLBACK;
      memset(tty->screen_buf[idx], 0, SCREEN_WIDTH);
      tty->buf_lines++;
   } else {
      /* Buffer full, drop oldest line */
      tty->buf_head = (tty->buf_head + 1) % TTY_SCROLLBACK;
      uint32_t idx = (tty->buf_head + tty->buf_lines - 1) % TTY_SCROLLBACK;
      memset(tty->screen_buf[idx], 0, SCREEN_WIDTH);
   }
   
   /* Update cursor for new line */
   if (tty->scroll_offset == 0) {
      if (tty->buf_lines >= SCREEN_HEIGHT)
         tty->cursor_y = SCREEN_HEIGHT - 1;
      else
         tty->cursor_y = (int)tty->buf_lines - 1;
   }
}

/*
 * ANSI escape sequence handling
 */

static void handle_ansi_sgr(TTY_Device *tty)
{
   for (int i = 0; i < tty->ansi_param_count; i++) {
      int code = tty->ansi_params[i];
      
      if (code == 0) {
         tty->color = tty->default_color;
      } else if (code == 1) {
         tty->color |= 0x08;  /* Bold */
      } else if (code >= 30 && code <= 37) {
         tty->color = (tty->color & 0xF0) | ansi_to_vga_fg[code - 30];
      } else if (code >= 40 && code <= 47) {
         tty->color = (tty->color & 0x0F) | ansi_to_vga_bg[code - 40];
      } else if (code >= 90 && code <= 97) {
         tty->color = (tty->color & 0xF0) | (ansi_to_vga_fg[code - 90] | 0x08);
      }
   }
}

static void handle_ansi_command(TTY_Device *tty, char cmd)
{
   int n = (tty->ansi_param_count > 0) ? tty->ansi_params[0] : 1;
   if (n < 1) n = 1;
   
   switch (cmd) {
      case 'A': /* Cursor up */
         tty->cursor_y -= n;
         if (tty->cursor_y < 0) tty->cursor_y = 0;
         break;
      case 'B': /* Cursor down */
         tty->cursor_y += n;
         if (tty->cursor_y >= SCREEN_HEIGHT) tty->cursor_y = SCREEN_HEIGHT - 1;
         break;
      case 'C': /* Cursor right */
         tty->cursor_x += n;
         if (tty->cursor_x >= SCREEN_WIDTH) tty->cursor_x = SCREEN_WIDTH - 1;
         break;
      case 'D': /* Cursor left */
         tty->cursor_x -= n;
         if (tty->cursor_x < 0) tty->cursor_x = 0;
         break;
      case 'H': case 'f': /* Cursor position */
         {
            int row = (tty->ansi_param_count > 0) ? tty->ansi_params[0] : 1;
            int col = (tty->ansi_param_count > 1) ? tty->ansi_params[1] : 1;
            if (row < 1) row = 1;
            if (col < 1) col = 1;
            tty->cursor_y = row - 1;
            tty->cursor_x = col - 1;
            if (tty->cursor_y >= SCREEN_HEIGHT) tty->cursor_y = SCREEN_HEIGHT - 1;
            if (tty->cursor_x >= SCREEN_WIDTH) tty->cursor_x = SCREEN_WIDTH - 1;
         }
         break;
      case 'J': /* Erase display */
         if (n == 2) TTY_ClearDevice(tty);
         break;
      case 'K': /* Erase line */
         {
            int start = compute_visible_start(tty);
            uint32_t rel = (uint32_t)start + (uint32_t)tty->cursor_y;
            if (rel < tty->buf_lines) {
               uint32_t idx = (tty->buf_head + rel) % TTY_SCROLLBACK;
               if (n == 0) {
                  memset(&tty->screen_buf[idx][tty->cursor_x], 0, 
                         SCREEN_WIDTH - tty->cursor_x);
               } else if (n == 1) {
                  memset(tty->screen_buf[idx], 0, tty->cursor_x + 1);
               } else {
                  memset(tty->screen_buf[idx], 0, SCREEN_WIDTH);
               }
               mark_dirty(tty, tty->cursor_y);
            }
         }
         break;
      case 'm': /* SGR - colors */
         handle_ansi_sgr(tty);
         break;
   }
}

static bool process_ansi(TTY_Device *tty, char c)
{
   if (tty->ansi_state == 0) {
      if (c == '\x1B') {
         tty->ansi_state = 1;
         return true;
      }
      return false;
   }
   
   if (tty->ansi_state == 1) {
      if (c == '[') {
         tty->ansi_state = 2;
         tty->ansi_param_count = 0;
         tty->ansi_params[0] = 0;
         return true;
      }
      tty->ansi_state = 0;
      return true;
   }
   
   if (tty->ansi_state == 2) {
      if (c >= '0' && c <= '9') {
         tty->ansi_params[tty->ansi_param_count] = 
            tty->ansi_params[tty->ansi_param_count] * 10 + (c - '0');
         return true;
      } else if (c == ';') {
         tty->ansi_param_count++;
         if (tty->ansi_param_count >= 16) tty->ansi_param_count = 15;
         tty->ansi_params[tty->ansi_param_count] = 0;
         return true;
      } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
         tty->ansi_param_count++;
         handle_ansi_command(tty, c);
         tty->ansi_state = 0;
         return true;
      } else if (c == '?') {
         return true;  /* Skip DEC private mode prefix */
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
   
   ensure_line_exists(tty);
   
   int visible_start = compute_visible_start(tty);
   uint32_t rel = (uint32_t)visible_start + (uint32_t)tty->cursor_y;
   
   /* Handle control characters */
   if (c == '\n') {
      tty->scroll_offset = 0;
      push_newline(tty);
      tty->cursor_x = 0;
      mark_all_dirty(tty);
      TTY_Repaint(tty);
      return;
   }
   
   if (c == '\r') {
      tty->cursor_x = 0;
      return;
   }
   
   if (c == '\t') {
      int n = 4 - (tty->cursor_x % 4);
      for (int i = 0; i < n && tty->cursor_x < SCREEN_WIDTH; i++) {
         tty_output_char(tty, ' ');
      }
      return;
   }
   
   if (c == '\b') {
      if (tty->cursor_x > 0) {
         tty->cursor_x--;
         /* Erase character at cursor */
         if (rel < tty->buf_lines) {
            uint32_t idx = (tty->buf_head + rel) % TTY_SCROLLBACK;
            memmove(&tty->screen_buf[idx][tty->cursor_x],
                    &tty->screen_buf[idx][tty->cursor_x + 1],
                    SCREEN_WIDTH - tty->cursor_x - 1);
            tty->screen_buf[idx][SCREEN_WIDTH - 1] = '\0';
            mark_dirty(tty, tty->cursor_y);
         }
      }
      TTY_Repaint(tty);
      return;
   }
   
   /* Printable character */
   while (rel >= tty->buf_lines) {
      push_newline(tty);
      visible_start = compute_visible_start(tty);
      rel = (uint32_t)visible_start + (uint32_t)tty->cursor_y;
   }
   
   uint32_t idx = (tty->buf_head + rel) % TTY_SCROLLBACK;
   tty->screen_buf[idx][tty->cursor_x] = c;
   tty->cursor_x++;
   
   if (tty->cursor_x >= SCREEN_WIDTH) {
      tty->cursor_x = 0;
      push_newline(tty);
   }
   
   tty->scroll_offset = 0;
   mark_dirty(tty, tty->cursor_y);
   TTY_Repaint(tty);
}

/*
 * Line discipline - canonical mode input handling
 */

static void line_flush(TTY_Device *tty)
{
   /* Push line buffer contents to input buffer */
   for (uint32_t i = 0; i < tty->line_len; i++) {
      buffer_push(&tty->input, tty->line_buf[i]);
   }
   buffer_push(&tty->input, '\n');
   tty->line_len = 0;
   tty->line_pos = 0;
   tty->line_ready = true;
}

static void line_erase_char(TTY_Device *tty)
{
   if (tty->line_len > 0) {
      tty->line_len--;
      if (tty->line_pos > tty->line_len) {
         tty->line_pos = tty->line_len;
      }
      /* Echo backspace */
      if (TTY_IsEcho(tty)) {
         tty_output_char(tty, '\b');
         tty_output_char(tty, ' ');
         tty_output_char(tty, '\b');
      }
   }
}

static void line_kill(TTY_Device *tty)
{
   /* Erase entire line */
   while (tty->line_len > 0) {
      line_erase_char(tty);
   }
}

static void line_add_char(TTY_Device *tty, char c)
{
   if (tty->line_len < TTY_LINE_SIZE - 1) {
      tty->line_buf[tty->line_len++] = c;
      tty->line_pos = tty->line_len;
      /* Echo if enabled */
      if (TTY_IsEcho(tty)) {
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
   for (int i = 0; i < TTY_MAX_DEVICES; i++) {
      g_TTYDevices[i] = NULL;
   }
   
   /* Create TTY0 (console) with static buffers */
   TTY_Device *tty0 = TTY_Create(0);
   if (tty0) {
      g_ActiveTTY = tty0;
   }
   
   g_TTYInitialized = true;
   TTY_Clear();
}

TTY_Device *TTY_Create(uint32_t id)
{
   if (id >= TTY_MAX_DEVICES) return NULL;
   if (g_TTYDevices[id] != NULL) return g_TTYDevices[id];
   
   TTY_Device *tty;
   
   /* TTY0 uses static buffers */
   if (id == 0) {
      tty = (TTY_Device *)kzalloc(sizeof(TTY_Device));
      if (!tty) return NULL;
      
      tty->screen_buf = (char (*)[SCREEN_WIDTH])g_TTY0ScreenBuf;
      tty->display_buf = g_TTY0DisplayBuf;
      buffer_init(&tty->input, g_TTY0InputBuf, TTY_INPUT_SIZE);
   } else {
      tty = (TTY_Device *)kzalloc(sizeof(TTY_Device));
      if (!tty) return NULL;
      
      tty->screen_buf = (char (*)[SCREEN_WIDTH])kmalloc(TTY_SCROLLBACK * SCREEN_WIDTH);
      if (!tty->screen_buf) {
         free(tty);
         return NULL;
      }
      
      tty->display_buf = (uint16_t *)kmalloc(SCREEN_WIDTH * SCREEN_HEIGHT * 2);
      if (!tty->display_buf) {
         free(tty->screen_buf);
         free(tty);
         return NULL;
      }
      
      char *input_buf = (char *)kmalloc(TTY_INPUT_SIZE);
      if (!input_buf) {
         free(tty->display_buf);
         free(tty->screen_buf);
         free(tty);
         return NULL;
      }
      buffer_init(&tty->input, input_buf, TTY_INPUT_SIZE);
   }
   
   /* Initialize TTY state */
   tty->id = id;
   tty->active = true;
   tty->line_len = 0;
   tty->line_pos = 0;
   tty->line_ready = false;
   tty->eof_pending = false;
   tty->buf_head = 0;
   tty->buf_lines = 0;
   tty->scroll_offset = 0;
   tty->cursor_x = 0;
   tty->cursor_y = 0;
   tty->color = 0x07;
   tty->default_color = 0x07;
   tty->flags = TTY_DEFAULT_FLAGS;
   tty->ansi_state = 0;
   tty->ansi_param_count = 0;
   tty->dirty_start = SCREEN_HEIGHT;
   tty->dirty_end = -1;
   tty->bytes_read = 0;
   tty->bytes_written = 0;
   
   /* Clear screen buffer */
   memset(tty->screen_buf, 0, TTY_SCROLLBACK * SCREEN_WIDTH);
   
   g_TTYDevices[id] = tty;
   return tty;
}

void TTY_Destroy(TTY_Device *tty)
{
   if (!tty) return;
   if (tty->id >= TTY_MAX_DEVICES) return;
   
   g_TTYDevices[tty->id] = NULL;
   
   if (g_ActiveTTY == tty) {
      g_ActiveTTY = g_TTYDevices[0];
   }
   
   /* Don't free static buffers for TTY0 */
   if (tty->id != 0) {
      free(tty->input.data);
      free(tty->display_buf);
      free(tty->screen_buf);
   }
   
   free(tty);
}

TTY_Device *TTY_GetDevice(void)
{
   return g_ActiveTTY;
}

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
   if ((tty->flags & TTY_FLAG_ICRNL) && c == '\r') {
      c = '\n';
   }
   
   /* Handle signals */
   if (tty->flags & TTY_FLAG_ISIG) {
      if (c == TTY_CHAR_INTR) {
         /* TODO: Send SIGINT to foreground process */
         if (TTY_IsEcho(tty)) {
            tty_output_char(tty, '^');
            tty_output_char(tty, 'C');
            tty_output_char(tty, '\n');
         }
         tty->line_len = 0;
         tty->line_pos = 0;
         return;
      }
      if (c == TTY_CHAR_EOF) {
         if (tty->line_len == 0) {
            tty->eof_pending = true;
            tty->line_ready = true;
         } else {
            line_flush(tty);
         }
         return;
      }
      if (c == TTY_CHAR_SUSP) {
         /* TODO: Send SIGTSTP to foreground process */
         if (TTY_IsEcho(tty)) {
            tty_output_char(tty, '^');
            tty_output_char(tty, 'Z');
            tty_output_char(tty, '\n');
         }
         return;
      }
   }
   
   /* Canonical mode - line editing */
   if (TTY_IsCanonical(tty)) {
      if (c == '\b' || c == TTY_CHAR_ERASE) {
         line_erase_char(tty);
         return;
      }
      if (c == TTY_CHAR_KILL) {
         line_kill(tty);
         return;
      }
      if (c == '\n') {
         if (TTY_IsEcho(tty)) {
            tty_output_char(tty, '\n');
         }
         line_flush(tty);
         return;
      }
      
      /* Regular character */
      line_add_char(tty, c);
   } else {
      /* Raw mode - pass through directly */
      buffer_push(&tty->input, c);
      if (TTY_IsEcho(tty)) {
         tty_output_char(tty, c);
      }
   }
}

void TTY_InputPush(char c)
{
   if (g_ActiveTTY) {
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
   if (tty->flags & TTY_FLAG_OPOST) {
      if ((tty->flags & TTY_FLAG_ONLCR) && c == '\n') {
         tty_output_char(tty, '\r');
      }
   }
   
   tty_output_char(tty, c);
   tty->bytes_written++;
}

void TTY_Write(TTY_Device *tty, const char *data, size_t len)
{
   if (!tty || !data) return;
   
   for (size_t i = 0; i < len; i++) {
      TTY_WriteChar(tty, data[i]);
   }
}

void TTY_PutChar(char c)
{
   if (g_ActiveTTY) {
      TTY_WriteChar(g_ActiveTTY, c);
   }
}

void TTY_PutString(const char *s)
{
   if (!s) return;
   while (*s) {
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
   if (TTY_IsCanonical(tty)) {
      /* Wait for line_ready - in real system this would block */
      if (!tty->line_ready && tty->input.count == 0) {
         return 0;  /* Would block */
      }
   }
   
   /* Check for EOF */
   if (tty->eof_pending && tty->input.count == 0) {
      tty->eof_pending = false;
      tty->line_ready = false;
      return 0;  /* EOF */
   }
   
   size_t bytes_read = 0;
   while (bytes_read < count) {
      int c = buffer_pop(&tty->input);
      if (c < 0) break;
      buf[bytes_read++] = (char)c;
      
      /* In canonical mode, stop at newline */
      if (TTY_IsCanonical(tty) && c == '\n') {
         break;
      }
   }
   
   if (tty->input.count == 0) {
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
   
   memset(tty->screen_buf, 0, TTY_SCROLLBACK * SCREEN_WIDTH);
   tty->buf_head = 0;
   tty->buf_lines = 0;
   tty->scroll_offset = 0;
   tty->cursor_x = 0;
   tty->cursor_y = 0;
   mark_all_dirty(tty);
   TTY_Repaint(tty);
}

void TTY_Clear(void)
{
   if (g_ActiveTTY) {
      TTY_ClearDevice(g_ActiveTTY);
   }
}

void TTY_Scroll(int lines)
{
   TTY_Device *tty = g_ActiveTTY;
   if (!tty) return;
   
   if (tty->buf_lines <= SCREEN_HEIGHT) return;
   
   int max_scroll = (int)(tty->buf_lines - SCREEN_HEIGHT);
   int new_scroll = (int)tty->scroll_offset + lines;
   
   if (new_scroll < 0) new_scroll = 0;
   if (new_scroll > max_scroll) new_scroll = max_scroll;
   
   tty->scroll_offset = (uint32_t)new_scroll;
   mark_all_dirty(tty);
   TTY_Repaint(tty);
}

void TTY_Repaint(TTY_Device *tty)
{
   if (!tty) return;
   if (tty != g_ActiveTTY) return;  /* Only repaint active TTY */
   
   int start = compute_visible_start(tty);
   
   if (tty->dirty_start > tty->dirty_end) {
      setcursor(tty->cursor_x, tty->cursor_y);
      return;
   }
   
   uint16_t attr = ((uint16_t)tty->color) << 8;
   
   for (int row = tty->dirty_start; row <= tty->dirty_end; row++) {
      uint32_t logical = (uint32_t)start + (uint32_t)row;
      uint16_t *dest = &tty->display_buf[row * SCREEN_WIDTH];
      
      if (logical >= tty->buf_lines) {
         uint16_t fill = attr | ' ';
         for (int col = 0; col < SCREEN_WIDTH; col++) {
            dest[col] = fill;
         }
      } else {
         uint32_t idx = (tty->buf_head + logical) % TTY_SCROLLBACK;
         for (int col = 0; col < SCREEN_WIDTH; col++) {
            char ch = tty->screen_buf[idx][col];
            if (!ch) ch = ' ';
            dest[col] = attr | (uint8_t)ch;
         }
      }
   }
   
   if (g_HalTtyOperations && g_HalTtyOperations->UpdateVga) {
      g_HalTtyOperations->UpdateVga(tty->display_buf);
   }
   
   reset_dirty(tty);
   setcursor(tty->cursor_x, tty->cursor_y);
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
   if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
   if (y < 0) y = 0;
   if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
   
   tty->cursor_x = x;
   tty->cursor_y = y;
   
   if (tty == g_ActiveTTY) {
      setcursor(x, y);
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
   if (g_ActiveTTY) {
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
   if (!tty) return 0;
   if (y < 0 || y >= SCREEN_HEIGHT) return 0;
   
   int start = compute_visible_start(tty);
   uint32_t logical = (uint32_t)start + (uint32_t)y;
   if (logical >= tty->buf_lines) return 0;
   
   uint32_t idx = (tty->buf_head + logical) % TTY_SCROLLBACK;
   int len = 0;
   while (len < SCREEN_WIDTH && tty->screen_buf[idx][len]) len++;
   return len;
}

int TTY_GetMaxScroll(void)
{
   TTY_Device *tty = g_ActiveTTY;
   if (!tty) return 0;
   if (tty->buf_lines <= SCREEN_HEIGHT) return 0;
   return (int)(tty->buf_lines - SCREEN_HEIGHT);
}

uint32_t TTY_GetVisibleStart(void)
{
   TTY_Device *tty = g_ActiveTTY;
   if (!tty) return 0;
   return (uint32_t)compute_visible_start(tty);
}

/*
 * Devfs operations
 */

uint32_t TTY_DevfsRead(DEVFS_DeviceNode *node, uint32_t offset,
                       uint32_t size, void *buffer)
{
   (void)offset;
   if (!buffer || size == 0) return 0;
   
   /* Get TTY from node's private_data, or use active TTY */
   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) tty = g_ActiveTTY;
   if (!tty) return 0;
   
   return (uint32_t)TTY_Read(tty, (char *)buffer, size);
}

uint32_t TTY_DevfsWrite(DEVFS_DeviceNode *node, uint32_t offset,
                        uint32_t size, const void *buffer)
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
   
   switch (cmd) {
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
         if (arg) {
            uint16_t *size = (uint16_t *)arg;
            size[0] = SCREEN_WIDTH;
            size[1] = SCREEN_HEIGHT;
         }
         return 0;
      default:
         return -1;
   }
}
