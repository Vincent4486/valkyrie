// SPDX-License-Identifier: AGPL-3.0-or-later

#include "stack.h"
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <std/string.h>

/**
 * x86 32-bit Stack Implementation
 *
 * Handles ESP/EBP register setup and x86-specific stack operations
 */

// Process exit handler (will be called when process returns from main)
extern void _process_exit_handler(void);

/**
 * x86-specific process stack setup
 *
 * Prepares user stack for process entry:
 * 1. Clears stack to known state
 * 2. Pushes process exit handler address (return address)
 * 3. Sets up initial EBP
 * 4. Adjusts stack for process entry
 */
void i686_Stack_SetupProcess(Stack *stack, uint32_t entry_point)
{
   if (!stack) return;

   // Reset stack to base (top of allocated region)
   stack->current = stack->base;

   /**
    * X86 stack layout after setup:
    *
    * High Address
    *   [ESP] -> return_address (process_exit_handler)
    * Low Address
    *
    * When the process starts at entry_point, the RET instruction
    * will pop this return address and jump to process_exit_handler
    * when main() returns.
    */

   // Push process exit handler address as return address
   // This is what will be executed when main() returns
   uint32_t exit_handler = (uint32_t)&_process_exit_handler;
   Stack_Push(stack, &exit_handler, sizeof(uint32_t));

   // The stack pointer is now positioned correctly for process entry
   // When entering the process at entry_point:
   // - ESP points to the return address
   // - The process can freely use stack below ESP
   // - When main() returns with RET, it will jump to _process_exit_handler
}

/**
 * Get current ESP register
 *
 * Read the current stack pointer from the ESP register
 */
uint32_t i686_Stack_GetESP(void)
{
   uint32_t esp;
   __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
   return esp;
}

/**
 * Get current EBP register
 *
 * Read the current base pointer from the EBP register
 */
uint32_t i686_Stack_GetEBP(void)
{
   uint32_t ebp;
   __asm__ __volatile__("mov %%ebp, %0" : "=r"(ebp));
   return ebp;
}

/**
 * Set ESP and EBP registers
 *
 * Used during context switch to restore process stack state
 *
 * WARNING: This completely changes the current stack!
 * Only use during context switch with proper stack management.
 */
void i686_Stack_SetRegisters(uint32_t esp, uint32_t ebp)
{
   // Set EBP first (provides frame reference for debuggers)
   __asm__ __volatile__("mov %0, %%ebp" : : "r"(ebp));

   // Set ESP (changes active stack)
   __asm__ __volatile__("mov %0, %%esp" : : "r"(esp));
}

/**
 * Get snapshot of current stack registers
 */
void i686_Stack_GetRegisters(uint32_t *esp_out, uint32_t *ebp_out)
{
   if (esp_out)
   {
      __asm__ __volatile__("mov %%esp, %0" : "=r"(*esp_out));
   }
   if (ebp_out)
   {
      __asm__ __volatile__("mov %%ebp, %0" : "=r"(*ebp_out));
   }
}

/**
 * Setup kernel stack for exception context
 *
 * Prepares stack frame for exception handling with error code
 */
void i686_Stack_SetupException(Stack *stack, uint32_t handler,
                               uint32_t error_code)
{
   if (!stack) return;

   // Reset to top of kernel stack
   stack->current = stack->base;

   /**
    * Exception stack frame (x86 standard):
    *
    * High Address
    *   [ESP] -> return address (after exception handler)
    *   [ESP+4] -> error code
    *   [ESP+8] -> handler context
    * Low Address
    */

   // Push error code
   Stack_Push(stack, &error_code, sizeof(uint32_t));

   // Push handler address
   Stack_Push(stack, &handler, sizeof(uint32_t));

   // Stack is ready for exception context
}

/**
 * Initialize x86 kernel stack
 *
 * Sets up the kernel stack in kernel memory space.
 * The kernel stack grows downward from a fixed high address.
 *
 * Kernel stack layout (typical):
 * - Physical location: First few pages of kernel memory
 * - Virtual location: Kernel address space (1GB+)
 * - Size: 4KB - 8KB (configurable)
 */
void i686_Stack_InitializeKernel(void)
{
   // Kernel stack initialization happens during boot
   // ESP is set up in boot code (entry.S) to point to kernel stack
   // This function can be used for additional per-CPU kernel stack setup
   // in multi-processor scenarios

   // For now, kernel stack is initialized in entry.S:
   // - Physical address: early in kernel memory
   // - Virtual address: kernel space (maps to physical via boot page tables)
   // - Size: predefined in linker script
}

/* Default process exit handler.
 * When a user process returns from main(), execution will jump here. For now,
 * just log and halt. Replace with a proper process teardown when available. */
__attribute__((noreturn)) void _process_exit_handler(void)
{
   printf("[process] exit handler invoked; halting.\n");
   for (;;)
   {
      __asm__ volatile("cli; hlt");
   }
}
