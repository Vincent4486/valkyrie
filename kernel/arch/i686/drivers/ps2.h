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

#endif
