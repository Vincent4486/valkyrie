// SPDX-License-Identifier: GPL-3.0-only

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#if defined(I686)
#include <arch/i686/syscall/syscall.h>
#define HAL_ARCH_syscall_handler i686_Syscall_IRQ
#else
#error "Unsupported architecture for HAL syscall"
#endif

static inline void HAL_syscall_handler(Registers *regs)
{
   HAL_ARCH_syscall_handler(regs);
}

#endif