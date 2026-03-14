// SPDX-License-Identifier: GPL-3.0-only

#ifndef SYSCALL_H
#define SYSCALL_H

#include <hal/irq.h>
#include <stdbool.h>
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

/* Syscall handler prototypes
 * These are called by arch-specific dispatcher after extracting parameters
 */
intptr_t sys_brk(void *addr);
void *sys_sbrk(intptr_t increment);
intptr_t sys_open(const char *path, int flags);
intptr_t sys_close(int fd);
intptr_t sys_read(int fd, void *buf, uint32_t count);
intptr_t sys_write(int fd, const void *buf, uint32_t count);
intptr_t sys_lseek(int fd, int32_t offset, int whence);
intptr_t sys_fork(const Registers *regs);
intptr_t sys_execve(const char *path, const char *const argv[],
                    const char *const envp[], Registers *regs);
intptr_t sys_exit(int status);

/* Generic syscall dispatcher (arch code calls this)
 * syscall_num: syscall number
 * args: array of up to 6 arguments
 */
intptr_t syscall(uint32_t syscall_num, uint32_t *args);
intptr_t syscall_dispatch(uint32_t syscall_num, uint32_t *args,
                          Registers *regs);

#endif