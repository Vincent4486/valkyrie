// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_PIC_H
#define I686_PIC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
   const char *Name;
   bool (*Probe)();
   void (*Initialize)(uint8_t offsetPic1, uint8_t offsetPic2, bool autoEoi);
   void (*Disable)();
   void (*SendEndOfInterrupt)(int irq);
   void (*Mask)(int irq);
   void (*Unmask)(int irq);
} PICDriver;

#endif