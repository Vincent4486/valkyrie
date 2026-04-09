// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include "scheduler.h"
#include <hal/mem.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
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

static void cleanup_child_space(Process *child)
{
   if (!child || !child->page_directory) return;

   for (uint32_t va = PAGE_SIZE; va < HEAP_MAX; va += PAGE_SIZE)
   {
      if (g_HalPagingOperations->IsPageMapped(child->page_directory, va) < 0)
      {
         continue;
      }

      uint32_t phys =
          g_HalPagingOperations->GetPhysicalAddress(child->page_directory, va);
      g_HalPagingOperations->UnmapPage(child->page_directory, va);
      if (phys) PMM_FreePhysicalPage(phys);
   }
}

Process *Process_Clone(Process *parent, const Registers *parent_regs)
{
   if (!parent || !parent->page_directory) return NULL;

   Process *child = (Process *)kzalloc(sizeof(Process));
   if (!child) return NULL;

   child->pid = Process_AllocatePid();
   child->ppid = parent->pid;
   child->state = STATE_READY;
   child->kernel_mode = parent->kernel_mode;
   child->uid = parent->uid;
   child->gid = parent->gid;
   child->euid = parent->euid;
   child->egid = parent->egid;
   child->priority = parent->priority;
   child->ticks_remaining = parent->ticks_remaining;
   child->wait_channel = NULL;
   child->signal_mask = parent->signal_mask;
   child->exit_code = 0;

   child->heap_start = parent->heap_start;
   child->heap_end = parent->heap_end;
   child->stack_start = parent->stack_start;
   child->stack_end = parent->stack_end;

   child->page_directory = g_HalPagingOperations->CreatePageDirectory();
   if (!child->page_directory)
   {
      free(child);
      return NULL;
   }

   if (allocate_kernel_stack(child) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(child->page_directory);
      free(child);
      return NULL;
   }

   uint8_t *copy_buffer = (uint8_t *)kmalloc(PAGE_SIZE);
   if (!copy_buffer)
   {
      cleanup_child_space(child);
      g_HalPagingOperations->DestroyPageDirectory(child->page_directory);
      free(child->kernel_stack);
      free(child);
      return NULL;
   }

   void *kernel_pd = Process_GetKernelPageDirectory();
   if (!kernel_pd) kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();

   for (uint32_t va = PAGE_SIZE; va < HEAP_MAX; va += PAGE_SIZE)
   {
      if (g_HalPagingOperations->IsPageMapped(parent->page_directory, va) < 0)
      {
         continue;
      }

      uint32_t new_phys = PMM_AllocatePhysicalPage();
      if (!new_phys)
      {
         free(copy_buffer);
         cleanup_child_space(child);
         g_HalPagingOperations->DestroyPageDirectory(child->page_directory);
         free(child->kernel_stack);
         free(child);
         return NULL;
      }

        if (g_HalPagingOperations->MapPage(child->page_directory, va, new_phys,
                                  HAL_PAGE_PRESENT | HAL_PAGE_RW |
                                     HAL_PAGE_USER) < 0)
      {
         PMM_FreePhysicalPage(new_phys);
         free(copy_buffer);
         cleanup_child_space(child);
         g_HalPagingOperations->DestroyPageDirectory(child->page_directory);
         free(child->kernel_stack);
         free(child);
         return NULL;
      }

      g_HalPagingOperations->SwitchPageDirectory(parent->page_directory);
      memcpy(copy_buffer, (void *)va, PAGE_SIZE);

      g_HalPagingOperations->SwitchPageDirectory(child->page_directory);
      memcpy((void *)va, copy_buffer, PAGE_SIZE);

      g_HalPagingOperations->SwitchPageDirectory(kernel_pd);
   }

   free(copy_buffer);

   child->eip = parent->eip;
   child->esp = parent->esp;
   child->ebp = parent->ebp;
   child->eax = 0;
   child->ebx = parent->ebx;
   child->ecx = parent->ecx;
   child->edx = parent->edx;
   child->esi = parent->esi;
   child->edi = parent->edi;
   child->eflags = parent->eflags;
   child->saved_regs = NULL;

   if (parent_regs)
   {
      child->eip = parent_regs->eip;
      child->esp = parent_regs->esp;
      child->ebp = parent_regs->ebp;
      child->ebx = parent_regs->ebx;
      child->ecx = parent_regs->ecx;
      child->edx = parent_regs->edx;
      child->esi = parent_regs->esi;
      child->edi = parent_regs->edi;
      child->eflags = parent_regs->eflags;
      child->eax = 0;
   }

   for (int i = 0; i < 16; ++i)
   {
      child->fd_table[i] = parent->fd_table[i];
      if (child->fd_table[i]) FD_Retain(child->fd_table[i]);
   }

   logfmt(LOG_INFO, "[PROC] fork: parent=%u child=%u\n", parent->pid,
          child->pid);

   Scheduler_RegisterProcess(child);

   return child;
}
