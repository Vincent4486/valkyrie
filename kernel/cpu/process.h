// SPDX-License-Identifier: GPL-3.0-only

#ifndef PROCESS_H
#define PROCESS_H

#include <mem/mm_kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <valkyrie/fs.h>

#define HEAP_MAX 0xC0000000u // Don't allow heap

typedef struct
{
   uint32_t pid;     // Process ID
   uint32_t ppid;    // Parent process ID
   uint32_t state;   // RUNNING, READY, BLOCKED, TERMINATED
   bool kernel_mode; // true if running in kernel mode

   // Memory management
   void *page_directory; // Points to process's page directory
   uint32_t heap_start;  // Start of heap segment
   uint32_t heap_end;    // Current heap end
   uint32_t stack_start; // Start of user stack
   uint32_t stack_end;   // End of user stack

   // CPU state
   uint32_t eip;                // Instruction pointer
   uint32_t esp;                // Stack pointer
   uint32_t ebp;                // Base pointer
   uint32_t eax, ebx, ecx, edx; // General purpose registers
   uint32_t esi, edi;           // Index registers
   uint32_t eflags;             // Flags register

   // File descriptors
   FileDescriptor
       *fd_table[FD_TABLE_SIZE]; // Open file descriptors (per-process)

   // Scheduling
   uint32_t priority;        // Priority level
   uint32_t ticks_remaining; // Time slice remaining

   // Signals
   uint32_t signal_mask; // Blocked signals

   // Exit status
   int exit_code; // Exit status when terminated
} Process;

/* Process lifecycle */
Process *Process_Create(uint32_t entry_point, bool kernel_mode);
void Process_Destroy(Process *proc);
Process *Process_GetCurrent(void);
void Process_SetCurrent(Process *proc);
void Process_SelfTest(void);

#endif