/* SPDX-License-Identifier: GPL-3.0-only */

/*
 * i686 PC MBR / El Torito stage1 loader.
 *
 * Stage2 load metadata is patched into stage1 at install time:
 *   stage2_start_lba:   u32 disk LBA where stage2 begins
 *   stage2_sector_count u16 number of sectors to load (for disk boot)
 */

.code16
.section .mbr, "ax"
.global _start

/* 
 * Stage2 is located exactly 1 sector (512 bytes) after Stage1.
 * 0x7C00 + 512 = 0x7E00
 */
.equ STAGE2_LOAD,  0x7E00

_start:
    jmp _start_real
    nop
    
    /* 
     * xorriso with -boot-info-table will overwrite bytes 8 through 63 
     * of the first sector with CD-ROM metadata. We use .org to safely 
     * push the start of our actual code past byte 64 to avoid corruption.
     */
    .org _start + 64

_start_real:
    cli
    /* Standardize segments to 0000:7C00 to prevent offset mismatch */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti
    cld

    /* Save the boot drive ID passed by the BIOS in DL */
    movb %dl, boot_drive

    /*
     * EL TORITO / NO-EMULATION CHECK
     * Check for the "VLSC" signature at the expected start of Stage2.
     * In No-Emul mode, the BIOS loads (Stage1 + Stage2) as a contiguous block.
     * Since we forced DS=0000, 0x7E02 points accurately to physical 0x7E02.
     */
    movw    $0x7E02, %bx
    cmpl    $0x43534C56, (%bx)  /* Compares 4 bytes ("VLSC") at once */
    je      stage2_jump         /* If found, skip disk read entirely */

disk_boot_path:
    /* MBR Disk Boot Path */
    movl    stage2_start_lba, %eax
    testl   %eax, %eax
    jz      boot_fail_code_5    /* Fail if LBA is 0 (Unpatched / Invalid) */
    movl    %eax, temp_lba

    /* Check for BIOS INT 13h Extensions support */
    movb    boot_drive, %dl
    movw    $0x55aa, %bx
    movb    $0x41, %ah
    int     $0x13
    jc      boot_fail_code_2    /* Err 2: Extensions not supported */
    cmpw    $0xaa55, %bx
    jne     boot_fail_code_3    /* Err 3: Extension magic mismatch */

    /* Setup load parameters */
    movw    stage2_sector_count, %ax
    testw   %ax, %ax
    jz      boot_fail_code_5    /* Err 5: Sector count is 0 */
    movw    %ax, stage2_sectors_left
    movw    $STAGE2_LOAD, stage2_offset

load_stage2_loop:
    cmpw    $0, stage2_sectors_left
    je      stage2_jump

    movw    stage2_offset, %bx
    movw    $1, %cx             /* Read safely, 1 sector at a time */
    call    bios_ext_read
    jc      boot_fail_code_6    /* Err 6: INT 13h read failed */

    addw    $512, stage2_offset
    incl    temp_lba
    decw    stage2_sectors_left
    jmp     load_stage2_loop

stage2_jump:
    /* Restore boot drive to DL so Stage2 knows what device to use */
    movb    boot_drive, %dl
    ljmp    $0x0000, $STAGE2_LOAD

/* --- Error Handling --- */

boot_fail_code_2: movb $'2', %al; jmp boot_fail
boot_fail_code_3: movb $'3', %al; jmp boot_fail
boot_fail_code_5: movb $'5', %al; jmp boot_fail
boot_fail_code_6: movb $'6', %al; jmp boot_fail

boot_fail:
    movb    %al, %cl
    movw    $boot_fail_msg, %si
    call    print_string
    movb    $':', %al
    call    print_char
    movb    %cl, %al
    call    print_char
    cli
1:  hlt
    jmp     1b

/* --- Utilities --- */

print_char:
    movb $0x0e, %ah
    xorw %bx, %bx
    movb $0x07, %bl
    int $0x10
    ret

print_string:
    lodsb
    testb %al, %al
    jz 1f
    call print_char
    jmp print_string
1:  ret

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

/* --- Data & Variables --- */

boot_drive:         .byte 0
stage2_patch_signature: .ascii "VLSP"
stage2_start_lba:   .long 0
stage2_sector_count: .word 0
temp_lba:           .long 0
stage2_offset:      .word 0
stage2_sectors_left: .word 0
boot_fail_msg:      .asciz "Err"

/* 
 * Disk Address Packet (DAP)
 * Forced 4-byte alignment to prevent picky BIOSes from rejecting the packet
 */
.balign 4
dap:
    .byte 0x10          /* Packet size (16 bytes) */
    .byte 0x00          /* Reserved */
dap_num_sectors:    .word 0
dap_buffer_offset:  .word 0
dap_buffer_segment: .word 0
dap_lba_low:        .long 0
dap_lba_high:       .long 0

/* 
 * Fill the rest of the sector up to byte 446.
 * The build tool / linker will append the partition table and the 0x55AA signature.
 */
.org _start + 446