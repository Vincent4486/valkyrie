// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include "scheduler.h"
#include <hal/paging.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <std/string.h>
#include <sys/sys.h>

#define USER_STACK_TOP 0xBFFF0000u
#define USER_STACK_SIZE (64u * 1024u)
#define USER_HEAP_START 0x10000000u
#define USER_EXIT_TRAMPOLINE_VA 0xBFFF1000u
#define USER_CODE_SELECTOR 0x1Bu
#define USER_DATA_SELECTOR 0x23u

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

static int map_user_trampoline(Process *proc)
{
   if (!proc || !proc->page_directory) return -1;

   uint32_t phys = PMM_AllocatePhysicalPage();
   if (!phys) return -1;

   if (!g_HalPagingOperations->MapPage(
           proc->page_directory, USER_EXIT_TRAMPOLINE_VA, phys,
           HAL_PAGE_PRESENT | HAL_PAGE_RW | HAL_PAGE_USER))
   {
      PMM_FreePhysicalPage(phys);
      return -1;
   }

   void *kernel_pd = Process_GetKernelPageDirectory();
   if (!kernel_pd) kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();

   g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);
   memset((void *)USER_EXIT_TRAMPOLINE_VA, 0, PAGE_SIZE);
   ((uint8_t *)USER_EXIT_TRAMPOLINE_VA)[0] = 0xEB;
   ((uint8_t *)USER_EXIT_TRAMPOLINE_VA)[1] = 0xFE;
   g_HalPagingOperations->SwitchPageDirectory(kernel_pd);

   return 0;
}

Process *Process_CreateUser(uint32_t entry_point)
{
   Process *proc = (Process *)kzalloc(sizeof(Process));
   if (!proc)
   {
      logfmt(LOG_ERROR, "[PROC] create user: kmalloc failed\n");
      return NULL;
   }

   proc->pid = Process_AllocatePid();
   proc->ppid = 0;
   proc->state = STATE_READY;
   proc->kernel_mode = false;
   proc->uid = 0;
   proc->gid = 0;
   proc->euid = 0;
   proc->egid = 0;
   proc->priority = 10;
   proc->ticks_remaining = 0;
   proc->signal_mask = 0;
   proc->exit_code = 0;

   proc->page_directory = g_HalPagingOperations->CreatePageDirectory();
   if (!proc->page_directory)
   {
      free(proc);
      return NULL;
   }

   if (allocate_kernel_stack(proc) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(proc->page_directory);
      free(proc);
      return NULL;
   }

   if (Heap_ProcessInitialize(proc, USER_HEAP_START) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(proc->page_directory);
      free(proc->kernel_stack);
      free(proc);
      return NULL;
   }

   if (Stack_ProcessInitialize(proc, USER_STACK_TOP, USER_STACK_SIZE) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(proc->page_directory);
      free(proc->kernel_stack);
      free(proc);
      return NULL;
   }

   if (map_user_trampoline(proc) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(proc->page_directory);
      free(proc->kernel_stack);
      free(proc);
      return NULL;
   }

   void *kernel_pd = Process_GetKernelPageDirectory();
   if (!kernel_pd) kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();

   uint32_t user_esp = USER_STACK_TOP;
   g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);
   user_esp -= sizeof(uint32_t);
   *(uint32_t *)user_esp = USER_EXIT_TRAMPOLINE_VA;
   g_HalPagingOperations->SwitchPageDirectory(kernel_pd);

   proc->eip = entry_point;
   proc->esp = user_esp;
   proc->ebp = user_esp;
   proc->eax = proc->ebx = proc->ecx = proc->edx = 0;
   proc->esi = proc->edi = 0;
   proc->eflags = 0x202u;
   proc->saved_regs = NULL;

   if (proc->kernel_stack && proc->kernel_stack_size >= sizeof(Registers))
   {
      uint8_t *kstack_top =
          (uint8_t *)proc->kernel_stack + proc->kernel_stack_size;
      Registers *frame = (Registers *)(kstack_top - sizeof(Registers));

      memset(frame, 0, sizeof(*frame));
      frame->eip = entry_point;
      frame->cs = USER_CODE_SELECTOR;
      frame->eflags = 0x202u; // IF=1 so timer/IRQ delivery works in user mode.
      frame->esp = user_esp;
      frame->ss = USER_DATA_SELECTOR;
      frame->ds = USER_DATA_SELECTOR;

      proc->saved_regs = frame;
   }

   for (int i = 0; i < 16; ++i) proc->fd_table[i] = NULL;

   logfmt(LOG_INFO, "[PROC] created user pid=%u entry=0x%08x\n", proc->pid,
          entry_point);

   Scheduler_RegisterProcess(proc);

   return proc;
}
