/* SPDX-License-Identifier: GPL-3.0-only */

.code16
.section .entry, "ax"
.global _start

_start:
	/*
	 * El Torito detection signature — must be the first 6 bytes
	 * at 0x7E00.  Stage1 checks for "VLSC" at offset +2 to decide
	 * whether to load from disk or jump directly.
	 */
	jmp _start_code
	.ascii "VLSC"
_start_code:
	/* Save the BIOS boot-drive number (passed in DL) immediately. */
	movb	%dl, boot_drive

	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %es

	/* Detect hardware and build the Multiboot2 Boot Information
	 * structure while BIOS services are still available. */
	movb	boot_drive, %dl
	call	detect_hardware
	movb	%al, available_outputs  /* Save output bitmask for C entry */

	movw	$stage2_message, %si

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

	/* Enable A20 through System Control Port A (0x92). */
	inb $0x92, %al
	orb $0x02, %al
	andb $0xfe, %al
	outb %al, $0x92

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

	movzbl	boot_drive, %eax        /* Pass boot drive to C */
	pushl	%eax
	movzbl	available_outputs, %eax /* Pass output bitmask to C */
	pushl	%eax
	pushl	corefs_start_address    /* Pass corefs load address to C */
	pushl	$MBI_BASE
	call	main
	addl	$16, %esp               /* 4 args × 4 bytes */

hang:
	hlt
	jmp hang

.section .text, "ax"

.align 8
gdt_start:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
gdt_end:

gdt_descriptor:
	.word gdt_end - gdt_start - 1
	.long gdt_start

/* ================================================================== */
/*  Data section                                                       */
/* ================================================================== */

	.balign 4
boot_drive:
	.byte 0

available_outputs:
	.byte 0

	/* Patched with the corefs load address (real-mode physical). */
	.balign 4
corefs_patch_signature:
	.ascii "VLSF"
corefs_start_address:
	.long 0

	/* Multiboot2 Boot Information structure base (matches detect.S). */
	.set MBI_BASE,     0x6000
	.set MBI_PTR_SAVE, 0x5000

	/* ---- String constants ---- */
	stage2_message:
		.asciz "Valecium stage2 bootloader loaded"
