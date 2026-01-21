// SPDX-License-Identifier: GPL-3.0-only

#include "syscall.h"
#include <std/stdio.h>
#include <stdint.h>
#include <syscall/syscall.h>

/* x86 syscall calling convention (int 0x80):
 * EAX = syscall number
 * EBX, ECX, EDX, ESI, EDI, EBP = args 0-5
 * Return value in EAX
 */

void i686_Syscall_IRQ(Registers *regs)
{
   uint32_t syscall_num = regs->eax;
   uint32_t args[6] = {regs->ebx, regs->ecx, regs->edx,
                       regs->esi, regs->edi, regs->ebp};

   printf("[i686_syscall] num=%u, args=[0x%x, 0x%x, 0x%x, ...]\n", syscall_num,
          args[0], args[1], args[2]);

   // Call generic dispatcher
   intptr_t result = syscall(syscall_num, args);

   // Store result in EAX for return to user
   regs->eax = (uint32_t)result;
}
