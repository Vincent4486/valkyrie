// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_PS2_H
#define I686_PS2_H

#include <stdint.h>

/**
 * i686-specific PS/2 keyboard driver
 * Handles port I/O, IRQ registration, and platform-specific idle
 */

/* Initialize PS/2 keyboard for i686 */
void i686_PS2_Initialize(void);

/* i686-specific keyboard readline with platform idle */
int i686_PS2_ReadLine(char *buf, int bufsize);

/* Non-blocking readline */
int i686_PS2_ReadLineNb(char *buf, int bufsize);

#endif