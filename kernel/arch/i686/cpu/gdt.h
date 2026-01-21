// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_GDT_H
#define I686_GDT_H

#define i686_GDT_CODE_SEGMENT 0x08
#define i686_GDT_DATA_SEGMENT 0x10

void i686_GDT_Initialize();

#endif