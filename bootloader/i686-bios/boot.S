/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * i686 PC MBR stage1 loader.
 *
 * Expected stage2 header at the first sector of stage2:
 *   +0x00 u32 magic        = 0x32534c56 ('VLS2')
 *   +0x04 u16 sector_count = total stage2 sectors (including header sector)
 *
 * Stage2 is expected to start at LBA 1 (disk sector 2), directly after the
 * 512-byte MBR sector. The loader reads the header there, validates it, then
 * loads sector_count sectors to 0000:8000 and jumps there.
 */

.code16
.section .mbr, "ax"
.global _start

.equ STAGE2_MAGIC, 0x32534c56
.equ SCAN_BUFFER,  0x0600
.equ STAGE2_LOAD,  0x8000

_start:
	cli
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw $0x7c00, %sp
	sti
	cld

	movb %dl, boot_drive

	/* Ensure BIOS INT 13h extensions are available for LBA reads. */
	movb boot_drive, %dl
	movw $0x55aa, %bx
	movb $0x41, %ah
	int $0x13
	jc boot_fail
	cmpw $0xaa55, %bx
	jne boot_fail
	testw $0x0001, %cx
	jz boot_fail

	/* First partition start LBA is at MBR + 0x1be + 8. */
	movl 0x7c00 + 0x1be + 8, %eax
	movl %eax, scan_limit_lba
	cmpl $2, %eax
	jb boot_fail

	/* Stage2 starts immediately after the boot sector (LBA 1). */
	movl $1, temp_lba
	movw $SCAN_BUFFER, %bx
	movw $1, %cx
	call bios_ext_read
	jc boot_fail

	cmpl $STAGE2_MAGIC, SCAN_BUFFER
	jne boot_fail

	movw SCAN_BUFFER + 4, %ax
	cmpw $1, %ax
	jb boot_fail
	movw %ax, stage2_sectors_left

	movzwl %ax, %edx
	movl $1, %ecx
	addl %edx, %ecx
	cmpl scan_limit_lba, %ecx
	ja boot_fail

	movw $STAGE2_LOAD, stage2_offset
load_stage2_loop:
	cmpw $0, stage2_sectors_left
	je stage2_jump

	movw stage2_offset, %bx
	movw $1, %cx
	call bios_ext_read
	jc boot_fail

	addw $512, stage2_offset
	incl temp_lba
	decw stage2_sectors_left
	jmp load_stage2_loop

stage2_jump:
	ljmp $0x0000, $STAGE2_LOAD

boot_fail:
	movw $boot_fail_msg, %si
	call print_string
	cli
hang:
	hlt
	jmp hang

/* Print a zero-terminated string at DS:SI using BIOS teletype output. */
print_string:
	lodsb
	testb %al, %al
	jz print_done
	movb $0x0e, %ah
	movb $0x00, %bh
	movb $0x07, %bl
	int $0x10
	jmp print_string
print_done:
	ret

/*
 * Read sectors using INT 13h extensions (AH=42h).
 * Inputs:
 *   temp_lba : u32 start LBA
 *   BX       : destination offset (segment 0000)
 *   CX       : sector count
 * Clobbers: AX, SI
 */
bios_ext_read:
	movw %cx, dap_num_sectors
	movw %bx, dap_buffer_offset
	movw $0, dap_buffer_segment

	movl temp_lba, %eax
	movl %eax, dap_lba_low
	movl $0, dap_lba_high

	movw $dap, %si
	movb $0x42, %ah
	movb boot_drive, %dl
	int $0x13
	ret

boot_drive:
	.byte 0

scan_limit_lba:
	.long 0

temp_lba:
	.long 0

stage2_offset:
	.word 0

stage2_sectors_left:
	.word 0

boot_fail_msg:
	.asciz "Failed to boot..."

/* BIOS disk address packet (DAP), 16 bytes. */
dap:
	.byte 0x10
	.byte 0x00
dap_num_sectors:
	.word 0
dap_buffer_offset:
	.word 0
dap_buffer_segment:
	.word 0
dap_lba_low:
	.long 0
dap_lba_high:
	.long 0

/* Keep stage1 in the boot-code area of MBR (446 bytes max). */
.if (. - _start) > 446
.error "i686-pc MBR stage1 exceeds 446 bytes"
.endif
.fill 446 - (. - _start), 1, 0
