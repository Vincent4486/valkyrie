	# SPDX-License-Identifier: AGPL-3.0-or-later

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
.extern start

_start:
	# GRUB passes: EAX = 0x2BADB002 (magic), EBX = pointer to multiboot_info
    cmpl $0x2BADB002, %eax
    jne .no_multiboot

	# EBX -> multiboot_info structure
    movl %ebx, %esi
	# boot_device field is at offset 0x0C in multiboot_info
    movl 0x0c(%esi), %ebx
	# drive number is stored in the highest byte (per multiboot spec)
    shrl $24, %ebx
	# Set up a small temporary stack to avoid relying on GRUB-provided stack
    movl $0x90000, %esp
    movl %esp, %ebp

	# Push args for C start(bootDrive, multiboot_info_ptr)
    pushl %esi      # multiboot_info pointer
    pushl %ebx      # bootDrive (32-bit value; C will take low byte)
    call start
    jmp .

.no_multiboot:
	# No multiboot info present.
	# -if the kernel was started by our stage2 bootloader it was called as:
	#    kernel_entry(bootDrive, partitionPtr)
	# In that case the bootDrive and partitionPtr are on the caller stack
	# ([esp+4] and [esp+8]). We need to grab those, establish a fresh
	# kernel stack, then call the C `start` function with the same args.
    #
	# Stack at entry: [esp] = return_addr, [esp+4] = bootDrive, [esp+8] = partitionPtr
    movl 4(%esp), %eax    # bootDrive (32-bit, low byte used)
    movl 8(%esp), %edx    # partitionPtr

	# Set up a small temporary stack for the kernel (same as multiboot path)
    movl $0x90000, %esp
    movl %esp, %ebp

	# Push arguments for C start(partitionPtr, bootDrive)
    pushl %edx            # partitionPtr
    pushl %eax            # bootDrive
    call start
    jmp .
