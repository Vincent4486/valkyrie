// SPDX-License-Identifier: GPL-3.0-only

#include "string.h"
#include <stddef.h>
#include <stdint.h>

const char *strchr(const char *str, char chr)
{
   if (str == NULL) return NULL;

   while (*str)
   {
      if (*str == chr) return str;

      ++str;
   }

   return NULL;
}

char *strcpy(char *dst, const char *src)
{
   char *origDst = dst;

   if (dst == NULL) return NULL;

   if (src == NULL)
   {
      *dst = '\0';
      return dst;
   }

   while (*src)
   {
      *dst = *src;
      ++src;
      ++dst;
   }

   *dst = '\0';
   return origDst;
}

unsigned strlen(const char *str)
{
   unsigned len = 0;
   while (*str)
   {
      ++len;
      ++str;
   }

   return len;
}

int str_eq(const char *a, const char *b)
{
   if (a == NULL || b == NULL) return 0;
   while (*a && *b)
   {
      if (*a != *b) return 0;
      ++a;
      ++b;
   }
   return *a == *b;
}

char *strncpy(char *dst, const char *src, unsigned n)
{
   char *origDst = dst;

   if (dst == NULL) return NULL;

   if (src == NULL)
   {
      while (n > 0)
      {
         *dst = '\0';
         ++dst;
         --n;
      }
      return origDst;
   }

   while (n > 0 && *src)
   {
      *dst = *src;
      ++src;
      ++dst;
      --n;
   }

   // Pad remaining with null bytes
   while (n > 0)
   {
      *dst = '\0';
      ++dst;
      --n;
   }

   return origDst;
}

int strcmp(const char *a, const char *b)
{
   if (a == NULL && b == NULL) return 0;
   if (a == NULL) return -1;
   if (b == NULL) return 1;

   while (*a && *b)
   {
      if (*a < *b) return -1;
      if (*a > *b) return 1;
      ++a;
      ++b;
   }

   if (*a == *b) return 0;
   return (*a < *b) ? -1 : 1;
}

char *strrchr(const char *s, int c)
{
   const char *last_occurrence =
       NULL;                   // Pointer to store the last found position
   char search_char = (char)c; // Convert int c to char

   // Iterate through the string until the null terminator is reached
   while (*s != '\0')
   {
      if (*s == search_char)
      {
         last_occurrence =
             s; // Update last_occurrence if the character is found
      }
      s++; // Move to the next character in the string
   }

   // After the loop, check if the character was the null terminator itself
   if (search_char == '\0')
   {
      last_occurrence =
          s; // If searching for null, its last occurrence is at the end
   }

   return (char *)
       last_occurrence; // Return the pointer to the last occurrence (or NULL)
}
