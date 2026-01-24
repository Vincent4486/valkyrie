// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARCH_I686_DRIVERS_TTY_H
#define ARCH_I686_DRIVERS_TTY_H

#include <stdint.h>

void i686_tty_putc(int x, int y, char c, uint8_t color);
char i686_tty_getc(int x, int y);
void i686_tty_set_cursor(int x, int y);
void i686_tty_clear(void);

#endif