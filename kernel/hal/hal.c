// SPDX-License-Identifier: AGPL-3.0-or-later

#include "hal.h"
#include "io.h"
#include "irq.h"
#include "paging.h"
#include "scheduler.h"
#include "stack.h"
#include "syscall.h"

const HAL_IoOperations *g_HalIoOperations = &(HAL_IoOperations){
    .outb = HAL_ARCH_outb,
    .outw = HAL_ARCH_outw,
    .outl = HAL_ARCH_outl,
    .inb = HAL_ARCH_inb,
    .inw = HAL_ARCH_inw,
    .inl = HAL_ARCH_inl,
    .EnableInterrupts = HAL_ARCH_EnableInterrupts,
    .DisableInterrupts = HAL_ARCH_DisableInterrupts,
    .iowait = HAL_ARCH_iowait,
    .Halt = HAL_ARCH_Halt,
    .Panic = HAL_ARCH_Panic,
};

const HAL_IrqOperations *g_HalIrqOperations = &(HAL_IrqOperations){
    .RegisterHandler = HAL_ARCH_IRQ_RegisterHandler,
    .Unmask = HAL_ARCH_IRQ_Unmask,
};

const HAL_PagingOperations *g_HalPagingOperations = &(HAL_PagingOperations){
    .Initialize = HAL_ARCH_Paging_Initialize,
    .Enable = HAL_ARCH_Paging_Enable,
    .CreatePageDirectory = HAL_ARCH_Paging_CreatePageDirectory,
    .DestroyPageDirectory = HAL_ARCH_Paging_DestroyPageDirectory,
    .MapPage = HAL_ARCH_Paging_MapPage,
    .UnmapPage = HAL_ARCH_Paging_UnmapPage,
    .GetPhysicalAddress = HAL_ARCH_Paging_GetPhysicalAddress,
    .IsPageMapped = HAL_ARCH_Paging_IsPageMapped,
    .PageFaultHandler = HAL_ARCH_Paging_PageFaultHandler,
    .InvalidateTlbEntry = HAL_ARCH_Paging_InvalidateTlbEntry,
    .FlushTlb = HAL_ARCH_Paging_FlushTlb,
    .SwitchPageDirectory = HAL_ARCH_Paging_SwitchPageDirectory,
    .GetCurrentPageDirectory = HAL_ARCH_Paging_GetCurrentPageDirectory,
    .AllocateKernelPages = HAL_ARCH_Paging_AllocateKernelPages,
    .FreeKernelPages = HAL_ARCH_Paging_FreeKernelPages,
    .SelfTest = HAL_ARCH_Paging_SelfTest,
};

const HAL_StackOperations *g_HalStackOperations = &(HAL_StackOperations){
    .GetEBP = HAL_ARCH_Stack_GetEBP,
    .GetESP = HAL_ARCH_Stack_GetESP,
    .GetRegisters = HAL_ARCH_Stack_GetRegisters,
    .InitializeKernel = HAL_ARCH_Stack_InitializeKernel,
    .SetRegisters = HAL_ARCH_Stack_SetRegisters,
    .SetupException = HAL_ARCH_Stack_SetupException,
    .SetupProcess = HAL_ARCH_Stack_SetupProcess,
};

const HAL_SyscallOperations *g_HalSyscallOperations = &(HAL_SyscallOperations){
    .Handler = HAL_ARCH_Syscall_Handler,
};

void HAL_Initialize()
{
#if defined(I686)
   i686_GDT_Initialize();
   i686_IDT_Initialize();
   i686_ISR_Initialize();
   i686_IRQ_Initialize();
   i686_PS2_Initialize();

   i686_IRQ_RegisterHandler(0, i686_i8253_TimerHandler);
   i686_i8253_Initialize(1000); // Set PIT to 1kHz (reasonable for OS timer)

   i686_ISR_RegisterHandler(0x80, i686_Syscall_IRQ);
#else
#error "Unsupported architecture for HAL initialization"
#endif
}