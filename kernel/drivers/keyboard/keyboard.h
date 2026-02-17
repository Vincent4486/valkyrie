// SPDX-License-Identifier: GPL-3.0-only

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <fs/devfs/devfs.h>
#include <stdint.h>

/**
 * Generic keyboard interface (platform-independent)
 * Handles scancode processing, line buffering, and editing
 */

/* Initialize keyboard driver and register in devfs */
void Keyboard_Initialize(void);

/* Process a scancode (called by platform-specific drivers) */
void Keyboard_HandleScancode(uint8_t scancode);

/* Platform-independent line reading functions */
int Keyboard_ReadlineNb(char *buf, int bufsize);
int Keyboard_Readline(char *buf, int bufsize);

/* Devfs read/write operations */
uint32_t Keyboard_DevfsRead(DEVFS_DeviceNode *node, uint32_t offset,
                            uint32_t size, void *buffer);
uint32_t Keyboard_DevfsWrite(DEVFS_DeviceNode *node, uint32_t offset,
                             uint32_t size, const void *buffer);

#endif
