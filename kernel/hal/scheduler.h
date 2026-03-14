// SPDX-License-Identifier: GPL-3.0-only

#ifndef HAL_SCHEDULER_H
#define HAL_SCHEDULER_H

#if defined(I686)
#include <arch/i686/cpu/scheduler.h>
#define HAL_ARCH_Scheduler_ContextSwitch i686_Scheduler_ContextSwitch
#else
#error "Unsupported architecture for HAL scheduler"
#endif

typedef struct HAL_SchedulerOperations
{
   void (*ContextSwitch)(void);
} HAL_SchedulerOperations;

extern const HAL_SchedulerOperations *g_HalSchedulerOperations;

#endif