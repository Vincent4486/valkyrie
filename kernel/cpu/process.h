// SPDX-License-Identifier: GPL-3.0-only

#ifndef PROCESS_H
#define PROCESS_H

#include <hal/irq.h> /* Registers – the interrupt stack frame type */
#include <hal/mem.h>
#include <mem/mm_kernel.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <valecium/fs.h>

#define HEAP_MAX HAL_ARCH_BASE // Don't allow heap to extend into kernel space

typedef enum
{
   STATE_READY = 0u,
   STATE_RUNNING = 1u,
   STATE_BLOCKED = 2u,
   STATE_ZOMBIE = 3u,
   STATE_WAITING = 4u,
} ProcessState;

typedef struct
{
   uint32_t pid;     // Process ID
   uint32_t ppid;    // Parent process ID
   uint32_t state;   // ProcessState
   bool kernel_mode; // true if running in kernel mode

   // Identity and credentials
   uint32_t uid;
   uint32_t gid;
   uint32_t euid;
   uint32_t egid;

   // Memory management
   void *page_directory; // Points to process's page directory
   uint32_t heap_start;  // Start of heap segment
   uint32_t heap_end;    // Current heap end
   uint32_t stack_start; // Start of user stack
   uint32_t stack_end;   // End of user stack

   /* CPU state – flat fields used at process creation and fork.
    * During a live context switch the scheduler instead consults
    * saved_regs, which points directly into the interrupt stack frame
    * constructed by the ISR stub (isr_asm.S) and passed to
    * i686_ISR_Handler(Registers *regs).  Storing the pointer here lets
    * the scheduler save/restore every register atomically without
    * copying the frame into the PCB. */
   uint32_t eip;                // Instruction pointer
   uint32_t esp;                // Stack pointer
   uint32_t ebp;                // Base pointer
   uint32_t eax, ebx, ecx, edx; // General purpose registers
   uint32_t esi, edi;           // Index registers
   uint32_t eflags;             // Flags register

   /* Canonical saved register frame for context switching.
    * Set by the context-switch path; NULL when the process has never
    * been preempted (e.g. a freshly-created, not-yet-run process). */
   Registers *saved_regs;

   // File descriptors
   FileDescriptor *fd_table[16]; // Open file descriptors (per-process)

   // Scheduling
   uint32_t priority;        // Priority level
   uint32_t ticks_remaining; // Time slice remaining
   void *wait_channel;       // Sleep channel for blocking operations

   // Signals
   uint32_t signal_mask; // Blocked signals

   // Exit status
   int exit_code; // Exit status when terminated

   // Per-process kernel stack (used for ring transitions, TSS esp0)
   void *kernel_stack;
   uint32_t kernel_stack_size;
} Process;

/* Process lifecycle */
Process *Process_Create(uint32_t entry_point, bool kernel_mode);
Process *Process_CreateUser(uint32_t entry_point);
Process *Process_CreateKernel(uint32_t entry_point);
Process *Process_Clone(Process *parent, const Registers *parent_regs);
int Process_Execute(Process *proc, const char *path, const char *const argv[],
                    const char *const envp[]);
void Process_Exit(Process *proc, int exit_code);
void Process_Destroy(Process *proc);
int Process_Wait(Process *parent, int32_t pid, int *status, int options);
Process *Process_GetCurrent(void);
void Process_SetCurrent(Process *proc);
int Process_InitializeStandardIO(Process *proc);
void Process_SelfTest(void);
void Process_BlockOn(Process *proc, void *wait_channel);
void Process_Unblock(Process *proc);
void Process_WakeByChannel(void *wait_channel);

/* Identity helpers */
uint32_t Process_GetPid(const Process *proc);
uint32_t Process_GetPPid(const Process *proc);
uint32_t Process_GetUid(const Process *proc);
uint32_t Process_GetGid(const Process *proc);
uint32_t Process_GetEUid(const Process *proc);
uint32_t Process_GetEGid(const Process *proc);
int Process_SetUid(Process *proc, uint32_t uid);
int Process_SetGid(Process *proc, uint32_t gid);

/* Core process state */
uint32_t Process_AllocatePid(void);
void Process_SetKernelPageDirectory(void *page_directory);
void *Process_GetKernelPageDirectory(void);

#endif