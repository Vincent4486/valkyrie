// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cpu/process.h>
#include <hal/paging.h>
#include <hal/stack.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <std/string.h>
#include <sys/sys.h>

/**
 * Generic stack management implementation
 * Platform-specific setup in arch/i686/mem/stack.c
 */

static Stack *kernel_stack = NULL;

/**
 * Initialize stack subsystem for the OS
 */
void Stack_Initialize(void) { Stack_InitializeKernel(); }

/**
 * Initialize kernel stack (delegates to architecture code)
 */
void Stack_InitializeKernel(void)
{
   // Initialize architecture-specific kernel stack
   HAL_Stack_InitializeKernel();

   // Get kernel stack size from system info (default 8KB if not set)
   uint32_t stack_size = (g_SysInfo && g_SysInfo->memory.kernel_stack_size)
                             ? g_SysInfo->memory.kernel_stack_size
                             : 8192;

   // Create kernel stack
   kernel_stack = Stack_Create(stack_size);
   if (!kernel_stack)
   {
      printf("[stack] ERROR: failed to create kernel stack\n");
   }
}

/**
 * Create a new user stack
 */
Stack *Stack_Create(size_t size)
{
   if (size == 0) return NULL;

   // Allocate Stack structure
   Stack *stack = (Stack *)kmalloc(sizeof(Stack));
   if (!stack) return NULL;

   // Allocate stack memory from kernel heap
   uint8_t *data = (uint8_t *)kmalloc(size);
   if (!data)
   {
      free(stack);
      return NULL;
   }

   // Initialize stack structure
   // Stack grows downward: base = highest address, current starts at base
   stack->base = (uint32_t)data + size; // Top of allocated region
   stack->size = size;
   stack->current = stack->base;
   stack->data = data;

   return stack;
}

/**
 * Initialize a process's user stack
 */
int Stack_ProcessInitialize(Process *proc, uint32_t stack_top_va, size_t size)
{
   if (!proc || size == 0) return -1;

   // Align size to page boundary
   if (size % PAGE_SIZE != 0)
   {
      size = ((size / PAGE_SIZE) + 1) * PAGE_SIZE;
   }

   uint32_t stack_bottom_va = stack_top_va - size;
   uint32_t pages_needed = size / PAGE_SIZE;

   // Allocate and map pages
   for (uint32_t i = 0; i < pages_needed; ++i)
   {
      uint32_t va = stack_bottom_va + (i * PAGE_SIZE);
      uint32_t phys = PMM_AllocatePhysicalPage();

      if (phys == 0)
      {
         printf("[stack] ERROR: PMM_AllocatePhysicalPage failed\n");
         // Cleanup already mapped pages
         for (uint32_t j = 0; j < i; ++j)
         {
            uint32_t va_cleanup = stack_bottom_va + (j * PAGE_SIZE);
            uint32_t phys_cleanup =
                HAL_Paging_GetPhysicalAddress(proc->page_directory, va_cleanup);
            HAL_Paging_UnmapPage(proc->page_directory, va_cleanup);
            if (phys_cleanup) PMM_FreePhysicalPage(phys_cleanup);
         }
         return -1;
      }

      // Map as User | RW | Present
      if (!HAL_Paging_MapPage(proc->page_directory, va, phys,
                              HAL_PAGE_PRESENT | HAL_PAGE_RW | HAL_PAGE_USER))
      {
         printf("[stack] ERROR: map_page failed for stack at 0x%08x\n", va);
         PMM_FreePhysicalPage(phys);
         // Cleanup
         for (uint32_t j = 0; j < i; ++j)
         {
            uint32_t va_cleanup = stack_bottom_va + (j * PAGE_SIZE);
            uint32_t phys_cleanup =
                HAL_Paging_GetPhysicalAddress(proc->page_directory, va_cleanup);
            HAL_Paging_UnmapPage(proc->page_directory, va_cleanup);
            if (phys_cleanup) PMM_FreePhysicalPage(phys_cleanup);
         }
         return -1;
      }
   }

   // Update Process struct
   proc->stack_start = stack_bottom_va;
   proc->stack_end = stack_top_va;

   // printf("[stack] Initialized user stack for pid=%u at 0x%08x-0x%08x\n",
   //        proc->pid, proc->stack_start, proc->stack_end);

   return 0;
}

/**
 * Destroy a user stack
 */
void Stack_Destroy(Stack *stack)
{
   if (!stack) return;

   // Free allocated data
   if (stack->data)
   {
      free(stack->data);
   }

   // Free Stack structure
   free(stack);
}

/**
 * Push data onto a stack (grows downward on x86)
 */
uint32_t Stack_Push(Stack *stack, const void *data, size_t size)
{
   if (!stack || !data || size == 0) return 0;

   // Check for stack overflow
   if (!Stack_HasSpace(stack, size))
   {
      return 0; // Stack overflow
   }

   // Move stack pointer down (x86 stack grows downward)
   stack->current -= size;

   // Copy data to stack
   memcpy((void *)stack->current, data, size);

   return stack->current;
}

/**
 * Pop data from a stack
 */
uint32_t Stack_Pop(Stack *stack, void *data, size_t size)
{
   if (!stack || !data || size == 0) return 0;

   // Check for stack underflow
   if (stack->current + size > stack->base)
   {
      return 0; // Stack underflow
   }

   // Copy data from stack
   memcpy(data, (void *)stack->current, size);

   // Move stack pointer up
   stack->current += size;

   return stack->current;
}

/**
 * Set stack pointer with bounds checking
 */
int Stack_SetSP(Stack *stack, uint32_t sp)
{
   if (!stack) return 0;

   uint32_t stack_bottom = (uint32_t)stack->data;
   uint32_t stack_top = stack->base;

   // Verify SP is within stack bounds
   if (sp >= stack_bottom && sp <= stack_top)
   {
      stack->current = sp;
      return 1;
   }

   return 0;
}

/**
 * Check if stack has enough free space
 */
int Stack_HasSpace(Stack *stack, size_t required)
{
   if (!stack || required == 0) return 0;

   uint32_t stack_bottom = (uint32_t)stack->data;

   // Calculate free space from current SP to bottom
   if (stack->current <= stack_bottom)
   {
      return 0; // Stack corrupted or at minimum
   }

   size_t free_space = stack->current - stack_bottom;
   return free_space >= required;
}

/**
 * Get kernel stack
 */
Stack *Stack_GetKernel(void) { return kernel_stack; }

/**
 * Platform wrappers -> architecture-specific implementations
 */

void Stack_SetupProcess(Stack *stack, uint32_t entry_point)
{
   HAL_Stack_SetupProcess(stack, entry_point);
}

uint32_t Stack_GetESP(void) { return HAL_Stack_GetESP(); }

uint32_t Stack_GetEBP(void) { return HAL_Stack_GetEBP(); }

void Stack_SetRegisters(uint32_t esp, uint32_t ebp)
{
   HAL_Stack_SetRegisters(esp, ebp);
}

/**
 * Stack self-test (returns 1 on success, 0 on failure)
 */
int Stack_SelfTest(void)
{
   // Create/destroy
   Stack *s = Stack_Create(4096);
   if (!s) return 0;

   uint32_t sp0 = s->current;

   // Push/pop
   uint32_t val = 0xAABBCCDD;
   if (!Stack_Push(s, &val, sizeof(val)))
   {
      Stack_Destroy(s);
      return 0;
   }
   uint32_t popped = 0;
   if (!Stack_Pop(s, &popped, sizeof(popped)))
   {
      Stack_Destroy(s);
      return 0;
   }
   if (popped != val || s->current != sp0)
   {
      Stack_Destroy(s);
      return 0;
   }

   // Bounds check
   if (!Stack_HasSpace(s, 1024))
   {
      Stack_Destroy(s);
      return 0;
   }
   if (Stack_SetSP(s, 0xFFFFFFFF))
   {
      Stack_Destroy(s);
      return 0;
   }

   Stack_Destroy(s);
   return 1;
}
