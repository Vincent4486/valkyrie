// SPDX-License-Identifier: GPL-3.0-only

#include "stdio.h"
#include <hal/io.h>

#include <mem/mm_kernel.h>
#include <stdarg.h>
#include <stdbool.h>

#include <drivers/tty/tty.h>

/* Simple non-blocking getchar that uses the TTY input stream. Returns -1
   when no character is available. */
void setcursor(int x, int y)
{
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   uint16_t pos = (uint16_t)(y * 80 + x);
   g_HalIoOperations->outb(0x3D4, 0x0F);
   g_HalIoOperations->outb(0x3D5, (uint8_t)(pos & 0xFF));
   g_HalIoOperations->outb(0x3D4, 0x0E);
   g_HalIoOperations->outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

int getchar(void)
{
   extern int TTY_ReadChar(void);
   return TTY_ReadChar();
}

void putc(char c)
{
   g_HalIoOperations->outb(0xe9, c);
   TTY_PutChar(c);
}

void puts(const char *str)
{
   for (int i = 0; str[i]; i++) putc(str[i]);
}

const char g_HexChars[] = "0123456789abcdef";

void printf_unsigned(unsigned long long number, int radix, int width,
                     bool zero_pad)
{
   char buffer[32];
   int pos = 0;

   // Limit width to buffer size to prevent stack overflow
   if (width > 31) width = 31;

   // convert number to ASCII
   do
   {
      unsigned long long rem = number % radix;
      number /= radix;
      buffer[pos++] = g_HexChars[rem];
   } while (number > 0 && pos < 31);

   // pad with zeros or spaces if needed
   while (pos < width)
   {
      buffer[pos++] = zero_pad ? '0' : ' ';
   }

   // print number in reverse order
   while (--pos >= 0) putc(buffer[pos]);
}

void printf_signed(long long number, int radix, int width, bool zero_pad)
{
   if (number < 0)
   {
      putc('-');
      printf_unsigned(-number, radix, (width > 0 ? width - 1 : 0), zero_pad);
   }
   else
      printf_unsigned(number, radix, width, zero_pad);
}

#define PRINTF_STATE_NORMAL 0
#define PRINTF_STATE_LENGTH 1
#define PRINTF_STATE_LENGTH_SHORT 2
#define PRINTF_STATE_LENGTH_LONG 3
#define PRINTF_STATE_SPEC 4

#define PRINTF_LENGTH_DEFAULT 0
#define PRINTF_LENGTH_SHORT_SHORT 1
#define PRINTF_LENGTH_SHORT 2
#define PRINTF_LENGTH_LONG 3
#define PRINTF_LENGTH_LONG_LONG 4

void printf(const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);

   int state = PRINTF_STATE_NORMAL;
   int length = PRINTF_LENGTH_DEFAULT;
   int radix = 10;
   bool sign = false;
   bool number = false;
   int width = 0;
   bool zero_pad = false;

   while (*fmt)
   {
      switch (state)
      {
      case PRINTF_STATE_NORMAL:
         switch (*fmt)
         {
         case '%':
            state = PRINTF_STATE_LENGTH;
            width = 0;
            zero_pad = false;
            break;
         default:
            putc(*fmt);
            break;
         }
         break;

      case PRINTF_STATE_LENGTH:
         switch (*fmt)
         {
         case '0':
            zero_pad = true;
            break;
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
            width = width * 10 + (*fmt - '0');
            break;
         case 'h':
            length = PRINTF_LENGTH_SHORT;
            state = PRINTF_STATE_LENGTH_SHORT;
            break;
         case 'l':
            length = PRINTF_LENGTH_LONG;
            state = PRINTF_STATE_LENGTH_LONG;
            break;
         default:
            goto PRINTF_STATE_SPEC_;
         }
         break;

      case PRINTF_STATE_LENGTH_SHORT:
         if (*fmt == 'h')
         {
            length = PRINTF_LENGTH_SHORT_SHORT;
            state = PRINTF_STATE_SPEC;
         }
         else
            goto PRINTF_STATE_SPEC_;
         break;

      case PRINTF_STATE_LENGTH_LONG:
         if (*fmt == 'l')
         {
            length = PRINTF_LENGTH_LONG_LONG;
            state = PRINTF_STATE_SPEC;
         }
         else
            goto PRINTF_STATE_SPEC_;
         break;

      case PRINTF_STATE_SPEC:
      PRINTF_STATE_SPEC_:
         switch (*fmt)
         {
         case 'c':
            putc((char)va_arg(args, int));
            break;

         case 's':
            puts(va_arg(args, const char *));
            break;

         case '%':
            putc('%');
            break;

         case 'd':
         case 'i':
            radix = 10;
            sign = true;
            number = true;
            break;

         case 'u':
            radix = 10;
            sign = false;
            number = true;
            break;

         case 'X':
         case 'x':
         case 'p':
            radix = 16;
            sign = false;
            number = true;
            break;

         case 'o':
            radix = 8;
            sign = false;
            number = true;
            break;

         // ignore invalid spec
         default:
            break;
         }

         if (number)
         {
            if (sign)
            {
               switch (length)
               {
               case PRINTF_LENGTH_SHORT_SHORT:
               case PRINTF_LENGTH_SHORT:
               case PRINTF_LENGTH_DEFAULT:
                  printf_signed(va_arg(args, int), radix, width, zero_pad);
                  break;

               case PRINTF_LENGTH_LONG:
                  printf_signed(va_arg(args, long), radix, width, zero_pad);
                  break;

               case PRINTF_LENGTH_LONG_LONG:
                  printf_signed(va_arg(args, long long), radix, width,
                                zero_pad);
                  break;
               }
            }
            else
            {
               switch (length)
               {
               case PRINTF_LENGTH_SHORT_SHORT:
               case PRINTF_LENGTH_SHORT:
               case PRINTF_LENGTH_DEFAULT:
                  printf_unsigned(va_arg(args, unsigned int), radix, width,
                                  zero_pad);
                  break;

               case PRINTF_LENGTH_LONG:
                  printf_unsigned(va_arg(args, unsigned long), radix, width,
                                  zero_pad);
                  break;

               case PRINTF_LENGTH_LONG_LONG:
                  printf_unsigned(va_arg(args, unsigned long long), radix,
                                  width, zero_pad);
                  break;
               }
            }
         }

         // reset state
         state = PRINTF_STATE_NORMAL;
         length = PRINTF_LENGTH_DEFAULT;
         radix = 10;
         sign = false;
         number = false;
         width = 0;
         zero_pad = false;
         break;
      }

      fmt++;
   }

   va_end(args);
}

void print_buffer(const char *msg, const void *buffer, uint32_t count)
{
   const uint8_t *u8Buffer = (const uint8_t *)buffer;
   puts(msg);
   for (uint16_t i = 0; i < count; i++)
   {
      putc(g_HexChars[u8Buffer[i] >> 4]);
      putc(g_HexChars[u8Buffer[i] & 0xF]);
   }
   putc('\n');
}

int snprintf(char *buffer, size_t buf_size, const char *format, ...)
{
   va_list ap;
   va_start(ap, format);
   int result = vsnprintf(buffer, buf_size, format, ap);
   va_end(ap);
   return result;
}

int vsnprintf(char *buffer, size_t buf_size, const char *format, va_list ap)
{
   size_t out_idx = 0;    /* next write position in buffer (0..buf_size-1) */
   size_t would_have = 0; /* total chars that would have been written */

/* helper to emit a single char */
#define EMIT_CH(c)                                                             \
   do                                                                          \
   {                                                                           \
      if (out_idx + 1 < buf_size) buffer[out_idx++] = (c);                     \
      would_have++;                                                            \
   } while (0)

/* helper to emit a whole string */
#define EMIT_STR(s)                                                            \
   do                                                                          \
   {                                                                           \
      const char *_p = (s);                                                    \
      while (*_p)                                                              \
      {                                                                        \
         EMIT_CH(*_p);                                                         \
         _p++;                                                                 \
      }                                                                        \
   } while (0)

   const char *p = format;
   while (*p)
   {
      if (*p != '%')
      {
         EMIT_CH(*p);
         p++;
         continue;
      }
      /* handle % */
      p++;
      if (*p == '%')
      {
         EMIT_CH('%');
         p++;
         continue;
      }

      /* No length modifiers supported here; keep it small for kernel */
      char spec = *p++;
      if (!spec) break;
      if (spec == 'c')
      {
         int ch = va_arg(ap, int);
         EMIT_CH((char)ch);
      }
      else if (spec == 's')
      {
         const char *s = va_arg(ap, const char *);
         if (!s) s = "(null)";
         EMIT_STR(s);
      }
      else if (spec == 'd' || spec == 'i')
      {
         long long val = va_arg(ap, int);
         if (val < 0)
         {
            EMIT_CH('-');
            val = -val;
         }
         /* unsigned print */
         char tmp[32];
         int t = 0;
         if (val == 0) tmp[t++] = '0';
         while (val > 0 && t < (int)sizeof(tmp))
         {
            tmp[t++] = '0' + (val % 10);
            val /= 10;
         }
         while (t--) EMIT_CH(tmp[t]);
      }
      else if (spec == 'u')
      {
         unsigned long long val = va_arg(ap, unsigned int);
         char tmp[32];
         int t = 0;
         if (val == 0) tmp[t++] = '0';
         while (val > 0 && t < (int)sizeof(tmp))
         {
            tmp[t++] = '0' + (val % 10);
            val /= 10;
         }
         while (t--) EMIT_CH(tmp[t]);
      }
      else if (spec == 'x' || spec == 'X' || spec == 'p')
      {
         unsigned long long val = va_arg(ap, unsigned long long);
         char tmp[32];
         int t = 0;
         const char *hex =
             (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
         if (val == 0) tmp[t++] = '0';
         while (val > 0 && t < (int)sizeof(tmp))
         {
            tmp[t++] = hex[val & 0xF];
            val >>= 4;
         }
         while (t--) EMIT_CH(tmp[t]);
      }
      else if (spec == 'o')
      {
         unsigned long long val = va_arg(ap, unsigned int);
         char tmp[32];
         int t = 0;
         if (val == 0) tmp[t++] = '0';
         while (val > 0 && t < (int)sizeof(tmp))
         {
            tmp[t++] = '0' + (val & 7);
            val >>= 3;
         }
         while (t--) EMIT_CH(tmp[t]);
      }
      else
      {
         /* unsupported specifier: emit it literally */
         EMIT_CH('%');
         EMIT_CH(spec);
      }
   }

   /* NUL-terminate if there's space */
   if (buf_size > 0)
   {
      if (out_idx < buf_size)
         buffer[out_idx] = '\0';
      else
         buffer[buf_size - 1] = '\0';
   }

   return (int)would_have;
}
