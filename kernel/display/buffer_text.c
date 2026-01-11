// SPDX-License-Identifier: AGPL-3.0-or-later

#include "buffer_text.h"
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <stdint.h>

/* declare setcursor (defined in stdio.c) to avoid implicit declaration */
extern void setcursor(int x, int y);

uint8_t s_color = 0x7;
/* Use fixed memory location for buffer instead of stack/BSS */
static char (*s_buffer)[SCREEN_WIDTH] = (char (*)[SCREEN_WIDTH])
    BUFFER_BASE_ADDR;
static uint32_t s_head = 0;       /* index of first valid line */
static uint32_t s_lines_used = 0; /* number of logical lines in buffer */
int s_cursor_x = 0;
int s_cursor_y = 0; /* cursor within visible area (0..SCREEN_HEIGHT-1) */
static uint32_t s_scroll =
    0; /* number of lines scrolled away from bottom (0 = at bottom) */

static int s_dirty_row_start = SCREEN_HEIGHT;
static int s_dirty_row_end = -1;

/* ANSI escape sequence state machine */
#define ANSI_MAX_PARAMS 16
typedef struct {
    int state;  /* 0=normal, 1=in escape, 2=in CSI */
    int params[ANSI_MAX_PARAMS];
    int param_count;
} AnsiState;

static AnsiState s_ansi_state = {0};

/* ANSI color mapping: VGA color (0-15) from ANSI color code (30-37, 40-47) */
static uint8_t ansi_to_vga_fg[] = {
    0x0, /* 30: black */
    0x4, /* 31: red */
    0x2, /* 32: green */
    0x6, /* 33: yellow */
    0x1, /* 34: blue */
    0x5, /* 35: magenta */
    0x3, /* 36: cyan */
    0x7  /* 37: white */
};

static uint8_t ansi_to_vga_bg[] = {
    0x0, /* 40: black */
    0x40, /* 41: red */
    0x20, /* 42: green */
    0x60, /* 43: yellow */
    0x10, /* 44: blue */
    0x50, /* 45: magenta */
    0x30, /* 46: cyan */
    0x70  /* 47: white */
};

static inline void mark_row_dirty(int row)
{
   if (row < 0 || row >= SCREEN_HEIGHT) return;
   if (row < s_dirty_row_start) s_dirty_row_start = row;
   if (row > s_dirty_row_end) s_dirty_row_end = row;
}

static inline void mark_dirty_range(int start, int end)
{
   if (start < 0) start = 0;
   if (end >= SCREEN_HEIGHT) end = SCREEN_HEIGHT - 1;
   if (start > end) return;
   if (start < s_dirty_row_start) s_dirty_row_start = start;
   if (end > s_dirty_row_end) s_dirty_row_end = end;
}

static inline void mark_visible_range_from_row(int row)
{
   if (row < 0) row = 0;
   if (row >= SCREEN_HEIGHT) return;
   mark_dirty_range(row, SCREEN_HEIGHT - 1);
}

static inline void mark_all_rows_dirty(void)
{
   s_dirty_row_start = 0;
   s_dirty_row_end = SCREEN_HEIGHT - 1;
}

static inline void reset_dirty_rows(void)
{
   s_dirty_row_start = SCREEN_HEIGHT;
   s_dirty_row_end = -1;
}

static void finalize_putc_repaint(int prev_visible_start);
static int compute_visible_start(void);

/* Parse ANSI escape sequences and execute them */
static void handle_ansi_sequence(void)
{
    if (s_ansi_state.param_count == 0)
        return;
    
    int param = s_ansi_state.params[0];
    
    switch (s_ansi_state.state) {
        case 'A': /* Cursor up */
            if (s_cursor_y > 0) s_cursor_y--;
            break;
        
        case 'B': /* Cursor down */
            if (s_cursor_y < SCREEN_HEIGHT - 1) s_cursor_y++;
            break;
        
        case 'C': /* Cursor right */
            if (s_cursor_x < SCREEN_WIDTH - 1) s_cursor_x++;
            break;
        
        case 'D': /* Cursor left */
            if (s_cursor_x > 0) s_cursor_x--;
            break;
        
        case 'H': /* Cursor position (ESC[row;colH) */
        case 'f':
            if (s_ansi_state.param_count >= 2) {
                int row = s_ansi_state.params[0];
                int col = s_ansi_state.params[1];
                if (row > 0) row--; /* 1-indexed to 0-indexed */
                if (col > 0) col--;
                if (row >= 0 && row < SCREEN_HEIGHT) s_cursor_y = row;
                if (col >= 0 && col < SCREEN_WIDTH) s_cursor_x = col;
            }
            break;
        
        case 'J': /* Erase display */
            if (param == 2 || param == 0) {
                Buffer_Clear();
            }
            break;
        
        case 'K': /* Erase line */
            {
                int visible_start = compute_visible_start();
                uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)s_cursor_y;
                if (rel_pos < s_lines_used) {
                    uint32_t idx = (s_head + rel_pos) % BUFFER_LINES;
                    memset(s_buffer[idx], 0, SCREEN_WIDTH);
                    mark_row_dirty(s_cursor_y);
                }
            }
            break;
        
        case 'm': /* SGR - Select Graphic Rendition (colors/attributes) */
            for (int i = 0; i < s_ansi_state.param_count; i++) {
                int code = s_ansi_state.params[i];
                
                if (code == 0) {
                    /* Reset all attributes */
                    s_color = 0x7;
                } else if (code == 1) {
                    /* Bold - increase brightness */
                    s_color |= 0x08;
                } else if (code >= 30 && code <= 37) {
                    /* Foreground color */
                    uint8_t fg = ansi_to_vga_fg[code - 30];
                    s_color = (s_color & 0xF0) | (fg & 0x0F);
                } else if (code >= 40 && code <= 47) {
                    /* Background color */
                    uint8_t bg = ansi_to_vga_bg[code - 40];
                    s_color = (s_color & 0x0F) | (bg & 0xF0);
                } else if (code >= 90 && code <= 97) {
                    /* Bright foreground (same as 30-37 but with bold bit) */
                    uint8_t fg = ansi_to_vga_fg[code - 90];
                    s_color = (s_color & 0xF0) | (fg | 0x08);
                } else if (code >= 100 && code <= 107) {
                    /* Bright background */
                    uint8_t bg = ansi_to_vga_bg[code - 100];
                    s_color = (s_color & 0x0F) | ((bg | 0x80) & 0xF0);
                }
            }
            break;
    }
}

/* Process a character in an ANSI escape sequence */
static int process_ansi_char(char c)
{
    if (s_ansi_state.state == 0) {
        /* Not in escape sequence */
        if (c == '\x1B') { /* ESC */
            s_ansi_state.state = 1;
            return 1; /* consumed */
        }
        return 0; /* not consumed */
    }
    
    if (s_ansi_state.state == 1) {
        /* After ESC, expecting [ for CSI */
        if (c == '[') {
            s_ansi_state.state = 2;
            s_ansi_state.param_count = 0;
            s_ansi_state.params[0] = 0;
            return 1;
        }
        /* Invalid sequence, reset */
        s_ansi_state.state = 0;
        return 1;
    }
    
    if (s_ansi_state.state == 2) {
        /* In CSI sequence */
        if (c >= '0' && c <= '9') {
            /* Accumulate parameter */
            s_ansi_state.params[s_ansi_state.param_count] =
                s_ansi_state.params[s_ansi_state.param_count] * 10 + (c - '0');
            return 1;
        } else if (c == ';') {
            /* Parameter separator */
            s_ansi_state.param_count++;
            if (s_ansi_state.param_count >= ANSI_MAX_PARAMS)
                s_ansi_state.param_count = ANSI_MAX_PARAMS - 1;
            s_ansi_state.params[s_ansi_state.param_count] = 0;
            return 1;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            /* Command character */
            s_ansi_state.state = c;
            s_ansi_state.param_count++;
            handle_ansi_sequence();
            s_ansi_state.state = 0;
            return 1;
        } else if (c == '?') {
            /* DEC private mode - just skip for now */
            return 1;
        }
        return 1; /* consume unknown chars */
    }
    
    return 0;
}

void Buffer_Initialize(void) { Buffer_Clear(); }

void Buffer_Clear(void)
{
   memset((char *)s_buffer, 0, BUFFER_LINES * SCREEN_WIDTH);
   s_head = 0;
   s_lines_used = 0;
   s_cursor_x = 0;
   s_cursor_y = 0;
   s_scroll = 0;
   mark_all_rows_dirty();
   Buffer_Repaint();
}

static void ensure_line_exists(void)
{
   if (s_lines_used == 0)
   {
      s_lines_used = 1;
      s_head = 0;
      for (int i = 0; i < SCREEN_WIDTH; i++) s_buffer[0][i] = '\0';
   }
}

/* Compute the relative index of the first visible logical line within the
   buffer (0..). This takes into account s_lines_used, SCREEN_HEIGHT and
   s_scroll and clamps to >= 0. */
static int compute_visible_start(void)
{
   int base =
       (s_lines_used > SCREEN_HEIGHT) ? (int)(s_lines_used - SCREEN_HEIGHT) : 0;
   int start = base - (int)s_scroll;
   if (start < 0) start = 0;
   return start;
}

/* Remove logical line at relative position rel_pos (0..s_lines_used-1).
   Shifts following lines left. */
static void buffer_remove_line_at_rel(uint32_t rel_pos)
{
   if (s_lines_used == 0 || rel_pos >= s_lines_used) return;
   for (uint32_t i = rel_pos; i + 1 < s_lines_used; i++)
   {
      uint32_t dst = (s_head + i) % BUFFER_LINES;
      uint32_t src = (s_head + i + 1) % BUFFER_LINES;
      memcpy(s_buffer[dst], s_buffer[src], SCREEN_WIDTH);
   }
   uint32_t last = (s_head + s_lines_used - 1) % BUFFER_LINES;
   memset(s_buffer[last], 0, SCREEN_WIDTH);
   s_lines_used--;
   if (s_lines_used == 0) s_head = 0;
}

/* Append an empty line at the tail. If buffer full, drop the head. */
static void push_newline_at_tail(void)
{
   if (s_lines_used < BUFFER_LINES)
   {
      uint32_t idx = (s_head + s_lines_used) % BUFFER_LINES;
      memset(s_buffer[idx], 0, SCREEN_WIDTH);
      s_lines_used++;
   }
   else
   {
      /* drop oldest line */
      s_head = (s_head + 1) % BUFFER_LINES;
      uint32_t idx = (s_head + s_lines_used - 1) % BUFFER_LINES;
      memset(s_buffer[idx], 0, SCREEN_WIDTH);
   }
   /* When a new logical line is appended programmatically, we prefer to
      preserve the user's scroll position unless they were already at the
      bottom (auto-follow mode). If s_scroll == 0 (following tail) then
      reset scroll to show the bottom and place the cursor on the last
      visible row. Otherwise increment s_scroll to keep the same visible
      window contents (because base increases by 1). */
   if (s_scroll == 0)
   {
      /* keep following the tail */
      if (s_lines_used >= SCREEN_HEIGHT)
         s_cursor_y = SCREEN_HEIGHT - 1;
      else
         s_cursor_y = (int)s_lines_used - 1;
   }
   else
   {
      /* user scrolled up: advance scroll so visible start remains stable */
      int max_scroll = (int)(s_lines_used - SCREEN_HEIGHT);
      int new_scroll = (int)s_scroll + 1;
      if (new_scroll > max_scroll) new_scroll = max_scroll;
      s_scroll = (uint32_t)new_scroll;
   }
}

/* Insert an empty logical line at relative position rel_pos (0..s_lines_used).
   If buffer is full, the oldest line is dropped (s_head advanced). */
static void buffer_insert_empty_line_at_rel(uint32_t rel_pos)
{
   if (rel_pos > s_lines_used) rel_pos = s_lines_used;

   if (s_lines_used < BUFFER_LINES)
   {
      /* shift lines right from tail to rel_pos */
      for (int i = (int)s_lines_used; i > (int)rel_pos; i--)
      {
         uint32_t dst = (s_head + i) % BUFFER_LINES;
         uint32_t src = (s_head + i - 1) % BUFFER_LINES;
         memmove(s_buffer[dst], s_buffer[src], SCREEN_WIDTH);
      }
      uint32_t idx = (s_head + rel_pos) % BUFFER_LINES;
      memset(s_buffer[idx], 0, SCREEN_WIDTH);
      s_lines_used++;
   }
   else
   {
      /* buffer full: drop head, then shift (head already moved logically) */
      s_head = (s_head + 1) % BUFFER_LINES;
      /* now move lines right from tail-1 down to rel_pos */
      for (int i = (int)s_lines_used - 1; i > (int)rel_pos; i--)
      {
         uint32_t dst = (s_head + i) % BUFFER_LINES;
         uint32_t src = (s_head + i - 1) % BUFFER_LINES;
         memmove(s_buffer[dst], s_buffer[src], SCREEN_WIDTH);
      }
      uint32_t idx = (s_head + rel_pos) % BUFFER_LINES;
      memset(s_buffer[idx], 0, SCREEN_WIDTH);
      /* s_lines_used remains BUFFER_LINES */
   }
}

void Buffer_PutChar(char c)
{
   /* Handle ANSI escape sequences first */
   if (process_ansi_char(c))
       return;
   
   ensure_line_exists();
   int prev_visible_start = compute_visible_start();
   int prev_cursor_row = s_cursor_y;

   int visible_start = prev_visible_start;
   uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)s_cursor_y;
   uint32_t idx = (s_head + (rel_pos < s_lines_used
                                 ? rel_pos
                                 : (s_lines_used ? s_lines_used - 1 : 0))) %
                  BUFFER_LINES;

   if (c == '\n')
   {
      s_scroll = 0;

      uint32_t rel = rel_pos;
      if (rel >= s_lines_used) rel = s_lines_used - 1;
      uint32_t idx_cur = (s_head + rel) % BUFFER_LINES;

      int len = 0;
      while (len < SCREEN_WIDTH && s_buffer[idx_cur][len]) len++;

      if (s_cursor_x < len)
      {
         buffer_insert_empty_line_at_rel(rel + 1);
         uint32_t idx_new = (s_head + rel + 1) % BUFFER_LINES;
         int move = len - s_cursor_x;
         memcpy(s_buffer[idx_new], &s_buffer[idx_cur][s_cursor_x], move);
         memset(s_buffer[idx_new] + move, 0, SCREEN_WIDTH - move);
         memset(s_buffer[idx_cur] + s_cursor_x, 0, SCREEN_WIDTH - s_cursor_x);
      }
      else
      {
         buffer_insert_empty_line_at_rel(rel + 1);
      }

      {
         uint32_t new_logical = rel + 1;
         int new_start = compute_visible_start();
         int new_y = (int)new_logical - new_start;
         if (new_y < 0) new_y = 0;
         if (new_y >= SCREEN_HEIGHT)
         {
            if (s_lines_used > SCREEN_HEIGHT)
               s_head = (s_head + 1) % BUFFER_LINES;
            new_y = SCREEN_HEIGHT - 1;
         }
         s_cursor_y = new_y;
      }

      s_cursor_x = 0;
      mark_row_dirty(prev_cursor_row);
      mark_row_dirty(prev_cursor_row + 1);
      mark_row_dirty(s_cursor_y);
      goto repaint;
   }

   if (c == '\r')
   {
      // Clear the current line and move cursor to start
      int visible_start = compute_visible_start();
      uint32_t rel_pos = (uint32_t)visible_start + (uint32_t)s_cursor_y;
      if (rel_pos < s_lines_used)
      {
         uint32_t idx = (s_head + rel_pos) % BUFFER_LINES;
         memset(s_buffer[idx], 0, SCREEN_WIDTH);
      }
      s_cursor_x = 0;
      mark_row_dirty(s_cursor_y);
      setcursor(s_cursor_x, s_cursor_y);
      return;
   }

   if (c == '\t')
   {
      int n = 4 - (s_cursor_x % 4);
      for (int i = 0; i < n; i++) Buffer_PutChar(' ');
      return;
   }

   if (c == '\b')
   {
      if (s_cursor_x > 0)
      {
         memmove(&s_buffer[idx][s_cursor_x - 1], &s_buffer[idx][s_cursor_x],
                 SCREEN_WIDTH - s_cursor_x);
         s_buffer[idx][SCREEN_WIDTH - 1] = '\0';
         s_cursor_x--;
         mark_row_dirty(prev_cursor_row);
         goto repaint;
      }

      if (rel_pos > 0)
      {
         uint32_t prev_rel = rel_pos - 1;
         uint32_t prev_idx = (s_head + prev_rel) % BUFFER_LINES;
         int len_prev = 0, len_curr = 0;
         while (len_prev < SCREEN_WIDTH && s_buffer[prev_idx][len_prev])
            len_prev++;
         while (len_curr < SCREEN_WIDTH && s_buffer[idx][len_curr]) len_curr++;
         int orig_prev_len = len_prev;
         int can_move = SCREEN_WIDTH - len_prev;
         int move = (len_curr < can_move) ? len_curr : can_move;
         memcpy(&s_buffer[prev_idx][len_prev], s_buffer[idx], move);

         if (move < len_curr)
         {
            int leftover = len_curr - move;
            memmove(s_buffer[idx], s_buffer[idx] + move, leftover);
            memset(s_buffer[idx] + leftover, 0, SCREEN_WIDTH - leftover);
         }
         else
         {
            memset(s_buffer[idx], 0, SCREEN_WIDTH);
         }

         int empty = 1;
         for (int i = 0; i < SCREEN_WIDTH; i++)
            if (s_buffer[idx][i])
            {
               empty = 0;
               break;
            }
         if (empty)
         {
            buffer_remove_line_at_rel(rel_pos);
            int visible_off = (int)((s_lines_used > SCREEN_HEIGHT)
                                        ? (s_lines_used - SCREEN_HEIGHT)
                                        : 0);
            s_cursor_y = (int)prev_rel - visible_off;
            if (s_cursor_y < 0) s_cursor_y = 0;
            int new_len = orig_prev_len;
            if (new_len > SCREEN_WIDTH - 1) new_len = SCREEN_WIDTH - 1;
            s_cursor_x = new_len;
         }
         else
         {
            s_cursor_x = 0;
         }
         mark_visible_range_from_row(prev_cursor_row > 0 ? prev_cursor_row - 1
                                                         : 0);
         mark_row_dirty(s_cursor_y);
         goto repaint;
      }

      s_scroll = 0;
      mark_all_rows_dirty();
      goto repaint;
   }

   {
      visible_start = compute_visible_start();
      rel_pos = (uint32_t)visible_start + (uint32_t)s_cursor_y;
      while (rel_pos >= s_lines_used) push_newline_at_tail();
      idx = (s_head + rel_pos) % BUFFER_LINES;

      int len = 0;
      while (len < SCREEN_WIDTH && s_buffer[idx][len]) len++;
      if (s_cursor_x > len) s_cursor_x = len;

      if (len < SCREEN_WIDTH)
      {
         memmove(&s_buffer[idx][s_cursor_x + 1], &s_buffer[idx][s_cursor_x],
                 (size_t)(len - s_cursor_x));
         s_buffer[idx][s_cursor_x] = c;
      }
      else
      {
         char last = s_buffer[idx][SCREEN_WIDTH - 1];
         if (rel_pos + 1 >= s_lines_used)
         {
            push_newline_at_tail();
         }
         uint32_t next_idx = (s_head + rel_pos + 1) % BUFFER_LINES;
         memmove(&s_buffer[next_idx][1], s_buffer[next_idx], SCREEN_WIDTH - 1);
         s_buffer[next_idx][0] = last;
         memmove(&s_buffer[idx][s_cursor_x + 1], &s_buffer[idx][s_cursor_x],
                 SCREEN_WIDTH - 1 - s_cursor_x);
         s_buffer[idx][s_cursor_x] = c;
      }

      s_cursor_x++;
      s_scroll = 0;
      if (s_cursor_x >= SCREEN_WIDTH)
      {
         s_cursor_x = 0;
         if (s_cursor_y < SCREEN_HEIGHT - 1)
            s_cursor_y++;
         else if (s_lines_used > SCREEN_HEIGHT)
            s_head = (s_head + 1) % BUFFER_LINES;
      }
      mark_row_dirty(prev_cursor_row);
      mark_row_dirty(s_cursor_y);
      goto repaint;
   }

repaint:
   finalize_putc_repaint(prev_visible_start);
}

static void finalize_putc_repaint(int prev_visible_start)
{
   if (compute_visible_start() != prev_visible_start) mark_all_rows_dirty();
   Buffer_Repaint();
}

void Buffer_PutString(const char *s)
{
   while (*s) Buffer_PutChar(*s++);
}

void Buffer_Scroll(int lines)
{
   /* Positive lines -> scroll up (view older content); negative -> scroll down.
      We maintain s_scroll which is clamped between 0 and max_scroll. */
   if (s_lines_used <= SCREEN_HEIGHT)
   {
      /* nothing to scroll */
      return;
   }

   int max_scroll = (int)(s_lines_used - SCREEN_HEIGHT);
   int new_scroll = (int)s_scroll + lines;
   if (new_scroll < 0) new_scroll = 0;
   if (new_scroll > max_scroll) new_scroll = max_scroll;
   s_scroll = (uint32_t)new_scroll;
   mark_all_rows_dirty();
   Buffer_Repaint();
}

void Buffer_SetColor(uint8_t color) { s_color = color; }

void Buffer_SetCursor(int x, int y)
{
   if (x < 0) x = 0;
   if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
   if (y < 0) y = 0;
   if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
   /* Clamp x to the actual printable length of the visible logical line so
      the cursor cannot be positioned in trailing empty space. */
   int max_x = Buffer_GetVisibleLineLength(y);
   if (x > max_x) x = max_x;
   s_cursor_x = x;
   s_cursor_y = y;
   setcursor(s_cursor_x, s_cursor_y);
}

void Buffer_GetCursor(int *x, int *y)
{
   if (x) *x = s_cursor_x;
   if (y) *y = s_cursor_y;
}

int Buffer_GetVisibleLineLength(int y)
{
   if (y < 0 || y >= SCREEN_HEIGHT) return 0;
   int start = compute_visible_start();
   uint32_t logical = (uint32_t)start + (uint32_t)y;
   if (logical >= s_lines_used) return 0;
   uint32_t idx = (s_head + logical) % BUFFER_LINES;
   int len = 0;
   while (len < SCREEN_WIDTH && s_buffer[idx][len]) len++;
   return len;
}

int Buffer_GetMaxScroll(void)
{
   if (s_lines_used <= SCREEN_HEIGHT) return 0;
   return (int)(s_lines_used - SCREEN_HEIGHT);
}

uint32_t Buffer_GetVisibleStart(void)
{
   return (uint32_t)compute_visible_start();
}

void Buffer_Repaint(void)
{
   int start = compute_visible_start();
   if (s_dirty_row_start > s_dirty_row_end)
   {
      setcursor(s_cursor_x, s_cursor_y);
      return;
   }

   uint16_t *vga = (uint16_t *)0xB8000;
   const uint8_t def_color = 0x7;
   uint16_t attr = ((uint16_t)(s_color ? s_color : def_color)) << 8;

   for (int row = s_dirty_row_start; row <= s_dirty_row_end; row++)
   {
      uint32_t logical_line = (uint32_t)start + (uint32_t)row;
      uint16_t *dest = &vga[row * SCREEN_WIDTH];
      if (logical_line >= s_lines_used)
      {
         uint16_t fill = attr | ' ';
         for (uint32_t col = 0; col < SCREEN_WIDTH; col++) dest[col] = fill;
      }
      else
      {
         uint32_t src = (s_head + logical_line) % BUFFER_LINES;
         for (uint32_t col = 0; col < SCREEN_WIDTH; col++)
         {
            char ch = s_buffer[src][col];
            if (!ch) ch = ' ';
            dest[col] = attr | (uint8_t)ch;
         }
      }
   }

   reset_dirty_rows();
   setcursor(s_cursor_x, s_cursor_y);
}
