// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <stddef.h>
#include <stdint.h>

#include <constants.h>

/* Output system identifiers (availability bit positions). */
#define OUTPUT_VBE 0
#define OUTPUT_VGA 1
#define OUTPUT_VGATEXT 2
#define OUTPUT_SERIAL 3

/* VGA text-mode defaults. */
#define VGATEXT_DEFAULT_COLOR 0x0A
#define VGATEXT_WIDTH 80
#define VGATEXT_HEIGHT 25

/* ================================================================== */
/*  Structures                                                         */
/* ================================================================== */

/* VBE framebuffer details (populated from boot-time mode selection). */
typedef struct VBE_Info
{
   uint64_t framebuffer_addr;
   uint32_t pitch;
   uint32_t width;
   uint32_t height;
   uint8_t bpp;
   uint8_t red_field_position;
   uint8_t red_mask_size;
   uint8_t green_field_position;
   uint8_t green_mask_size;
   uint8_t blue_field_position;
   uint8_t blue_mask_size;
} VBE_Info;

/* ================================================================== */
/*  Functions                                                          */
/* ================================================================== */

extern void outb(uint16_t port, uint8_t val); /* Write port byte. */
extern uint8_t inb(uint16_t port);            /* Read port byte. */

extern int preferredOutput; /* Preferred output system. */

int VGATEXT_Initialize(void); /* Initialize VGA text mode. */
int VGATEXT_PutChar(char c, int x, int y, char color); /* Put text char. */
int VGATEXT_PutPixel(int pixel, int x, int y);         /* Text mode: -EINVAL. */

int Serial_Initialize(void); /* Initialize COM1 serial. */
int Serial_PutChar(char c, int x, int y, char color); /* Put serial char. */
int Serial_PutPixel(int pixel, int x, int y);         /* Serial: -EINVAL. */

int VBE_Initialize(void); /* Initialize VBE framebuffer. */
int VBE_PutChar(char c, int x, int y, char color);     /* Put VBE char. */
int VBE_PutPixel(uint32_t pixel, int x, int y);        /* Put VBE pixel. */
uint32_t VBE_PackRGB(uint8_t r, uint8_t g, uint8_t b); /* Pack RGB for VBE. */
void VBE_SetInfo(const VBE_Info *info); /* Set VBE info before init. */
int VBE_HasInfo(void);                  /* Check if VBE info is available. */
uint32_t VBE_GetWidth(void);            /* Get current VBE width. */
uint32_t VBE_GetHeight(void);           /* Get current VBE height. */
void VBE_ClearScreen(uint32_t pixel);   /* Clears the current VBE screen */

#ifndef CORE
void LOGO_Draw(void); /* Draw boot logo (VBE only). */
#endif                /* !CORE */

int VGA_Initialize(void); /* Initialize VGA graphics. */
int VGA_PutChar(char c, int x, int y, char color); /* Put VGA char. */
int VGA_PutPixel(int pixel, int x, int y);         /* Put VGA pixel. */

void putc(char c);                   /* Write a single character. */
void puts(const char *str);          /* Write a null-terminated string. */
void puti(int val);                  /* Write signed int as decimal. */
void putd(int val);                  /* Write signed int as decimal. */
void putl(long val);                 /* Write signed long as decimal. */
void putll(long long val);           /* Write signed long long as decimal. */
void putu(unsigned val);             /* Write unsigned int as decimal. */
void putul(unsigned long val);       /* Write unsigned long as decimal. */
void putull(unsigned long long val); /* Write unsigned long long as decimal. */
void putx(unsigned long long val);   /* Write unsigned long long as hex. */
void putX(unsigned long long val);   /* Write unsigned long long as HEX. */
void puto(unsigned long long val);   /* Write unsigned long long as octal. */
void putb(unsigned long long val);   /* Write unsigned long long as binary. */
void putp(const void *ptr);          /* Write pointer as 0x hex. */