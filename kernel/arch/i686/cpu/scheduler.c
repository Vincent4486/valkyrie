// SPDX-License-Identifier: GPL-3.0-only

#include "scheduler.h"
#include "gdt.h"
#include <cpu/process.h>
#include <cpu/scheduler.h>
#include <hal/io.h>
#include <std/string.h>

#define USER_CODE_SELECTOR (i686_GDT_USER_CODE_SEGMENT | 0x3u)
#define USER_DATA_SELECTOR (i686_GDT_USER_DATA_SEGMENT | 0x3u)

static Registers *build_initial_frame(Process *proc)
{
   if (!proc || !proc->kernel_stack ||
       proc->kernel_stack_size < sizeof(Registers))
   {
      return NULL;
   }

   uint8_t *stack_top = (uint8_t *)proc->kernel_stack + proc->kernel_stack_size;
   Registers *frame = (Registers *)(stack_top - sizeof(Registers));

   memset(frame, 0, sizeof(*frame));

   frame->ds = proc->kernel_mode ? i686_GDT_DATA_SEGMENT : USER_DATA_SELECTOR;
   frame->edi = proc->edi;
   frame->esi = proc->esi;
   frame->ebp = proc->ebp;
   frame->ebx = proc->ebx;
   frame->edx = proc->edx;
   frame->ecx = proc->ecx;
   frame->eax = proc->eax;
   frame->interrupt = 0;
   frame->error = 0;
   frame->eip = proc->eip;
   frame->cs = proc->kernel_mode ? i686_GDT_CODE_SEGMENT : USER_CODE_SELECTOR;
   frame->eflags = proc->eflags ? proc->eflags : 0x202u;
   frame->esp = proc->esp;
   frame->ss = proc->kernel_mode ? i686_GDT_DATA_SEGMENT : USER_DATA_SELECTOR;

   proc->saved_regs = frame;
   return frame;
}

void i686_Scheduler_SaveCpuState() {}

void i686_Scheduler_RestoreCpuState() {}

Registers *i686_Scheduler_ContextSwitch_Impl(void)
{
   for (;;)
   {
      Scheduler_Schedule();

      Process *next = Process_GetCurrent();
      if (next && next->state == STATE_RUNNING)
      {
         if (g_HalIoOperations && g_HalIoOperations->DisableInterrupts)
         {
            g_HalIoOperations->DisableInterrupts();
         }

         if (!next->saved_regs)
         {
            return build_initial_frame(next);
         }

         return next->saved_regs;
      }

      if (!g_HalIoOperations || !g_HalIoOperations->EnableInterrupts ||
          !g_HalIoOperations->Halt)
      {
         return NULL;
      }

      g_HalIoOperations->EnableInterrupts();
      g_HalIoOperations->Halt();
   }
}