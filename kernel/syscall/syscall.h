// SPDX-License-Identifier: GPL-3.0-only

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdbool.h>
#include <stdint.h>

#define SYS_BRK 45
#define SYS_SBRK 186
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_LSEEK 19

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

/* Generic syscall dispatcher (arch code calls this)
 * syscall_num: syscall number
 * args: array of up to 6 arguments
 */
intptr_t syscall(uint32_t syscall_num, uint32_t *args);

#endif