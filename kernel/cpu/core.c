// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include "scheduler.h"
#include <hal/paging.h>
#include <hal/scheduler.h>
#include <hal/tss.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>

#ifndef ECHILD
#define ECHILD 10
#endif

#ifndef EINVAL
#define EINVAL 22
#endif

#define USER_EXIT_TRAMPOLINE_VA 0xBFFF1000u

static Process *g_CurrentProcess = NULL;
static uint32_t g_NextPid = 1;
static void *g_KernelPageDirectory = NULL;

static Process *find_process_by_pid(uint32_t pid)
{
   uint32_t count = Scheduler_GetProcessCount();
   for (uint32_t i = 0; i < count; ++i)
   {
      Process *candidate = Scheduler_GetProcessAt(i);
      if (candidate && candidate->pid == pid) return candidate;
   }

   return NULL;
}

static bool process_matches_wait_target(const Process *child, uint32_t parent_pid,
                                        int32_t pid)
{
   if (!child) return false;
   if (child->ppid != parent_pid) return false;
   if (pid > 0 && child->pid != (uint32_t)pid) return false;
   return true;
}

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
   proc->state = STATE_ZOMBIE;

   Process *parent = find_process_by_pid(proc->ppid);
   if (parent && parent->state == STATE_WAITING)
   {
      parent->state = STATE_READY;
   }
}

int Process_Wait(Process *parent, int32_t pid, int *status, int options)
{
   (void)options;

   if (!parent) return -EINVAL;

   for (;;)
   {
      bool has_child = false;
      uint32_t count = Scheduler_GetProcessCount();

      for (uint32_t i = 0; i < count; ++i)
      {
         Process *child = Scheduler_GetProcessAt(i);
         if (!process_matches_wait_target(child, parent->pid, pid)) continue;

         has_child = true;
         if (child->state != STATE_ZOMBIE) continue;

         int child_status = child->exit_code;
         uint32_t child_pid = child->pid;

         Process_Destroy(child);

         if (status) *status = child_status;
         parent->state = STATE_RUNNING;
         return (int)child_pid;
      }

      if (!has_child)
      {
         parent->state = STATE_RUNNING;
         return -ECHILD;
      }

      parent->state = STATE_WAITING;
      if (g_HalSchedulerOperations && g_HalSchedulerOperations->ContextSwitch)
      {
         g_HalSchedulerOperations->ContextSwitch();
      }
   }
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

uint32_t Process_GetPid(const Process *proc)
{
   if (!proc) return 0;
   return proc->pid;
}

uint32_t Process_GetPPid(const Process *proc)
{
   if (!proc) return 0;
   return proc->ppid;
}

uint32_t Process_GetUid(const Process *proc)
{
   if (!proc) return 0;
   return proc->uid;
}

uint32_t Process_GetGid(const Process *proc)
{
   if (!proc) return 0;
   return proc->gid;
}

uint32_t Process_GetEUid(const Process *proc)
{
   if (!proc) return 0;
   return proc->euid;
}

uint32_t Process_GetEGid(const Process *proc)
{
   if (!proc) return 0;
   return proc->egid;
}

int Process_SetUid(Process *proc, uint32_t uid)
{
   if (!proc) return -EINVAL;

   proc->uid = uid;
   proc->euid = uid;
   return 0;
}

int Process_SetGid(Process *proc, uint32_t gid)
{
   if (!proc) return -EINVAL;

   proc->gid = gid;
   proc->egid = gid;
   return 0;
}
