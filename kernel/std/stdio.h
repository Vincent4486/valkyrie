// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

   typedef enum {
      LOG_INFO = 0,
      LOG_WARNING = 1,
      LOG_ERROR = 2,
      LOG_FATAL = 3
   } LogType;

   void clrscr();
   void putc(char c);
   void puts(const char *str);
   void printf(const char *fmt, ...);
   /* logfmt expands to printf with prefix and ANSI colors, reset at end */
   #define logfmt(logtype, fmt, ...) printf("%s" fmt "%s", \
      (logtype == LOG_INFO ? "\x1B[37mINFO: " : \
       logtype == LOG_WARNING ? "\x1B[33mWARNING: " : \
       logtype == LOG_ERROR ? "\x1B[31mERROR: " : \
       logtype == LOG_FATAL ? "\x1B[41;37mFATAL: " : "UNKNOWN: "), \
      ##__VA_ARGS__, "\x1B[0m")
   void print_buffer(const char *msg, const void *buffer, uint32_t count);
   void setcursor(int x, int y);
   int snprintf(char *buffer, size_t buf_size, const char *format, ...);
   int vsnprintf(char *buffer, size_t buf_size, const char *format, va_list ap);

#ifdef __cplusplus
}
#endif
