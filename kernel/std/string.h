// SPDX-License-Identifier: GPL-3.0-only

#pragma once

const char *strchr(const char *str, char chr);
char *strcpy(char *dst, const char *src);
unsigned strlen(const char *str);

// Copy up to n characters from src to dst. Returns dst.
char *strncpy(char *dst, const char *src, unsigned n);

// Compare strings. Returns 0 if equal, non-zero otherwise.
int strcmp(const char *a, const char *b);

// Compare strings for equality. Returns 1 if equal, 0 otherwise.
int str_eq(const char *a, const char *b);

char *strrchr(const char *s, int c);