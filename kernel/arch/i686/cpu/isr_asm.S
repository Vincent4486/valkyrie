	# SPDX-License-Identifier: AGPL-3.0-or-later

.code32

.extern i686_ISR_Handler

	# cpu pushes to the stack: ss, esp, eflags, cs, eip

.macro ISR_NOERRORCODE num
.global i686_ISR\num
i686_ISR\num:
    pushl $0              # push dummy error code
    pushl $\num             # push interrupt number
    jmp isr_common
.endm

.macro ISR_ERRORCODE num
.global i686_ISR\num
i686_ISR\num:
	# cpu pushes an error code to the stack
    pushl $\num             # push interrupt number
    jmp isr_common
.endm

.include "kernel/arch/i686/cpu/isrs_gen.inc"


isr_common:
    pushal

    xorl %eax, %eax
    movw %ds, %ax
    pushl %eax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp
    call i686_ISR_Handler

    addl $4, %esp

    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    popal
    addl $8, %esp
    iret
