// SPDX-License-Identifier: GPL-3.0-only

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <cpu/process.h>

void Scheduler_Initialize();

void Scheduler_RegisterProcess(Process *process);
void Scheduler_UnregisterProcess(Process *process);

void Scheduler_Schedule();

void Scheduler_SetProcessState();
void Scheduler_GetNextRunnableProcess();

#endif