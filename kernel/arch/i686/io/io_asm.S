	# SPDX-License-Identifier: AGPL-3.0-or-later

.global i686_outb
i686_outb:
    movw 4(%esp), %dx
    movb 8(%esp), %al
    outb %al, %dx
    ret

.global i686_outw
i686_outw:
    movw 4(%esp), %dx
    movw 8(%esp), %ax
    outw %ax, %dx
    ret

.global i686_outl
i686_outl:
    movw 4(%esp), %dx
    movl 8(%esp), %eax
    outl %eax, %dx
    ret

.global i686_inb
i686_inb:
    movw 4(%esp), %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

.global i686_inw
i686_inw:
    movw 4(%esp), %dx
    xorl %eax, %eax
    inw %dx, %ax
    ret

.global i686_inl
i686_inl:
    movw 4(%esp), %dx
    xorl %eax, %eax
    inl %dx, %eax
    ret

.global i686_EnableInterrupts
i686_EnableInterrupts:
    sti
    ret

.global i686_DisableInterrupts
i686_DisableInterrupts:
    cli
    ret

.global i686_Panic
i686_Panic:
    cli
    hlt

.global crash_me
crash_me:
	# div by 0
	# movl $0x1337, %ecx
    movl $0, %eax
    divl %eax
    ret

.global i686_Halt
i686_Halt:
    cli
    hlt