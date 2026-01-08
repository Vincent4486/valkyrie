// SPDX-License-Identifier: AGPL-3.0-or-later

#include "hal.h"

void HAL_Initialize()
{
#if defined(I686)
   i686_GDT_Initialize();
   i686_IDT_Initialize();
   i686_ISR_Initialize();
   i686_IRQ_Initialize();
   i686_PS2_Initialize();

   i686_IRQ_RegisterHandler(0, i686_i8253_TimerHandler);
   i686_i8253_Initialize(1000); // Set PIT to 1kHz (reasonable for OS timer)

   i686_ISR_RegisterHandler(0x80, i686_Syscall_IRQ);
#else
#error "Unsupported architecture for HAL initialization"
#endif
}