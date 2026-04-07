// SPDX-License-Identifier: GPL-3.0-only

#include "syscall.h"
#include <cpu/process.h>
#include <hal/scheduler.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <valecium/fs.h>

#ifndef ENOMEM
#define ENOMEM 12
#endif

#ifndef EACCES
#define EACCES 13
#endif

#ifndef ENOENT
#define ENOENT 2
#endif

#ifndef EINVAL
#define EINVAL 22
#endif

static inline Process *get_current_process(void)
{
   return Process_GetCurrent();
}

static inline intptr_t map_exec_error(int exec_result)
{
   if (exec_result == -2) return -ENOENT;
   if (exec_result == -3) return -EACCES;
   return -1;
}

intptr_t sys_brk(void *addr)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   void *result = Heap_ProcessSbrk(proc, 0);  // Get current break
   if (addr == NULL) return (intptr_t)result; // Return current break

   // Calculate increment needed
   intptr_t increment = (intptr_t)addr - (intptr_t)result;
   if (Heap_ProcessSbrk(proc, increment) == (void *)-1) return -1;

   return (intptr_t)addr;
}

void *sys_sbrk(intptr_t increment)
{
   Process *proc = get_current_process();
   if (!proc) return (void *)-1;

   return Heap_ProcessSbrk(proc, increment);
}

// File descriptor syscalls
intptr_t sys_open(const char *path, int flags, uint16_t mode)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   return FD_Open(proc, path, flags, mode);
}

intptr_t sys_close(int fd)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   return FD_Close(proc, fd);
}

intptr_t sys_read(int fd, void *buf, uint32_t count)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   // logfmt(LOG_FATAL, "[SYSCALL] %d\n", fd);
   return FD_Read(proc, fd, buf, count);
}

intptr_t sys_write(int fd, const void *buf, uint32_t count)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   return FD_Write(proc, fd, buf, count);
}

intptr_t sys_lseek(int fd, int32_t offset, int whence)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   return FD_Lseek(proc, fd, offset, whence);
}

intptr_t sys_chmod(const char *path, uint16_t mode)
{
   Process *proc = get_current_process();
   if (!proc || !path) return -1;

   if (proc->euid != 0) return -EACCES;
   return VFS_Chmod(path, mode) ? 0 : -1;
}

intptr_t sys_chown(const char *path, uint32_t uid, uint32_t gid)
{
   Process *proc = get_current_process();
   if (!proc || !path) return -1;

   if (proc->euid != 0) return -EACCES;
   return VFS_Chown(path, uid, gid) ? 0 : -1;
}

intptr_t sys_fork(const Registers *regs)
{
   Process *parent = get_current_process();
   if (!parent) return -1;

   Process *child = Process_Clone(parent, regs);
   if (!child) return -ENOMEM;

   return (intptr_t)child->pid;
}

intptr_t sys_execve(const char *path, const char *const argv[],
                    const char *const envp[], Registers *regs)
{
   Process *proc = get_current_process();
   if (!proc || !path || !regs) return -1;

   int exec_result = Process_Execute(proc, path, argv, envp);
   if (exec_result != 0) return map_exec_error(exec_result);

   regs->eip = proc->eip;
   regs->esp = proc->esp;
   regs->ebp = proc->ebp;
   regs->eax = 0;
   regs->ecx = 0;
   regs->edx = 0;
   regs->esi = 0;
   regs->edi = 0;
   regs->eflags = proc->eflags;

   return 0;
}

intptr_t sys_exit(int status)
{
   Process *proc = get_current_process();
   if (!proc) return -1;

   Process_Exit(proc, status);
   if (g_HalSchedulerOperations && g_HalSchedulerOperations->ContextSwitch)
   {
      g_HalSchedulerOperations->ContextSwitch();
   }
   return 0;
}

intptr_t Syscall_GetPid(void)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return (intptr_t)Process_GetPid(proc);
}

intptr_t Syscall_GetPPid(void)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return (intptr_t)Process_GetPPid(proc);
}

intptr_t Syscall_GetUid(void)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return (intptr_t)Process_GetUid(proc);
}

intptr_t Syscall_GetGid(void)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return (intptr_t)Process_GetGid(proc);
}

intptr_t Syscall_SetUid(uint32_t uid)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return Process_SetUid(proc, uid);
}

intptr_t Syscall_SetGid(uint32_t gid)
{
   Process *proc = get_current_process();
   if (!proc) return -1;
   return Process_SetGid(proc, gid);
}

intptr_t Syscall_Wait4(int32_t pid, int *status, int options, void *rusage)
{
   (void)rusage;

   Process *proc = get_current_process();
   if (!proc) return -1;

   if (options != 0) return -EINVAL;
   return Process_Wait(proc, pid, status, options);
}

/* Generic syscall dispatcher
 *
 * Called by arch-specific handler after extracting parameters from registers.
 * Returns result in EAX (for x86).
 */
intptr_t syscall(uint32_t syscall_num, uint32_t *args)
{
   return syscall_dispatch(syscall_num, args, NULL);
}

intptr_t syscall_dispatch(uint32_t syscall_num, uint32_t *args, Registers *regs)
{
   switch (syscall_num)
   {
   case SYS_EXIT:
      return sys_exit((int)args[0]);

   case SYS_FORK:
      return sys_fork(regs);

   case SYS_EXECVE:
      return sys_execve((const char *)args[0], (const char *const *)args[1],
                        (const char *const *)args[2], regs);

   case SYS_WAIT4:
      return Syscall_Wait4((int32_t)args[0], (int *)args[1], (int)args[2],
                           (void *)args[3]);

   case SYS_GETPID:
      return Syscall_GetPid();

   case SYS_GETPPID:
      return Syscall_GetPPid();

   case SYS_SETUID:
      return Syscall_SetUid(args[0]);

   case SYS_GETUID:
      return Syscall_GetUid();

   case SYS_SETGID:
      return Syscall_SetGid(args[0]);

   case SYS_GETGID:
      return Syscall_GetGid();

   case SYS_BRK:
      return sys_brk((void *)args[0]);

   case SYS_SBRK:
      return (intptr_t)sys_sbrk((intptr_t)args[0]);

   case SYS_OPEN:
      return sys_open((const char *)args[0], args[1], (uint16_t)args[2]);

   case SYS_CLOSE:
      return sys_close(args[0]);

   case SYS_READ:
      return sys_read(args[0], (void *)args[1], args[2]);

   case SYS_WRITE:
      return sys_write(args[0], (const void *)args[1], args[2]);

   case SYS_LSEEK:
      return sys_lseek(args[0], (int32_t)args[1], args[2]);

   case SYS_CHMOD:
      return sys_chmod((const char *)args[0], (uint16_t)args[1]);

   case SYS_CHOWN:
      return sys_chown((const char *)args[0], args[1], args[2]);

   default:
      logfmt(LOG_ERROR, "[SYSCALL] unknown syscall %u\n", syscall_num);
      return -1;
   }
}
