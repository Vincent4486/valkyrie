// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_syscall_H
#define I686_syscall_H
#include <arch/i686/cpu/irq.h>
#include <stdint.h>

#define SYS_BRK 45
#define SYS_SBRK 186
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_LSEEK 19

/* x86 syscall dispatcher entry point
 *
 * Called from syscall_entry_asm.asm when user executes int 0x80
 * Extracts parameters from registers and dispatches to generic handler
 */
void i686_Syscall_IRQ(Registers *regs);

#endif