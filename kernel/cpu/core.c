// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include "scheduler.h"
#include <hal/paging.h>
#include <hal/tss.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>

#define USER_EXIT_TRAMPOLINE_VA 0xBFFF1000u

static Process *g_CurrentProcess = NULL;
static uint32_t g_NextPid = 1;
static void *g_KernelPageDirectory = NULL;

static void free_kernel_stack(Process *proc)
{
   if (!proc || !proc->kernel_stack) return;

   free(proc->kernel_stack);
   proc->kernel_stack = NULL;
   proc->kernel_stack_size = 0;
}

static void cleanup_user_address_space(Process *proc)
{
   if (!proc || !proc->page_directory) return;

   if (proc->stack_start && proc->stack_end)
   {
      uint32_t pages = (proc->stack_end - proc->stack_start) / PAGE_SIZE;
      for (uint32_t i = 0; i < pages; ++i)
      {
         uint32_t va = proc->stack_start + (i * PAGE_SIZE);
         uint32_t phys = g_HalPagingOperations->GetPhysicalAddress(
             proc->page_directory, va);
         g_HalPagingOperations->UnmapPage(proc->page_directory, va);
         if (phys) PMM_FreePhysicalPage(phys);
      }
   }

   uint32_t tramp_phys = g_HalPagingOperations->GetPhysicalAddress(
       proc->page_directory, USER_EXIT_TRAMPOLINE_VA);
   if (tramp_phys)
   {
      g_HalPagingOperations->UnmapPage(proc->page_directory,
                                       USER_EXIT_TRAMPOLINE_VA);
      PMM_FreePhysicalPage(tramp_phys);
   }

   if (proc->heap_start && proc->heap_end)
   {
      uint32_t heap_pages =
          (proc->heap_end - proc->heap_start + PAGE_SIZE - 1) / PAGE_SIZE;
      for (uint32_t i = 0; i < heap_pages; ++i)
      {
         uint32_t va = proc->heap_start + (i * PAGE_SIZE);
         uint32_t phys = g_HalPagingOperations->GetPhysicalAddress(
             proc->page_directory, va);
         g_HalPagingOperations->UnmapPage(proc->page_directory, va);
         if (phys) PMM_FreePhysicalPage(phys);
      }
   }
}

uint32_t Process_AllocatePid(void) { return g_NextPid++; }

void Process_SetKernelPageDirectory(void *page_directory)
{
   g_KernelPageDirectory = page_directory;
}

void *Process_GetKernelPageDirectory(void) { return g_KernelPageDirectory; }

Process *Process_Create(uint32_t entry_point, bool kernel_mode)
{
   return kernel_mode ? Process_CreateKernel(entry_point)
                      : Process_CreateUser(entry_point);
}

void Process_Destroy(Process *proc)
{
   if (!proc) return;

   Scheduler_UnregisterProcess(proc);

   if (!proc->kernel_mode) cleanup_user_address_space(proc);

   if (proc->page_directory && !proc->kernel_mode)
   {
      g_HalPagingOperations->DestroyPageDirectory(proc->page_directory);
   }

   FD_CloseAll(proc);
   free_kernel_stack(proc);

   if (g_CurrentProcess == proc)
   {
      g_CurrentProcess = NULL;
      if (!g_KernelPageDirectory)
      {
         g_KernelPageDirectory =
             g_HalPagingOperations->GetCurrentPageDirectory();
      }
      if (g_KernelPageDirectory)
      {
         g_HalPagingOperations->SwitchPageDirectory(g_KernelPageDirectory);
      }
   }

   free(proc);
}

void Process_Exit(Process *proc, int exit_code)
{
   if (!proc) return;

   proc->exit_code = exit_code;
   proc->state = 3; // TERMINATED
   Process_Destroy(proc);
}

Process *Process_GetCurrent(void) { return g_CurrentProcess; }

void Process_SetCurrent(Process *proc)
{
   g_CurrentProcess = proc;

   if (!g_KernelPageDirectory)
   {
      g_KernelPageDirectory = VMM_GetPageDirectory();
   }

   if (proc)
   {
      if (proc->page_directory)
      {
         g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);
      }

      if (proc->kernel_stack && proc->kernel_stack_size)
      {
         uint32_t esp0 = (uint32_t)proc->kernel_stack + proc->kernel_stack_size;
         g_HalTssOperations->SetKernelStack(esp0);
      }
   }
   else
   {
      if (g_KernelPageDirectory)
      {
         g_HalPagingOperations->SwitchPageDirectory(g_KernelPageDirectory);
      }

      Stack *kernel_stack = Stack_GetKernel();
      if (kernel_stack)
      {
         g_HalTssOperations->SetKernelStack(kernel_stack->base);
      }
   }
}

void Process_SelfTest(void)
{
   logfmt(LOG_INFO, "[PROC] self-test: starting\n");

   Process *p = Process_CreateUser(0x08048000u);
   if (!p)
   {
      logfmt(LOG_ERROR,
             "[PROC] self-test: FAIL (Process_CreateUser returned NULL)\n");
      return;
   }

   if (Heap_ProcessSbrk(p, 4096) == (void *)-1)
   {
      logfmt(LOG_ERROR, "[PROC] self-test: FAIL (sbrk failed)\n");
      Process_Destroy(p);
      return;
   }

   Process_SetCurrent(p);
   volatile uint32_t *heap_test = (volatile uint32_t *)p->heap_start;
   *heap_test = 0xCAFEBABEu;
   if (*heap_test != 0xCAFEBABEu)
   {
      logfmt(LOG_ERROR, "[PROC] self-test: FAIL (heap write/read)\n");
      Process_Destroy(p);
      return;
   }

   volatile uint32_t *stack_test =
       (volatile uint32_t *)(p->stack_end - sizeof(uint32_t));
   *stack_test = 0x11223344u;
   if (*stack_test != 0x11223344u)
   {
      logfmt(LOG_ERROR, "[PROC] self-test: FAIL (stack write/read)\n");
      Process_Destroy(p);
      return;
   }

   logfmt(LOG_INFO, "[PROC] self-test: PASS (pid=%u, heap+stack ok)\n", p->pid);
   Process_Destroy(p);
}
