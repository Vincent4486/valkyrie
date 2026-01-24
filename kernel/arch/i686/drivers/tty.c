// SPDX-License-Identifier: GPL-3.0-only

#include <hal/io.h>
#include <stdint.h>

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define VGA_BUFFER ((uint16_t *)0xB8000)
#define DEFAULT_COLOR 0x07

void i686_tty_putc(int x, int y, char c, uint8_t color)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    VGA_BUFFER[y * SCREEN_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
}

char i686_tty_getc(int x, int y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return 0;
    return (char)(VGA_BUFFER[y * SCREEN_WIDTH + x] & 0xFF);
}

void i686_tty_set_cursor(int x, int y)
{
    if (x < 0 || x >= SCREEN_WIDTH) x = 0;
    if (y < 0 || y >= SCREEN_HEIGHT) y = 0;
    int pos = y * SCREEN_WIDTH + x;
    g_HalIoOperations->outb(0x3D4, 0x0F);
    g_HalIoOperations->outb(0x3D5, (uint8_t)(pos & 0xFF));
    g_HalIoOperations->outb(0x3D4, 0x0E);
    g_HalIoOperations->outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void i686_tty_clear(void)
{
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            i686_tty_putc(x, y, ' ', DEFAULT_COLOR);
    i686_tty_set_cursor(0, 0);
}