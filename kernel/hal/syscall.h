// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#if defined(I686)
#include <arch/i686/syscall/syscall.h>
#define HAL_ARCH_Syscall_Handler i686_Syscall_IRQ
#else
#error "Unsupported architecture for HAL syscall"
#endif

typedef struct HAL_SyscallOperations
{
   void (*Handler)(Registers *regs);
} HAL_SyscallOperations;

extern const HAL_SyscallOperations *g_HalSyscallOperations;

#endif