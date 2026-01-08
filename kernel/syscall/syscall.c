// SPDX-License-Identifier: AGPL-3.0-or-later

#include "syscall.h"
#include <cpu/process.h>
#include <fs/fd.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <stddef.h>
#include <stdint.h>

intptr_t sys_brk(void *addr)
{
   Process *proc = Process_GetCurrent();
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
   Process *proc = Process_GetCurrent();
   if (!proc) return (void *)-1;

   return Heap_ProcessSbrk(proc, increment);
}

// File descriptor syscalls
intptr_t sys_open(const char *path, int flags)
{
   Process *proc = Process_GetCurrent();
   if (!proc) return -1;

   return FD_Open(proc, path, flags);
}

intptr_t sys_close(int fd)
{
   Process *proc = Process_GetCurrent();
   if (!proc) return -1;

   return FD_Close(proc, fd);
}

intptr_t sys_read(int fd, void *buf, uint32_t count)
{
   Process *proc = Process_GetCurrent();
   if (!proc) return -1;

   return FD_Read(proc, fd, buf, count);
}

intptr_t sys_write(int fd, const void *buf, uint32_t count)
{
   Process *proc = Process_GetCurrent();
   if (!proc) return -1;

   return FD_Write(proc, fd, buf, count);
}

intptr_t sys_lseek(int fd, int32_t offset, int whence)
{
   Process *proc = Process_GetCurrent();
   if (!proc) return -1;

   return FD_Lseek(proc, fd, offset, whence);
}

/* Generic syscall dispatcher
 *
 * Called by arch-specific handler after extracting parameters from registers.
 * Returns result in EAX (for x86).
 */
intptr_t syscall(uint32_t syscall_num, uint32_t *args)
{
   switch (syscall_num)
   {
   case SYS_BRK:
      return sys_brk((void *)args[0]);

   case SYS_SBRK:
      return (intptr_t)sys_sbrk((intptr_t)args[0]);

   case SYS_OPEN:
      return sys_open((const char *)args[0], args[1]);

   case SYS_CLOSE:
      return sys_close(args[0]);

   case SYS_READ:
      return sys_read(args[0], (void *)args[1], args[2]);

   case SYS_WRITE:
      return sys_write(args[0], (const void *)args[1], args[2]);

   case SYS_LSEEK:
      return sys_lseek(args[0], (int32_t)args[1], args[2]);

   default:
      printf("[syscall] unknown syscall %u\n", syscall_num);
      return -1;
   }
}
