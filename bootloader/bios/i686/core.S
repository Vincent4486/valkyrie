/* SPDX-License-Identifier: BSD-3-Clause */

.code16
.section .text, "ax"
.global _start

_start:
	xorw %ax, %ax
	movw %ax, %ds
	movw $stage2_message, %si

print_loop:
	lodsb
	testb %al, %al
	jz done
	movb $0x0e, %ah
	movb $0x00, %bh
	movb $0x0a, %bl
	int $0x10
	jmp print_loop

done:
	cli
	lgdtl gdt_descriptor

	movl %cr0, %eax
	orl $0x1, %eax
	movl %eax, %cr0

	ljmp $0x08, $protected_mode_entry

.code32
protected_mode_entry:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	movl $0x9fc00, %esp

	cld
	movl $protected_mode_message, %esi
	movl $0xb8000, %edi

print_loop_pm:
	lodsb
	testb %al, %al
	jz hang
	movb %al, (%edi)
	movb $0x0a, 1(%edi)
	addl $2, %edi
	jmp print_loop_pm

hang:
	hlt
	jmp hang

.align 8
gdt_start:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
gdt_end:

gdt_descriptor:
	.word gdt_end - gdt_start - 1
	.long gdt_start

stage2_message:
	.asciz "Valecium stage2 bootloader loaded"

protected_mode_message:
	.asciz "Entered protected mode"
