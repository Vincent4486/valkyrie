// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef I686_STACK_H
#define I686_STACK_H

#include <mem/mm_kernel.h>
#include <stdint.h>

/**
 * x86 32-bit Stack Management
 *
 * Platform-specific stack setup for i686 architecture:
 * - ESP (stack pointer) register
 * - EBP (base pointer) register
 * - Process entry point setup
 * - Stack frame creation
 */

/**
 * X86 stack frame layout
 *
 * When a process starts, the stack is set up as:
 *
 *   [ESP] -> return address (exit syscall)
 *   [EBP] -> previous frame pointer (0 for first frame)
 *
 * The stack grows downward (from high to low addresses).
 */

/**
 * x86-specific process stack setup
 *
 * Prepares the user stack for process execution:
 * - Sets up return address pointing to process exit handler
 * - Initializes EBP to point to frame base
 * - Prepares stack for entry into process main
 *
 * @param stack User stack structure
 * @param entry_point Virtual address of process entry point (usually _start)
 *
 * Stack layout after setup:
 *   ESP -> return_address (points to exit handler)
 */
void i686_Stack_SetupProcess(Stack *stack, uint32_t entry_point);

/**
 * Get current ESP register value
 *
 * @return Current stack pointer
 */
uint32_t i686_Stack_GetESP(void);

/**
 * Get current EBP register value
 *
 * @return Current base pointer
 */
uint32_t i686_Stack_GetEBP(void);

/**
 * Set ESP and EBP for context switching
 *
 * Used during context switch to restore process stack state
 *
 * @param esp New stack pointer
 * @param ebp New base pointer
 */
void i686_Stack_SetRegisters(uint32_t esp, uint32_t ebp);

/**
 * Get a snapshot of current stack registers
 *
 * @param esp_out Pointer to store ESP
 * @param ebp_out Pointer to store EBP
 */
void i686_Stack_GetRegisters(uint32_t *esp_out, uint32_t *ebp_out);

/**
 * Prepare kernel stack for a CPU exception context
 *
 * Sets up stack frame for exception handling
 *
 * @param stack Kernel stack
 * @param handler Exception handler function pointer
 * @param error_code Error code to pass to handler
 */
void i686_Stack_SetupException(Stack *stack, uint32_t handler,
                               uint32_t error_code);

void i686_Stack_InitializeKernel(void);

#endif