// SPDX-License-Identifier: GPL-3.0-only

#include "video.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void putc(char c)
{
   switch (preferredOutput)
   {
   case OUTPUT_VBE:
      VBE_PutChar(c, -1, -1, VGATEXT_DEFAULT_COLOR);
      break;
   case OUTPUT_VGA:
      VGA_PutChar(c, -1, -1, VGATEXT_DEFAULT_COLOR);
      break;
   case OUTPUT_VGATEXT:
      VGATEXT_PutChar(c, -1, -1, VGATEXT_DEFAULT_COLOR);
      break;
   case OUTPUT_SERIAL:
      Serial_PutChar(c, -1, -1, VGATEXT_DEFAULT_COLOR);
      break;
   default:
      VGATEXT_PutChar(c, -1, -1, VGATEXT_DEFAULT_COLOR);
      break;
   }
}

static const char g_HexLower[] = "0123456789abcdef";
static const char g_HexUpper[] = "0123456789ABCDEF";

static void put_reverse(const char *buf, int len)
{
   while (--len >= 0) putc(buf[len]);
}

static void put_unsigned(unsigned long long val, unsigned radix,
                         const char *digits, int min_width, bool zero_pad)
{
   char buf[68]; /* 64 bits in binary = up to 64 chars + sign */
   int pos = 0;

   if (radix < 2 || radix > 16) return;

   do
   {
      buf[pos++] = digits[val % radix];
      val /= radix;
   } while (val > 0 && pos < (int)sizeof(buf));

   while (pos < min_width && pos < (int)sizeof(buf))
      buf[pos++] = zero_pad ? '0' : ' ';

   put_reverse(buf, pos);
}

static void put_signed(long long val, unsigned radix, const char *digits,
                       int min_width, bool zero_pad)
{
   if (val < 0)
   {
      putc('-');
      put_unsigned(-(unsigned long long)val, radix, digits,
                   min_width > 0 ? min_width - 1 : 0, zero_pad);
   }
   else
   {
      put_unsigned((unsigned long long)val, radix, digits, min_width, zero_pad);
   }
}

void puts(const char *str)
{
   if (!str) return;
   for (; *str; ++str) putc(*str);
}

void puti(int val) { put_signed(val, 10, g_HexLower, 0, false); }
void putd(int val) { put_signed(val, 10, g_HexLower, 0, false); }
void putl(long val) { put_signed(val, 10, g_HexLower, 0, false); }
void putll(long long val) { put_signed(val, 10, g_HexLower, 0, false); }

void putu(unsigned val) { put_unsigned(val, 10, g_HexLower, 0, false); }
void putul(unsigned long val) { put_unsigned(val, 10, g_HexLower, 0, false); }
void putull(unsigned long long val)
{
   put_unsigned(val, 10, g_HexLower, 0, false);
}

void putx(unsigned long long val)
{
   put_unsigned(val, 16, g_HexLower, 0, false);
}
void putX(unsigned long long val)
{
   put_unsigned(val, 16, g_HexUpper, 0, false);
}

void puto(unsigned long long val)
{
   put_unsigned(val, 8, g_HexLower, 0, false);
}

void putb(unsigned long long val)
{
   put_unsigned(val, 2, g_HexLower, 0, false);
}

void putp(const void *ptr)
{
   puts("0x");
   if (sizeof(ptr) == 8)
      put_unsigned((unsigned long long)(uintptr_t)ptr, 16, g_HexLower, 16,
                   true);
   else
      put_unsigned((unsigned long long)(uintptr_t)ptr, 16, g_HexLower, 8, true);
}