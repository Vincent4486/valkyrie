// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <stdint.h>

/* 8×16 bitmap font – one byte per row, 16 rows per glyph.
 *
 * Each byte encodes 8 horizontal pixels (MSB = leftmost pixel).
 * Glyphs are defined for the printable ASCII range 0x20 … 0x7E.
 */

#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define FONT_FIRST 0x20                       /* first defined glyph (space) */
#define FONT_LAST 0x7E                        /* last  defined glyph (~) */
#define FONT_NUM (FONT_LAST - FONT_FIRST + 1) /* 95 glyphs */

/** 8×16 bitmap font table. */
extern const uint8_t g_Font8x16[FONT_NUM][FONT_HEIGHT];
