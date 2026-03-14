// SPDX-License-Identifier: GPL-3.0-only

#include "scheduler.h"
#include <std/stdio.h>

#define SCHED_MAX_PROCESSES 128

#define PROCESS_STATE_READY 0u
#define PROCESS_STATE_RUNNING 1u
#define PROCESS_STATE_BLOCKED 2u
#define PROCESS_STATE_TERMINATED 3u

static Process *g_SchedulerProcesses[SCHED_MAX_PROCESSES];
static uint32_t g_SchedulerProcessCount = 0;
static uint32_t g_SchedulerLastIndex = 0;
static Process *g_SchedulerNextRunnable = NULL;

void Scheduler_Initialize()
{
   for (uint32_t i = 0; i < SCHED_MAX_PROCESSES; ++i)
   {
      g_SchedulerProcesses[i] = NULL;
   }

   g_SchedulerProcessCount = 0;
   g_SchedulerLastIndex = 0;
   g_SchedulerNextRunnable = NULL;
}

void Scheduler_RegisterProcess(Process *process)
{
   if (!process) return;

   for (uint32_t i = 0; i < g_SchedulerProcessCount; ++i)
   {
      if (g_SchedulerProcesses[i] == process)
      {
         return;
      }
   }

   if (g_SchedulerProcessCount >= SCHED_MAX_PROCESSES)
   {
      logfmt(LOG_WARNING, "[SCHED] process list full, pid=%u not queued\n",
             process->pid);
      return;
   }

   g_SchedulerProcesses[g_SchedulerProcessCount++] = process;
}

void Scheduler_UnregisterProcess(Process *process)
{
   if (!process || g_SchedulerProcessCount == 0) return;

   for (uint32_t i = 0; i < g_SchedulerProcessCount; ++i)
   {
      if (g_SchedulerProcesses[i] != process) continue;

      for (uint32_t j = i; j + 1 < g_SchedulerProcessCount; ++j)
      {
         g_SchedulerProcesses[j] = g_SchedulerProcesses[j + 1];
      }

      g_SchedulerProcesses[g_SchedulerProcessCount - 1] = NULL;
      --g_SchedulerProcessCount;

      if (g_SchedulerProcessCount == 0)
      {
         g_SchedulerLastIndex = 0;
      }
      else if (g_SchedulerLastIndex >= g_SchedulerProcessCount)
      {
         g_SchedulerLastIndex = 0;
      }

      return;
   }
}

void Scheduler_GetNextRunnableProcess()
{
   g_SchedulerNextRunnable = NULL;
   if (g_SchedulerProcessCount == 0) return;

   for (uint32_t n = 0; n < g_SchedulerProcessCount; ++n)
   {
      uint32_t idx = (g_SchedulerLastIndex + n) % g_SchedulerProcessCount;
      Process *candidate = g_SchedulerProcesses[idx];

      if (!candidate) continue;
      if (candidate->state == PROCESS_STATE_BLOCKED) continue;
      if (candidate->state == PROCESS_STATE_TERMINATED) continue;

      g_SchedulerNextRunnable = candidate;
      g_SchedulerLastIndex = (idx + 1) % g_SchedulerProcessCount;
      return;
   }
}

void Scheduler_SetProcessState()
{
   Process *current = Process_GetCurrent();
   if (!current) return;

   if (current->state == PROCESS_STATE_RUNNING)
   {
      current->state = PROCESS_STATE_READY;
   }
}

void Scheduler_Schedule()
{
   Scheduler_SetProcessState();
   Scheduler_GetNextRunnableProcess();

   Process *next = g_SchedulerNextRunnable;
   if (!next) return;

   next->state = PROCESS_STATE_RUNNING;
   Process_SetCurrent(next);
}