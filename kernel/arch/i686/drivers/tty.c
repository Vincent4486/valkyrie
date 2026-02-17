// SPDX-License-Identifier: GPL-3.0-only

#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <stdint.h>

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define VGA_BUFFER ((uint16_t *)0xB8000)
#define DEFAULT_COLOR 0x07

void i686_TTY_UpdateVga(uint16_t *buff) { memcpy(VGA_BUFFER, buff, 4000); }