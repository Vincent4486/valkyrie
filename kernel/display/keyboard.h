// SPDX-License-Identifier: GPL-3.0-only

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/**
 * Generic keyboard interface (platform-independent)
 * Handles scancode processing, line buffering, and editing
 */

/* Process a scancode (called by platform-specific drivers) */
void Keyboard_HandleScancode(uint8_t scancode);

/* Platform-independent line reading functions */
int Keyboard_ReadlineNb(char *buf, int bufsize);
int Keyboard_Readline(char *buf, int bufsize);

#endif