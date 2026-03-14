// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_syscall_H
#define I686_syscall_H
#include <arch/i686/cpu/irq.h>
#include <stdint.h>

#ifndef SYS_EXIT
#define SYS_EXIT 1
#endif
#ifndef SYS_FORK
#define SYS_FORK 2
#endif
#ifndef SYS_BRK
#define SYS_BRK 45
#endif
#ifndef SYS_SBRK
#define SYS_SBRK 186
#endif
#ifndef SYS_OPEN
#define SYS_OPEN 5
#endif
#ifndef SYS_CLOSE
#define SYS_CLOSE 6
#endif
#ifndef SYS_EXECVE
#define SYS_EXECVE 11
#endif
#ifndef SYS_READ
#define SYS_READ 3
#endif
#ifndef SYS_WRITE
#define SYS_WRITE 4
#endif
#ifndef SYS_LSEEK
#define SYS_LSEEK 19
#endif

/* x86 syscall dispatcher entry point
 *
 * Called from syscall_entry_asm.asm when user executes int 0x80
 * Extracts parameters from registers and dispatches to generic handler
 */
void i686_Syscall_IRQ(Registers *regs);

#endif