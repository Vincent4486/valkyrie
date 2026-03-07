// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_TSS_H
#define I686_TSS_H

#include <stdint.h>

void i686_TSS_Initialize(void);
void i686_TSS_SetKernelStack(uint32_t esp0);
uint32_t i686_TSS_GetKernelStack(void);

#endif