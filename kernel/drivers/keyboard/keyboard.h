// SPDX-License-Identifier: GPL-3.0-only

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/**
 * Generic keyboard interface (platform-independent)
 * Handles scancode processing and forwards input to TTY
 */

/* Initialize keyboard input state */
void Keyboard_Initialize(void);

/* Process a scancode (called by platform-specific drivers) */
void Keyboard_HandleScancode(uint8_t scancode);

#endif
