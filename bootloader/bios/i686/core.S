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
hang:
	hlt
	jmp hang

stage2_message:
	.asciz "Valecium stage2 core loaded"
