// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROCMM_H
#define PROCMM_H

#include <cpu/process.h>
#include <mem/mm_kernel.h>
#include <stddef.h>
#include <stdint.h>

/* Per-process heap management */
int Heap_ProcessInitialize(Process *proc, uint32_t heap_start_va);
int Heap_ProcessBrk(Process *proc, void *addr);
void *Heap_ProcessSbrk(Process *proc, intptr_t inc);

/* Virtual Memory Manager (VMM) - Process level
 *
 * Per-process virtual memory operations.
 * Takes explicit page directory for context-specific mapping.
 */

/* Allocate and map virtual memory in a page directory.
 * If next_vaddr_state is NULL, uses the kernel allocator bump pointer.
 */
void *VMM_AllocateInDir(void *page_dir, uint32_t *next_vaddr_state,
                        uint32_t size, uint32_t flags);

/* Free previously allocated virtual memory in a page directory
 */
void VMM_FreeInDir(void *page_dir, void *vaddr, uint32_t size);

/* Map existing physical memory in a page directory
 */
bool VMM_MapInDir(void *page_dir, uint32_t vaddr, uint32_t paddr, uint32_t size,
                  uint32_t flags);

/* Unmap virtual memory in a page directory (does not free physical pages)
 */
bool VMM_UnmapInDir(void *page_dir, uint32_t vaddr, uint32_t size);

/* Get physical address of a virtual address in a page directory
 */
uint32_t VMM_GetPhysInDir(void *page_dir, uint32_t vaddr);

/* Stack Management - Process level */

/**
 * Create a new user stack for a process
 * @param size Stack size in bytes (typically 64KB for user processes)
 * @return Pointer to new Stack structure, or NULL on failure
 * Allocates memory for the stack and initializes the Stack structure.
 * The stack grows downward (standard x86 behavior).
 */
Stack *Stack_Create(size_t size);

/**
 * Initialize a process's user stack
 * @param proc Process to initialize stack for
 * @param stack_top_va Virtual address of the top of the stack (e.g. 0xBFFFF000)
 * @param size Size of the stack in bytes
 * @return 0 on success, -1 on failure
 */
int Stack_ProcessInitialize(Process *proc, uint32_t stack_top_va, size_t size);

/**
 * Destroy a user stack
 * @param stack Stack to destroy
 * Frees the allocated stack memory and the Stack structure.
 */
void Stack_Destroy(Stack *stack);

/**
 * Push data onto a stack
 * @param stack Stack to push to
 * @param data Data to push
 * @param size Size of data in bytes
 * @return New stack pointer, or 0 on failure (stack overflow)
 */
uint32_t Stack_Push(Stack *stack, const void *data, size_t size);

/**
 * Pop data from a stack
 * @param stack Stack to pop from
 * @param data Buffer to receive popped data
 * @param size Size of data to pop in bytes
 * @return New stack pointer, or 0 on failure (stack underflow)
 */
uint32_t Stack_Pop(Stack *stack, void *data, size_t size);

/**
 * Get current stack pointer for a stack
 * @param stack Stack structure
 * @return Current stack pointer value
 */
static inline uint32_t Stack_GetSP(Stack *stack)
{
   return stack ? stack->current : 0;
}

/**
 * Set current stack pointer
 * @param stack Stack structure
 * @param sp New stack pointer value
 * @return 1 on success, 0 if SP is out of valid range
 */
int Stack_SetSP(Stack *stack, uint32_t sp);

/**
 * Check if stack has enough free space
 * @param stack Stack structure
 * @param required Required free space in bytes
 * @return 1 if enough space, 0 otherwise
 */
int Stack_HasSpace(Stack *stack, size_t required);

/**
 * Platform-specific: Setup stack for process execution
 * Called after ELF loading to prepare stack for:
 * - argc/argv
 * - Environment variables
 * - Return address
 */
void Stack_SetupProcess(Stack *stack, uint32_t entry_point);

#endif