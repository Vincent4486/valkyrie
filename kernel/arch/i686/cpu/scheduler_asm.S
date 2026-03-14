	# SPDX-License-Identifier: GPL-3.0-only

.extern i686_Scheduler_ContextSwitch_Impl

.global i686_Scheduler_ContextSwitch
i686_Scheduler_ContextSwitch:
	cli
	call i686_Scheduler_ContextSwitch_Impl
	test %eax, %eax
	jz .no_switch

	# EAX -> Registers frame laid out like isr_common expects:
	# ds, pushal regs, interrupt, error, eip, cs, eflags, esp, ss
	movl %eax, %esp

	popl %eax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs

	popal
	addl $8, %esp
	iret

.no_switch:
	sti
	ret