// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

   typedef enum
   {
      LOG_INFO = 0,
      LOG_WARNING = 1,
      LOG_ERROR = 2,
      LOG_FATAL = 3
   } LogType;

   void clrscr();
   void putc(char c);
   void puts(const char *str);
   void printf(const char *fmt, ...);
   void vprintf(const char *fmt, va_list args);
   void logfmt_impl(LogType logtype, const char *fmt, ...);
   void LOG_DisableInfo(void);
/* logfmt expands to printf with prefix and ANSI colors, reset at end */
#define logfmt(logtype, fmt, ...) logfmt_impl((logtype), (fmt), ##__VA_ARGS__)
   void print_buffer(const char *msg, const void *buffer, uint32_t count);
   void setcursor(int x, int y);
   /* Read one byte from TTY input stream. Returns -1 if no data. */
   int getchar(void);
   int snprintf(char *buffer, size_t buf_size, const char *format, ...);
   int vsnprintf(char *buffer, size_t buf_size, const char *format, va_list ap);

#ifdef __cplusplus
}
#endif
