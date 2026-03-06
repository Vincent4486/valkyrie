	# SPDX-License-Identifier: GPL-3.0-only

.code32

.section .multiboot, "a"
	# Multiboot v1 header (magic, flags, checksum)
	# magic = 0x1BADB002
	# flags = 0x00000003  ; request memory info and boot_device (no addr fields)
	# checksum = -(magic + flags)
    .long 0x1BADB002
    .long 0x00000003
    .long (-(0x1BADB002 + 0x00000003))

.section .entry
	# Entry point symbol - linker ENTRY points here
.global _start
.extern Parser_Multiboot

_start:
	# GRUB/Multiboot passes: EAX = 0x2BADB002 (magic), EBX = *multiboot_info_t
    cmpl $0x2BADB002, %eax
    jne .no_multiboot

	# Preserve the two Multiboot registers before anything clobbers them.
    movl %eax, %edi         # edi = magic   (1st argument to Parser_Multiboot)
    movl %ebx, %esi         # esi = mbi ptr (2nd argument to Parser_Multiboot)

	# Set up a small temporary stack (below the 640 KB low-memory mark).
    movl $0x90000, %esp
    movl %esp, %ebp

	# Push arguments in cdecl right-to-left order:
	#   2nd arg: multiboot_info_t *mbi
    pushl %esi
	#   1st arg: uint32_t magic
    pushl %edi
    call Parser_Multiboot
    # Parser_Multiboot never returns (it calls start(), which loops forever).
    jmp .

.no_multiboot:
	# No valid Multiboot handoff.  The kernel requires a Multiboot-compliant
	# bootloader (e.g. GRUB2).  Hang the processor cleanly.
.hang:
    cli
    hlt
    jmp .hang
