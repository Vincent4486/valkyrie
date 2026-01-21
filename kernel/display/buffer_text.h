// SPDX-License-Identifier: GPL-3.0-only

#ifndef BUFFER_TEXT_H
#define BUFFER_TEXT_H

#include <stdint.h>

/* Export screen dimensions so other modules (keyboard, etc.) can reference
   the VGA text-mode dimensions. Keep these in the header to avoid duplicate
   magic numbers across files. */
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

void Buffer_Initialize(void);
void Buffer_Clear(void);
void Buffer_PutChar(char c);
void Buffer_PutString(const char *s);
void Buffer_Repaint(void);
void Buffer_Scroll(int lines);
void Buffer_SetColor(uint8_t color);
void Buffer_SetCursor(int x, int y);
void Buffer_GetCursor(int *x, int *y);
/* Return the length (number of printable chars) of the visible logical line
   at the given visible row y (0..SCREEN_HEIGHT-1). Returns 0 if no line.
   This is useful for cursor movement logic in input handling. */
int Buffer_GetVisibleLineLength(int y);
/* Return maximum number of scroll lines available (older content). */
int Buffer_GetMaxScroll(void);
/* Return the logical index (relative to head) of the first visible line. */
uint32_t Buffer_GetVisibleStart(void);

/* Debug: draw a small overlay on row 0 with buffer internals (s_lines_used,
   s_head, s_scroll, max_scroll). This is temporary debugging aid. */
void Buffer_DebugOverlay(void);

#endif