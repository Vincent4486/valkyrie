// SPDX-License-Identifier: GPL-3.0-only

#ifndef TTY_H
#define TTY_H

#include <stdint.h>

/* Export screen dimensions so other modules (keyboard, etc.) can reference
   the VGA text-mode dimensions. Keep these in the header to avoid duplicate
   magic numbers across files. */
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

void TTY_Initialize(void);
void TTY_Clear(void);
void TTY_PutChar(char c);
void TTY_PutString(const char *s);
void TTY_Repaint(void);
void TTY_Scroll(int lines);
void TTY_SetColor(uint8_t color);
void TTY_SetCursor(int x, int y);
void TTY_GetCursor(int *x, int *y);
/* Input stream: push keyboard chars here (kernel-only). */
int TTY_InputPush(char c);
/* Read a single character from the input stream. If no data, return -1.
   Reading moves the character to the output stream (visible) and removes
   it from the input FIFO. */
int TTY_ReadChar(void);
/* Return the length (number of printable chars) of the visible logical line
   at the given visible row y (0..SCREEN_HEIGHT-1). Returns 0 if no line.
   This is useful for cursor movement logic in input handling. */
int TTY_GetVisibleLineLength(int y);
/* Return maximum number of scroll lines available (older content). */
int TTY_GetMaxScroll(void);
/* Return the logical index (relative to head) of the first visible line. */
uint32_t TTY_GetVisibleStart(void);

/* Debug: draw a small overlay on row 0 with buffer internals (s_lines_used,
   s_head, s_scroll, max_scroll). This is temporary debugging aid. */
void TTY_DebugOverlay(void);

#endif