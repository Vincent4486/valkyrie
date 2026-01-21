// SPDX-License-Identifier: GPL-3.0-only

#ifndef I686_SCHEDULER_H
#define I686_SCHEDULER_H

void i686_Scheduler_SaveCpuState();
void i686_Scheduler_RestoreCpuState();

void __attribute__((cdecl)) i686_Scheduler_ContextSwitch();

#endif