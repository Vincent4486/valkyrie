// SPDX-License-Identifier: GPL-3.0-only

/*
 * kernel/arch/i686/video/vga.c
 *
 * VGA 80x25 text-mode backend for the HAL video interface.
 *
 * This translation unit is the ONLY place in the i686 port that touches
 * VGA VRAM (0xB8000) and the CRT controller I/O ports (0x3D4/0x3D5).
 * All other code reaches hardware rendering through g_HalVideoOperations.
 */

#include "vga.h"
#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <std/string.h>
#include <stdbool.h>
#include <stdint.h>

/* ── Hardware constants ──────────────────────────────────────────────────── */

#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

/* CRT controller: index port and data port */
#define VGA_CRTC_ADDR 0x3D4
#define VGA_CRTC_DATA 0x3D5

/* Sequencer and Graphics Controller ports */
#define VGA_SEQ_ADDR 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_GFX_ADDR 0x3CE
#define VGA_GFX_DATA 0x3CF

/* Miscellaneous Output / Input Status ports */
#define VGA_MISC_OUT                                                           \
   0x3C2                 /* write: clock select, sync polarity, RAM enable     \
                          */
#define VGA_ISTAT1 0x3DA /* read:  resets Attribute Controller flip-flop   */

/* Attribute Controller address/data port and Palette Address Source bit */
#define VGA_AC_ADDR 0x3C0 /* write index then data; read: index register    */
#define VGA_AC_PAS 0x20 /* Palette Address Source – set to re-enable video */

/* CRTC register indices – horizontal */
#define VGA_CRTC_HTOTAL 0x00  /* Horizontal Total                          */
#define VGA_CRTC_HDEND 0x01   /* Horizontal Display Enable End             */
#define VGA_CRTC_HBSTART 0x02 /* Start Horizontal Blanking                 */
#define VGA_CRTC_HBEND 0x03   /* End   Horizontal Blanking                 */
#define VGA_CRTC_HRSTART 0x04 /* Start Horizontal Retrace Pulse            */
#define VGA_CRTC_HREND 0x05   /* End   Horizontal Retrace Pulse            */
/* CRTC register indices – vertical */
#define VGA_CRTC_VTOTAL 0x06   /* Vertical Total (low 8 bits)               */
#define VGA_CRTC_OVERFLOW 0x07 /* Overflow (high bits of VT/VDE/VRS/VBS)   */
#define VGA_CRTC_MAXSCAN 0x09  /* Maximum Scan Line (font height − 1)       */
#define VGA_CRTC_CURSOR_START 0x0A /* Cursor Scan Line Start               */
#define VGA_CRTC_CURSOR_END 0x0B   /* Cursor Scan Line End                 */
#define VGA_CRTC_CURSOR_HI 0x0E    /* Cursor Location High byte            */
#define VGA_CRTC_CURSOR_LO 0x0F    /* Cursor Location Low  byte            */
#define VGA_CRTC_VRSTART 0x10 /* Vertical Retrace Start (low 8 bits)       */
#define VGA_CRTC_VREND 0x11   /* Vertical Retrace End   (holds protect bit) */
#define VGA_CRTC_VDEND 0x12   /* Vertical Display Enable End (low 8 bits)  */
#define VGA_CRTC_OFFSET 0x13  /* Logical Screen Width in words            */
#define VGA_CRTC_VBSTART 0x15 /* Vertical Blank Start (low 8 bits)         */
#define VGA_CRTC_VBEND 0x16   /* Vertical Blank End                        */

/* VGA character-generator plane constants */
#define VGA_FONT_PLANE ((volatile uint8_t *)0xA0000)
#define VGA_GLYPH_SLOT 32 /* bytes reserved per glyph in plane 2  */

/* ── Current display dimensions (updated by i686_VGA_SetDisplaySize) ─────── */

static int s_VGA_Cols = 80;
static int s_VGA_Rows = 25;
static bool s_font_cache_ready = false;
static uint8_t s_font16[256 * 16];
static uint8_t s_font8[256 * 8];

#define VGA_TERM_MAX_COLS 160
#define VGA_TERM_MAX_ROWS 50
#define VGA_ANSI_PARAM_MAX 16

static uint16_t s_TermBuffer[VGA_TERM_MAX_ROWS][VGA_TERM_MAX_COLS];
static int s_TermCursorX = 0;
static int s_TermCursorY = 0;
static uint8_t s_TermColor = 0x07;
static uint8_t s_TermDefaultColor = 0x07;
static uint8_t s_TermInputColor = 0x07;
static int s_AnsiState = 0;
static int s_AnsiParamCount = 0;
static int s_AnsiParams[VGA_ANSI_PARAM_MAX];

static const uint8_t s_AnsiToVgaFg[] = {0x0, 0x4, 0x2, 0x6, 0x1, 0x5, 0x3, 0x7};
static const uint8_t s_AnsiToVgaBg[] = {0x00, 0x40, 0x20, 0x60,
                                        0x10, 0x50, 0x30, 0x70};

/* ── Text-mode descriptor table ─────────────────────────────────────────── */

/** One CRTC (index, value) write; sentinel has index == 0xFF. */
typedef struct
{
   uint8_t reg;
   uint8_t val;
} VGA_RegVal;

/** Complete CRTC programming for a supported text mode. */
typedef struct
{
   int cols;
   int rows;
   int char_height;     /* scan lines per character: 8 or 16          */
   uint8_t misc_output; /* Miscellaneous Output Register (port 0x3C2)  */
   uint8_t overflow;    /* CRTC Overflow Register (0x07): high bits of
                         *   VT[8], VDE[8], VRS[8], VBS[8], LC[8].
                         * Written explicitly inside the CRTC-protect
                         * unlock window before the crtc[] table loop.  */
   VGA_RegVal crtc[17]; /* sentinel-terminated; up to 16 CRTC writes   */
} VGA_ModeDesc;

/*
 * CRTC register tables for each supported text mode.
 *
 *  40×25  – halved horizontal timing, 16-line font.
 *  80×25  – standard mode-3 timing,   16-line font.
 *  80×43  – 350-scanline EGA-compat,   8-line font (43 × 8 = 344 visible).
 *  80×50  – 400-scanline standard,     8-line font (50 × 8 = 400 visible).
 *
 * Register 0x11 (Vertical Retrace End / protect) is NOT listed in any table;
 * it is unlocked before the loop and re-locked after to protect 0x00–0x07.
 *
 * Sources: VGA-FAQ, VGADOC4b, Ralf Brown's Interrupt List (INT 10h tables).
 */
/*
 * Complete CRTC + Misc Output tables for each supported text mode.
 *
 * All four modes share the same 25 MHz / 28 MHz dot clock, and the same
 * horizontal timing for equal column counts, so only vertical registers
 * and MaxScanLine differ between 80×25 and 80×50.
 *
 * Vertical register derivations (400-scanline base, VGA mode 3):
 *   VT  = 447 → 0x06 = 0xBF (lo), overflow bit0 = 1
 *   VDE = 399 → 0x12 = 0x8F (lo), overflow bit1 = 1
 *   VRS = 412 → 0x10 = 0x9C (lo), overflow bit2 = 1
 *   VBS = 406 → 0x15 = 0x96 (lo), overflow bit3 = 1
 *   Overflow = 0x1F (bits 0‒4 set, bits 5‒7 clear = values < 512)
 *
 * For 350-scanline EGA-compat mode (80×43):
 *   VT  = 363 → 0x06 = 0x6B, overflow bit0 = 1
 *   VDE = 343 → 0x12 = 0x57, overflow bit1 = 1 (43 rows × 8 px = 344 − 1)
 *   VRS = 350 → 0x10 = 0x5E, overflow bit2 = 1
 *   VBS = 344 → 0x15 = 0x58, overflow bit3 = 1
 *   Overflow = 0x1F
 *   Misc Output = 0xA3  (25 MHz clock, +V/+H for 350-line)
 *
 * Register 0x11 (VRE / protect) is handled separately in the switch
 * function and is therefore NOT listed in any crtc[] array.
 */
static const VGA_ModeDesc s_VGA_Modes[] = {
    /* ── 40×25 (400-scanline, 16-line font) ────────────────── */
    {.cols = 40,
     .rows = 25,
     .char_height = 16,
     .misc_output = 0x63, /* 25 MHz clock, colour, RAM enable        */
     /* Overflow 0x1F: VT[8]=1 VDE[8]=1 VRS[8]=1 VBS[8]=1 LC[8]=1  */
     .overflow = 0x1F,
     .crtc =
         {
             /* Horizontal */
             {VGA_CRTC_HTOTAL, 0x37},  /* Horizontal Total                */
             {VGA_CRTC_HDEND, 0x27},   /* Horizontal Display End          */
             {VGA_CRTC_HBSTART, 0x2D}, /* Start Horizontal Blank          */
             {VGA_CRTC_HBEND, 0x37},   /* End   Horizontal Blank          */
             {VGA_CRTC_HRSTART, 0x31}, /* Start Horizontal Retrace        */
             {VGA_CRTC_HREND, 0xB2},   /* End   Horizontal Retrace        */
             /* Vertical (400 scanlines, 16-line font: 25 rows × 16 px = 400) */
             {VGA_CRTC_VTOTAL, 0xBF},  /* Vertical Total     lo (447)     */
             {VGA_CRTC_MAXSCAN, 0x4F}, /* Max Scan Line 15 + LC[9]=1      */
             {VGA_CRTC_CURSOR_START, 0x0E}, /* Cursor Start             */
             {VGA_CRTC_CURSOR_END, 0x0F},   /* Cursor End               */
             {VGA_CRTC_VRSTART, 0x9C}, /* Vertical Retrace   Start lo     */
             {VGA_CRTC_VDEND, 0x8F},   /* Vertical Display   End   lo (399)*/
             {VGA_CRTC_OFFSET, 0x14},  /* Offset (40-col = 20 words)      */
             {VGA_CRTC_VBSTART, 0x96}, /* Vertical Blank     Start lo     */
             {VGA_CRTC_VBEND, 0xB9},   /* Vertical Blank     End          */
             {0xFF, 0x00} /* sentinel                                    */
         }},
    /* ── 80×25 (400-scanline, 16-line font, standard mode 3) ── */
    {.cols = 80,
     .rows = 25,
     .char_height = 16,
     .misc_output = 0x67, /* 28 MHz clock, colour, RAM enable        */
     /* Overflow 0x1F: VT[8]=1 VDE[8]=1 VRS[8]=1 VBS[8]=1 LC[8]=1  */
     .overflow = 0x1F,
     .crtc =
         {
             /* Horizontal */
             {VGA_CRTC_HTOTAL, 0x5F},  /* Horizontal Total                */
             {VGA_CRTC_HDEND, 0x4F},   /* Horizontal Display End          */
             {VGA_CRTC_HBSTART, 0x50}, /* Start Horizontal Blank          */
             {VGA_CRTC_HBEND, 0x82},   /* End   Horizontal Blank          */
             {VGA_CRTC_HRSTART, 0x55}, /* Start Horizontal Retrace        */
             {VGA_CRTC_HREND, 0x81},   /* End   Horizontal Retrace        */
             /* Vertical (400 scanlines, 16-line font: 25 rows × 16 px = 400) */
             {VGA_CRTC_VTOTAL, 0xBF},  /* Vertical Total     lo (447)     */
             {VGA_CRTC_MAXSCAN, 0x4F}, /* Max Scan Line 15 + LC[9]=1      */
             {VGA_CRTC_CURSOR_START, 0x0E}, /* Cursor Start             */
             {VGA_CRTC_CURSOR_END, 0x0F},   /* Cursor End               */
             {VGA_CRTC_VRSTART, 0x9C}, /* Vertical Retrace   Start lo     */
             {VGA_CRTC_VDEND, 0x8F},   /* Vertical Display   End   lo (399)*/
             {VGA_CRTC_OFFSET, 0x28},  /* Offset (80-col = 40 words)      */
             {VGA_CRTC_VBSTART, 0x96}, /* Vertical Blank     Start lo     */
             {VGA_CRTC_VBEND, 0xB9},   /* Vertical Blank     End          */
             {0xFF, 0x00} /* sentinel                                    */
         }},
    /* ── 80×43 (350-scanline EGA-compat, 8-line font) ──────── */
    {.cols = 80,
     .rows = 43,
     .char_height = 8,
     .misc_output = 0xA3, /* 25 MHz clock, +V/+H polarity for 350 lines */
     /* Overflow 0x1F: VT[8]=1 VDE[8]=1 VRS[8]=1 VBS[8]=1 LC[8]=1  */
     .overflow = 0x1F,
     .crtc =
         {
             /* Horizontal */
             {VGA_CRTC_HTOTAL, 0x5F},  /* Horizontal Total                */
             {VGA_CRTC_HDEND, 0x4F},   /* Horizontal Display End          */
             {VGA_CRTC_HBSTART, 0x50}, /* Start Horizontal Blank          */
             {VGA_CRTC_HBEND, 0x82},   /* End   Horizontal Blank          */
             {VGA_CRTC_HRSTART, 0x55}, /* Start Horizontal Retrace        */
             {VGA_CRTC_HREND, 0x81},   /* End   Horizontal Retrace        */
             /* Vertical (350-scanline EGA: 43 rows × 8 px = 344 visible)    */
             {VGA_CRTC_VTOTAL, 0x6B},  /* Vertical Total     lo (363)     */
             {VGA_CRTC_MAXSCAN, 0x07}, /* Max Scan Line 7   (8-line font)  */
             {VGA_CRTC_CURSOR_START, 0x06}, /* Cursor Start             */
             {VGA_CRTC_CURSOR_END, 0x07},   /* Cursor End               */
             {VGA_CRTC_VRSTART, 0x5E}, /* Vertical Retrace   Start lo (350)*/
             {VGA_CRTC_VDEND, 0x57},   /* Vertical Display   End   lo (343)*/
             {VGA_CRTC_OFFSET, 0x28},  /* Offset (80-col = 40 words)      */
             {VGA_CRTC_VBSTART, 0x58}, /* Vertical Blank     Start lo (344)*/
             {VGA_CRTC_VBEND, 0x6B},   /* Vertical Blank     End          */
             {0xFF, 0x00} /* sentinel                                    */
         }},
    /* ── 80×50 (400-scanline, 8-line font) ─────────────────── */
    {.cols = 80,
     .rows = 50,
     .char_height = 8,
     .misc_output = 0x67, /* 28 MHz clock, colour, RAM enable        */
     /* Overflow 0x1F: VT[8]=1 VDE[8]=1 VRS[8]=1 VBS[8]=1 LC[8]=1  */
     .overflow = 0x1F,
     .crtc =
         {
             /* Horizontal (identical to 80×25) */
             {VGA_CRTC_HTOTAL, 0x5F},  /* Horizontal Total                */
             {VGA_CRTC_HDEND, 0x4F},   /* Horizontal Display End          */
             {VGA_CRTC_HBSTART, 0x50}, /* Start Horizontal Blank          */
             {VGA_CRTC_HBEND, 0x82},   /* End   Horizontal Blank          */
             {VGA_CRTC_HRSTART, 0x55}, /* Start Horizontal Retrace        */
             {VGA_CRTC_HREND, 0x81},   /* End   Horizontal Retrace        */
             /* Vertical (same 400-scanline base as 80×25, 8-line font)      */
             {VGA_CRTC_VTOTAL, 0xBF},  /* Vertical Total     lo (447)     */
             {VGA_CRTC_MAXSCAN, 0x07}, /* Max Scan Line 7   (8-line font)  */
             {VGA_CRTC_CURSOR_START, 0x06}, /* Cursor Start             */
             {VGA_CRTC_CURSOR_END, 0x07},   /* Cursor End               */
             {VGA_CRTC_VRSTART, 0x9C}, /* Vertical Retrace   Start lo     */
             {VGA_CRTC_VDEND, 0x8F},   /* Vertical Display   End   lo (399)*/
             {VGA_CRTC_OFFSET, 0x28},  /* Offset (80-col = 40 words)      */
             {VGA_CRTC_VBSTART, 0x96}, /* Vertical Blank     Start lo     */
             {VGA_CRTC_VBEND, 0xB9},   /* Vertical Blank     End          */
             {0xFF, 0x00} /* sentinel                                    */
         }},
};

#define VGA_MODE_COUNT ((int)(sizeof(s_VGA_Modes) / sizeof(s_VGA_Modes[0])))

/* ── Sequencer / Graphics Controller helpers ─────────────────────────────── */

static inline uint8_t seq_read(uint8_t idx)
{
   g_HalIoOperations->outb(VGA_SEQ_ADDR, idx);
   return g_HalIoOperations->inb(VGA_SEQ_DATA);
}
static inline void seq_write(uint8_t idx, uint8_t val)
{
   g_HalIoOperations->outb(VGA_SEQ_ADDR, idx);
   g_HalIoOperations->outb(VGA_SEQ_DATA, val);
}
static inline uint8_t gfx_read(uint8_t idx)
{
   g_HalIoOperations->outb(VGA_GFX_ADDR, idx);
   return g_HalIoOperations->inb(VGA_GFX_DATA);
}
static inline void gfx_write(uint8_t idx, uint8_t val)
{
   g_HalIoOperations->outb(VGA_GFX_ADDR, idx);
   g_HalIoOperations->outb(VGA_GFX_DATA, val);
}

/*
 * vga_prepare_font_cache – cache BIOS 8×16 font and derive an 8×8 variant.
 *
 * The 8×8 variant is generated by downsampling rows 0..15 to 0..7 (every
 * second scanline). This keeps full glyph height semantics when switching to
 * 43/50-row text modes.
 */
static void vga_prepare_font_cache(void)
{
   if (s_font_cache_ready) return;

   uint8_t s_seq2 = seq_read(0x02);
   uint8_t s_seq4 = seq_read(0x04);
   uint8_t s_gfx4 = gfx_read(0x04);
   uint8_t s_gfx5 = gfx_read(0x05);
   uint8_t s_gfx6 = gfx_read(0x06);

   /* Access VGA plane 2 as linear bytes at 0xA0000. */
   seq_write(0x04, 0x06); /* extended memory on, odd/even off          */
   gfx_write(0x04, 0x02); /* read from plane 2                         */
   gfx_write(0x05, 0x00); /* read mode 0, write mode 0, no odd/even    */
   gfx_write(0x06, 0x04); /* map at 0xA0000–0xAFFFF, chain-4 disabled  */

   /* Cache BIOS 8×16 font from bank 0. */
   volatile uint8_t *plane = VGA_FONT_PLANE;
   for (int ch = 0; ch < 256; ch++)
   {
      uint32_t base = (uint32_t)ch * VGA_GLYPH_SLOT;
      for (int row = 0; row < 16; row++)
         s_font16[ch * 16 + row] = plane[base + (uint32_t)row];
   }

   /* Derive 8×8 font by downsampling the cached 8×16 glyphs. */
   for (int ch = 0; ch < 256; ch++)
   {
      for (int row = 0; row < 8; row++)
         s_font8[ch * 8 + row] = s_font16[ch * 16 + (row * 2)];
   }

   /* Restore Seq/GFX state. */
   seq_write(0x02, s_seq2);
   seq_write(0x04, s_seq4);
   gfx_write(0x04, s_gfx4);
   gfx_write(0x05, s_gfx5);
   gfx_write(0x06, s_gfx6);

   s_font_cache_ready = true;
}

/*
 * vga_upload_font – upload a character generator into VGA font bank 0.
 */
static void vga_upload_font(int char_height)
{
   vga_prepare_font_cache();

   uint8_t s_seq2 = seq_read(0x02);
   uint8_t s_seq4 = seq_read(0x04);
   uint8_t s_gfx4 = gfx_read(0x04);
   uint8_t s_gfx5 = gfx_read(0x05);
   uint8_t s_gfx6 = gfx_read(0x06);

   seq_write(0x04, 0x06);
   seq_write(0x02, 0x04); /* write plane 2 only */
   gfx_write(0x04, 0x02);
   gfx_write(0x05, 0x00);
   gfx_write(0x06, 0x04);

   volatile uint8_t *plane = VGA_FONT_PLANE;
   for (int ch = 0; ch < 256; ch++)
   {
      uint32_t base = (uint32_t)ch * VGA_GLYPH_SLOT;
      if (char_height == 8)
      {
         for (int row = 0; row < 8; row++)
            plane[base + (uint32_t)row] = s_font8[ch * 8 + row];
         for (int row = 8; row < VGA_GLYPH_SLOT; row++)
            plane[base + (uint32_t)row] = 0x00;
      }
      else
      {
         for (int row = 0; row < 16; row++)
            plane[base + (uint32_t)row] = s_font16[ch * 16 + row];
         for (int row = 16; row < VGA_GLYPH_SLOT; row++)
            plane[base + (uint32_t)row] = 0x00;
      }
   }

   seq_write(0x02, s_seq2);
   seq_write(0x04, s_seq4);
   gfx_write(0x04, s_gfx4);
   gfx_write(0x05, s_gfx5);
   gfx_write(0x06, s_gfx6);
}

static inline void vga_clamp_cursor(void)
{
   if (s_TermCursorX < 0) s_TermCursorX = 0;
   if (s_TermCursorY < 0) s_TermCursorY = 0;
   if (s_TermCursorX >= s_VGA_Cols) s_TermCursorX = s_VGA_Cols - 1;
   if (s_TermCursorY >= s_VGA_Rows) s_TermCursorY = s_VGA_Rows - 1;
}

static void vga_present(void)
{
   uint16_t blank = ((uint16_t)s_TermColor << 8) | ' ';

   for (int row = 0; row < s_VGA_Rows; row++)
   {
      for (int col = 0; col < s_VGA_Cols; col++)
      {
         uint16_t cell = s_TermBuffer[row][col];
         VGA_BUFFER[row * s_VGA_Cols + col] = cell ? cell : blank;
      }
   }
}

static void vga_reset_terminal_state(uint8_t color)
{
   memset(s_TermBuffer, 0, sizeof(s_TermBuffer));
   s_TermCursorX = 0;
   s_TermCursorY = 0;
   s_TermColor = color;
   s_TermDefaultColor = color;
   s_TermInputColor = color;
   s_AnsiState = 0;
   s_AnsiParamCount = 0;
   memset(s_AnsiParams, 0, sizeof(s_AnsiParams));
}

static void vga_scroll_up(void)
{
   if (s_VGA_Rows <= 1) return;

   memmove(s_TermBuffer[0], s_TermBuffer[1],
           (size_t)(s_VGA_Rows - 1) * VGA_TERM_MAX_COLS * sizeof(uint16_t));
   memset(s_TermBuffer[s_VGA_Rows - 1], 0,
          (size_t)VGA_TERM_MAX_COLS * sizeof(uint16_t));
   s_TermCursorY = s_VGA_Rows - 1;
}

static void vga_handle_ansi_sgr(void)
{
   for (int i = 0; i < s_AnsiParamCount; i++)
   {
      int code = s_AnsiParams[i];

      if (code == 0)
      {
         s_TermColor = s_TermDefaultColor;
      }
      else if (code == 1)
      {
         s_TermColor |= 0x08;
      }
      else if (code == 22)
      {
         s_TermColor &= (uint8_t)~0x08;
      }
      else if (code >= 30 && code <= 37)
      {
         s_TermColor =
             (uint8_t)((s_TermColor & 0xF0) | s_AnsiToVgaFg[code - 30]);
      }
      else if (code == 39)
      {
         s_TermColor =
             (uint8_t)((s_TermColor & 0xF0) | (s_TermDefaultColor & 0x0F));
      }
      else if (code >= 40 && code <= 47)
      {
         s_TermColor =
             (uint8_t)((s_TermColor & 0x0F) | s_AnsiToVgaBg[code - 40]);
      }
      else if (code == 49)
      {
         s_TermColor =
             (uint8_t)((s_TermColor & 0x0F) | (s_TermDefaultColor & 0xF0));
      }
      else if (code >= 90 && code <= 97)
      {
         s_TermColor = (uint8_t)((s_TermColor & 0xF0) |
                                 (s_AnsiToVgaFg[code - 90] | 0x08));
      }
   }
}

static void vga_handle_ansi_command(char cmd)
{
   int n = (s_AnsiParamCount > 0) ? s_AnsiParams[0] : 1;
   if (n < 1) n = 1;

   switch (cmd)
   {
   case 'A':
      s_TermCursorY -= n;
      break;
   case 'B':
      s_TermCursorY += n;
      break;
   case 'C':
      s_TermCursorX += n;
      break;
   case 'D':
      s_TermCursorX -= n;
      break;
   case 'H':
   case 'f':
   {
      int row = (s_AnsiParamCount > 0) ? s_AnsiParams[0] : 1;
      int col = (s_AnsiParamCount > 1) ? s_AnsiParams[1] : 1;
      if (row < 1) row = 1;
      if (col < 1) col = 1;
      s_TermCursorY = row - 1;
      s_TermCursorX = col - 1;
      break;
   }
   case 'J':
      if (n == 2)
      {
         memset(s_TermBuffer, 0, sizeof(s_TermBuffer));
         s_TermCursorX = 0;
         s_TermCursorY = 0;
         vga_present();
      }
      break;
   case 'K':
   {
      uint16_t *row = s_TermBuffer[s_TermCursorY];

      if (n == 0)
         memset(&row[s_TermCursorX], 0,
                (size_t)(s_VGA_Cols - s_TermCursorX) * sizeof(uint16_t));
      else if (n == 1)
         memset(row, 0, (size_t)(s_TermCursorX + 1) * sizeof(uint16_t));
      else
         memset(row, 0, (size_t)s_VGA_Cols * sizeof(uint16_t));

      vga_present();
      break;
   }
   case 'm':
      vga_handle_ansi_sgr();
      break;
   default:
      break;
   }

   vga_clamp_cursor();
}

static int vga_process_ansi(char c)
{
   if (s_AnsiState == 0)
   {
      if (c == '\x1B')
      {
         s_AnsiState = 1;
         return 0;
      }
      return -1;
   }

   if (s_AnsiState == 1)
   {
      if (c == '[')
      {
         s_AnsiState = 2;
         s_AnsiParamCount = 0;
         s_AnsiParams[0] = 0;
         return 0;
      }
      s_AnsiState = 0;
      return 0;
   }

   if (s_AnsiState == 2)
   {
      if (c >= '0' && c <= '9')
      {
         s_AnsiParams[s_AnsiParamCount] =
             s_AnsiParams[s_AnsiParamCount] * 10 + (c - '0');
         return 0;
      }
      if (c == ';')
      {
         s_AnsiParamCount++;
         if (s_AnsiParamCount >= VGA_ANSI_PARAM_MAX)
            s_AnsiParamCount = VGA_ANSI_PARAM_MAX - 1;
         s_AnsiParams[s_AnsiParamCount] = 0;
         return 0;
      }
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
      {
         s_AnsiParamCount++;
         vga_handle_ansi_command(c);
         s_AnsiState = 0;
         return 0;
      }
      if (c == '?')
      {
         return 0;
      }

      s_AnsiState = 0;
      return 0;
   }

   return -1;
}

static void vga_stream_put_char(char c)
{
   bool repaint = false;

   if (vga_process_ansi(c) == 0)
   {
      i686_VGA_SetCursor(s_TermCursorX, s_TermCursorY);
      return;
   }

   if (c == '\n')
   {
      s_TermCursorX = 0;
      s_TermCursorY++;
      if (s_TermCursorY >= s_VGA_Rows) vga_scroll_up();
      repaint = true;
   }
   else if (c == '\r')
   {
      s_TermCursorX = 0;
   }
   else if (c == '\t')
   {
      int spaces = 4 - (s_TermCursorX % 4);
      for (int i = 0; i < spaces; i++)
      {
         vga_stream_put_char(' ');
      }
      return;
   }
   else if (c == '\b')
   {
      if (s_TermCursorX > 0)
      {
         s_TermCursorX--;
         memmove(&s_TermBuffer[s_TermCursorY][s_TermCursorX],
                 &s_TermBuffer[s_TermCursorY][s_TermCursorX + 1],
                 (size_t)(s_VGA_Cols - s_TermCursorX - 1) * sizeof(uint16_t));
         s_TermBuffer[s_TermCursorY][s_VGA_Cols - 1] = 0;
         repaint = true;
      }
   }
   else if ((unsigned char)c >= 0x20)
   {
      s_TermBuffer[s_TermCursorY][s_TermCursorX] =
          ((uint16_t)s_TermColor << 8) | (uint8_t)c;
      s_TermCursorX++;

      if (s_TermCursorX >= s_VGA_Cols)
      {
         s_TermCursorX = 0;
         s_TermCursorY++;
         if (s_TermCursorY >= s_VGA_Rows) vga_scroll_up();
      }

      repaint = true;
   }

   if (repaint) vga_present();
   i686_VGA_SetCursor(s_TermCursorX, s_TermCursorY);
}

/* ── Backend implementation ──────────────────────────────────────────────── */

/*
 * VGA_Initialize — one-time hardware setup.
 *
 * Programs the CRT controller cursor-shape registers so that the blinking
 * underline cursor is visible in 80×25 text mode:
 *   - Register 0x0A (Cursor Start): scan line 14, bit 5 clear (cursor on).
 *   - Register 0x0B (Cursor End):   scan line 15.
 * Then homes the cursor to (0, 0).
 */
void i686_VGA_Initialize(void)
{
   /* Cursor Start: enable cursor (bit 5 = 0), start scan line 14 */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_START);
   g_HalIoOperations->outb(VGA_CRTC_DATA, 0x0E);

   /* Cursor End: end scan line 15 */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_END);
   g_HalIoOperations->outb(VGA_CRTC_DATA, 0x0F);

   s_VGA_Cols = 80;
   s_VGA_Rows = 25;
   vga_reset_terminal_state(0x07);
   vga_present();
   i686_VGA_SetCursor(0, 0);
}

/*
 * VGA_SetDisplaySize — switch to a supported VGA text mode.
 *
 * The CRTC protect bit (register 0x11 bit 7) is cleared before writing any
 * register and restored afterwards so that registers 0x00–0x07 are writable.
 *
 * Returns  0 on success.
 * Returns -1 and leaves the current mode unchanged if (cols, rows) does not
 *          match any entry in s_VGA_Modes[].
 */
int i686_VGA_SetDisplaySize(int cols, int rows)
{
   /* ── 1. Find the requested mode ───────────────────────── */
   const VGA_ModeDesc *mode = (void *)0;
   for (int i = 0; i < VGA_MODE_COUNT; i++)
   {
      if (s_VGA_Modes[i].cols == cols && s_VGA_Modes[i].rows == rows)
      {
         mode = &s_VGA_Modes[i];
         break;
      }
   }

   if (!mode) return -1; /* unsupported mode – do not change anything */

   /* ── 2. Sequencer synchronous reset ───────────────────────
    * Writing 0x01 to SEQ index 0 halts the sequencer.  This is the
    * signal emulators (QEMU, Bochs) watch to re-evaluate the video mode
    * and resize the host window.  Must be cleared (0x03) afterwards. */
   seq_write(0x00, 0x01);

   /* ── 3. Miscellaneous Output Register ─────────────────────
    * Sets dot-clock frequency and H/V sync polarities.  Sync polarity
    * encodes the total scanline count for the monitor:
    *   -H, +V (bits 7:6 = 00 in QEMU convention) → 400 lines
    *   +V, +H (bits 7:6 = 10)                    → 350 lines    */
   g_HalIoOperations->outb(VGA_MISC_OUT, mode->misc_output);

   /* ── 4. Unlock CRTC registers 0x00–0x07 ───────────────── */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_VREND);
   uint8_t vrend = g_HalIoOperations->inb(VGA_CRTC_DATA);
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_VREND);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)(vrend & ~0x80u));

   /* ── 5. Program CRTC Overflow (0x07) from dedicated struct field ──
    * Register 0x07 carries the 8th/9th bits of VT, VDE, VRS, VBS, and
    * the Line Compare (LC) bit; it falls inside the protect range
    * 0x00–0x07 so it must be written while the protect bit is clear.
    * Setting it to 0x1F places LC[8]=1 (bit 4), preventing a spurious
    * split-screen in the lower half of a 400-line frame.               */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_OVERFLOW);
   g_HalIoOperations->outb(VGA_CRTC_DATA, mode->overflow);

   /* ── 6. Program each remaining CRTC register from the table ─────── */
   for (int i = 0; mode->crtc[i].reg != 0xFF; i++)
   {
      g_HalIoOperations->outb(VGA_CRTC_ADDR, mode->crtc[i].reg);
      g_HalIoOperations->outb(VGA_CRTC_DATA, mode->crtc[i].val);
   }

   /* ── 7. Re-lock CRTC ───────────────────────────────────── */
   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_VREND);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)(vrend | 0x80u));

   /* ── 8. Upload matching font (sequencer still in reset for plane access) */
   vga_upload_font(mode->char_height);

   /* ── 9. Reset Attribute Controller flip-flop and re-enable video ──
    * Done while the sequencer is still held in reset so the Palette
    * Address Source (PAS) latch is fully committed before pixel output
    * resumes.  Reading VGA_ISTAT1 resets the AC flip-flop to 'address'
    * state; writing VGA_AC_PAS (index 0x20) sets the PAS bit, which
    * prevents the display from blanking when the sequencer restarts.   */
   (void)g_HalIoOperations->inb(VGA_ISTAT1);
   g_HalIoOperations->outb(VGA_AC_ADDR, VGA_AC_PAS);

   /* ── 10. Clear sequencer reset – restarts pixel output ───
    * This is the final step: pixel clock resumes after all timing
    * registers, font data, and the PAS latch are fully settled.        */
   seq_write(0x00, 0x03);

   /* ── 11. Update tracked dimensions ─────────────────────── */
   s_VGA_Cols = cols;
   s_VGA_Rows = rows;
   vga_clamp_cursor();
   vga_present();
   i686_VGA_SetCursor(s_TermCursorX, s_TermCursorY);

   return 0;
}

/*
 * VGA_PutChar — write one character directly into VGA VRAM.
 *
 * In stream mode (x/y < 0) this routes through the internal terminal parser,
 * including ANSI handling and cursor movement.
 */
void i686_VGA_PutChar(char c, uint8_t color, int x, int y)
{
   if (x < 0 || y < 0)
   {
      /* External colour (TTY default) should not clobber active ANSI colour
       * on each character.  Only adopt it when the caller changes it. */
      if (color != s_TermInputColor)
      {
         s_TermInputColor = color;
         s_TermDefaultColor = color;
         if (s_AnsiState == 0)
         {
            s_TermColor = color;
         }
      }
      vga_stream_put_char(c);
      return;
   }

   if (x >= s_VGA_Cols || y >= s_VGA_Rows) return;

   VGA_BUFFER[y * s_VGA_Cols + x] = ((uint16_t)color << 8) | (uint8_t)c;
   if (x < VGA_TERM_MAX_COLS && y < VGA_TERM_MAX_ROWS)
   {
      s_TermBuffer[y][x] = ((uint16_t)color << 8) | (uint8_t)c;
   }
}

/*
 * VGA_Clear — fill all 80×25 cells with spaces in the requested colour.
 */
void i686_VGA_Clear(uint8_t color)
{
   vga_reset_terminal_state(color);
   vga_present();
   i686_VGA_SetCursor(0, 0);
}

/*
 * VGA_SetCursor — program the CRT controller to move the hardware cursor.
 *
 * The CRT controller expects a linear cell offset:
 *
 *   Offset = (y × s_VGA_Cols) + x
 *
 * sent as two 8-bit writes to VGA_CRTC_ADDR / VGA_CRTC_DATA:
 *   high byte → VGA_CRTC_CURSOR_HI
 *   low  byte → VGA_CRTC_CURSOR_LO
 */
void i686_VGA_SetCursor(int x, int y)
{
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   if (x >= s_VGA_Cols) x = s_VGA_Cols - 1;
   if (y >= s_VGA_Rows) y = s_VGA_Rows - 1;

   s_TermCursorX = x;
   s_TermCursorY = y;

   uint16_t pos = (uint16_t)(y * s_VGA_Cols + x);

   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_HI);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));

   g_HalIoOperations->outb(VGA_CRTC_ADDR, VGA_CRTC_CURSOR_LO);
   g_HalIoOperations->outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
}

void i686_VGA_GetCursor(int *x, int *y)
{
   if (x) *x = s_TermCursorX;
   if (y) *y = s_TermCursorY;
}
