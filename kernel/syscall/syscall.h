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
#ifndef SYS_GETPID
#define SYS_GETPID 20
#endif
#ifndef SYS_GETPPID
#define SYS_GETPPID 64
#endif
#ifndef SYS_SETUID
#define SYS_SETUID 23
#endif
#ifndef SYS_GETUID
#define SYS_GETUID 24
#endif
#ifndef SYS_SETGID
#define SYS_SETGID 46
#endif
#ifndef SYS_GETGID
#define SYS_GETGID 47
#endif
#ifndef SYS_WAIT4
#define SYS_WAIT4 114
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
#ifndef SYS_CHMOD
#define SYS_CHMOD 15
#endif
#ifndef SYS_CHOWN
#define SYS_CHOWN 182
#endif

/* Syscall handler prototypes
 * These are called by arch-specific dispatcher after extracting parameters
 */
intptr_t sys_brk(void *addr);
void *sys_sbrk(intptr_t increment);
intptr_t sys_open(const char *path, int flags, uint16_t mode);
intptr_t sys_close(int fd);
intptr_t sys_read(int fd, void *buf, uint32_t count);
intptr_t sys_write(int fd, const void *buf, uint32_t count);
intptr_t sys_lseek(int fd, int32_t offset, int whence);
intptr_t sys_chmod(const char *path, uint16_t mode);
intptr_t sys_chown(const char *path, uint32_t uid, uint32_t gid);
intptr_t sys_fork(const Registers *regs);
intptr_t sys_execve(const char *path, const char *const argv[],
                    const char *const envp[], Registers *regs);
intptr_t sys_exit(int status);
intptr_t Syscall_GetPid(void);
intptr_t Syscall_GetPPid(void);
intptr_t Syscall_GetUid(void);
intptr_t Syscall_GetGid(void);
intptr_t Syscall_SetUid(uint32_t uid);
intptr_t Syscall_SetGid(uint32_t gid);
intptr_t Syscall_Wait4(int32_t pid, int *status, int options, void *rusage);

/* Generic syscall dispatcher (arch code calls this)
 * syscall_num: syscall number
 * args: array of up to 6 arguments
 */
intptr_t syscall(uint32_t syscall_num, uint32_t *args);
intptr_t syscall_dispatch(uint32_t syscall_num, uint32_t *args,
                          Registers *regs);

#endif