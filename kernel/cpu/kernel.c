// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include "scheduler.h"
#include <hal/paging.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <sys/sys.h>

static int allocate_kernel_stack(Process *proc)
{
   uint32_t stack_size = (g_SysInfo && g_SysInfo->memory.kernel_stack_size)
                             ? g_SysInfo->memory.kernel_stack_size
                             : 65536u;

   proc->kernel_stack = kmalloc(stack_size);
   if (!proc->kernel_stack) return -1;

   proc->kernel_stack_size = stack_size;
   return 0;
}

Process *Process_CreateKernel(uint32_t entry_point)
{
   Process *proc = (Process *)kzalloc(sizeof(Process));
   if (!proc)
   {
      logfmt(LOG_ERROR, "[PROC] create kernel: kmalloc failed\n");
      return NULL;
   }

   proc->pid = Process_AllocatePid();
   proc->ppid = 0;
   proc->state = STATE_READY;
   proc->kernel_mode = true;
   proc->uid = 0;
   proc->gid = 0;
   proc->euid = 0;
   proc->egid = 0;
   proc->priority = 10;
   proc->ticks_remaining = 0;
   proc->signal_mask = 0;
   proc->exit_code = 0;

   proc->page_directory = Process_GetKernelPageDirectory();
   if (!proc->page_directory)
   {
      proc->page_directory = g_HalPagingOperations->GetCurrentPageDirectory();
      Process_SetKernelPageDirectory(proc->page_directory);
   }

   if (allocate_kernel_stack(proc) != 0)
   {
      free(proc);
      return NULL;
   }

   proc->heap_start = proc->heap_end = 0;
   proc->stack_start = proc->stack_end = 0;

   proc->eip = entry_point;
   proc->esp = (uint32_t)proc->kernel_stack + proc->kernel_stack_size;
   proc->ebp = proc->esp;
   proc->eax = proc->ebx = proc->ecx = proc->edx = 0;
   proc->esi = proc->edi = 0;
   proc->eflags = 0x202u;
   proc->saved_regs = NULL;

   for (int i = 0; i < 16; ++i) proc->fd_table[i] = NULL;

   if (Process_InitializeStandardIO(proc) != 0)
   {
      free(proc->kernel_stack);
      free(proc);
      return NULL;
   }

   logfmt(LOG_INFO, "[PROC] created kernel pid=%u entry=0x%08x\n", proc->pid,
          entry_point);

   Scheduler_RegisterProcess(proc);

   return proc;
}
