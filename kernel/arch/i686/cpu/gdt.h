// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_GDT_H
#define I686_GDT_H

#include <stdint.h>

#define i686_GDT_CODE_SEGMENT 0x08
#define i686_GDT_DATA_SEGMENT 0x10
#define i686_GDT_USER_CODE_SEGMENT 0x18
#define i686_GDT_USER_DATA_SEGMENT 0x20
#define i686_GDT_TSS_SEGMENT 0x28

void i686_GDT_Initialize();
void i686_GDT_SetTSSEntry(uint32_t base, uint32_t limit);

#endif