# SPDX-License-Identifier: AGPL-3.0-or-later

.globl get_arch
get_arch:
    pushl %ebp
    movl %esp, %ebp
    
    # Get the pointer argument (at ebp+8)
    movl 8(%ebp), %eax    # eax = pointer to structure element
    
    # Write 1 to that location (byte write to avoid overwriting adjacent memory)
    movb $1, (%eax)       # *pointer = 1
    
    # Clean up and return void
    movl %ebp, %esp
    popl %ebp
    ret

.globl get_cpu_count
get_cpu_count:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    
    movl 8(%ebp), %esi      # esi = pointer to uint32_t
    
    # First, check if CPUID is supported by trying to flip ID bit (bit 21) in EFLAGS
    pushfl                  # Save EFLAGS
    pushfl                  # Get EFLAGS
    xorl $0x200000, (%esp)  # Flip ID bit
    popfl                   # Set modified EFLAGS
    pushfl                  # Get EFLAGS again
    popl %eax               # EAX = modified EFLAGS
    xorl (%esp), %eax       # Check if ID bit was modified
    popfl                   # Restore original EFLAGS
    andl $0x200000, %eax
    jz .no_cpuid            # CPUID not supported
    
    # Check maximum CPUID leaf
    xorl %eax, %eax
    cpuid
    testl %eax, %eax        # Check if leaf 1 is supported
    jz .no_cpuid
    
    # Get CPU features via CPUID leaf 1
    movl $1, %eax
    cpuid
    
    # Check if multiprocessor bit is set (EDX bit 28 - HTT)
    testl $0x10000000, %edx
    jz .single_core
    
    # Extract logical processor count from EBX[23:16]
    movl %ebx, %eax
    shrl $16, %eax
    andl $0xFF, %eax
    
    # Validate: if 0 or > 255, fall back to 1
    testl %eax, %eax
    jz .single_core
    cmpl $255, %eax
    ja .single_core
    
    jmp .store_count
    
.no_cpuid:
.single_core:
    movl $1, %eax

.store_count:
    # Write to the pointer
    movl %eax, (%esi)
    
    popl %esi
    popl %ebx
    movl %ebp, %esp
    popl %ebp
    ret

/*
 * Function: void get_cpu_brand(char *brand)
 * brand: pointer to 13-byte buffer (12 chars + null terminator)
 * Gets CPU vendor string via CPUID
 */
.globl get_cpu_brand
get_cpu_brand:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    
    movl 8(%ebp), %edi      # edi = pointer to brand buffer
    
    # CPUID leaf 0 - Get vendor string
    xorl %eax, %eax         # eax = 0
    cpuid                   # Get vendor ID
    
    # Store vendor string (12 bytes: EBX, EDX, ECX order)
    movl %ebx, (%edi)       # First 4 bytes
    movl %edx, 4(%edi)      # Next 4 bytes  
    movl %ecx, 8(%edi)      # Last 4 bytes
    
    # Null terminate
    movb $0, 12(%edi)
    
    popl %edi
    popl %esi
    popl %ebx
    movl %ebp, %esp
    popl %ebp
    ret


.globl get_cpu_frequency
get_cpu_frequency:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ecx

    # Try CPUID leaf 0x16 first
    xorl %eax, %eax
    cpuid
    movl %eax, %esi        # esi = max leaf
    cmpl $0x16, %esi
    jge .use_leaf_16
    cmpl $0x15, %esi
    jge .use_leaf_15

    # Fallback: PIT+TSC measurement using mode 0 (one-shot) over one full PIT period (~55ms)
    # Setup PIT channel 0, mode 0 (one-shot), reload = 0xFFFF
    movb $0x30, %al        # command: channel 0, mode 0, binary
    outb %al, $0x43
    movb $0xFF, %al
    outb %al, $0x40        # low byte
    movb $0xFF, %al
    outb %al, $0x40        # high byte

    # Capture TSC start (immediately after starting PIT)
    rdtsc
    movl %eax, %edi        # tsc_start_low

.loop:
    call .read_pit_count    # eax = cur
    testl %eax, %eax        # check if cur == 0
    jnz .loop               # -if not 0, continue

    # Capture TSC end (PIT has reached 0)
    rdtsc
    movl %eax, %esi        # tsc_end_low

    # delta = tsc_end - tsc_start (32-bit; safe for ~55ms)
    movl %esi, %eax
    subl %edi, %eax        # eax = delta

    # cpu_hz = (delta * PIT_FREQ) / 0xFFFF
    movl %eax, %eax
    movl $1193182, %ebx    # PIT_FREQ
    mull %ebx              # edx:eax = delta * PIT_FREQ
    movl $0xFFFF, %ecx     # N = 0xFFFF
    divl %ecx              # eax = cpu_hz (Hz)

    # Convert to MHz: cpu_mhz = cpu_hz / 1_000_000
    xorl %edx, %edx
    movl $1000000, %ecx
    divl %ecx              # eax = cpu_mhz
    jmp .done

.use_leaf_16:
    movl $0x16, %eax
    cpuid
    # EAX = base frequency in MHz
    jmp .cleanup_store

.use_leaf_15:
    movl $0x15, %eax
    cpuid
    # EAX = denom, EBX = numer, ECX = crystal_freq (Hz)
    movl %eax, %edi    # denom
    movl %ebx, %esi    # numer
    movl %ecx, %ebx    # crystal (Hz)
    testl %edi, %edi
    jz .no_leaf15
    testl %esi, %esi
    jz .no_leaf15
    testl %ebx, %ebx
    jz .no_leaf15

    # crystal_MHz = crystal / 1_000_000
    movl %ebx, %eax
    xorl %edx, %edx
    movl $1000000, %ecx
    divl %ecx            # eax = crystal_MHz

    imull %esi, %eax     # eax = crystal_MHz * numer
    xorl %edx, %edx
    divl %edi            # eax = MHz
    jmp .cleanup_store

.no_leaf15:
    xorl %eax, %eax
    jmp .cleanup_store

.read_pit_count:
    # Latch and read PIT channel 0 (16-bit)
    movb $0x00, %al
    outb %al, $0x43
    inb $0x40, %al
    movzbl %al, %eax
    inb $0x40, %al
    movzbl %al, %ecx
    shll $8, %ecx
    orl %ecx, %eax
    ret

.cleanup_store:
    # EAX contains MHz for leaf cases or 0
.done:
    popl %ecx
    popl %edi
    popl %esi
    popl %ebx
    movl %ebp, %esp
    popl %ebp
    ret

.globl get_cache_line_size
get_cache_line_size:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    xorl %eax, %eax
    movl $1, %eax
    cpuid
    # EBX[15:8] contains cache line size in 8-byte units
    movl %ebx, %eax
    shrl $8, %eax
    andl $0xff, %eax
    imull $8, %eax

    popl %edi
    popl %esi
    popl %ebx
    movl %ebp, %esp
    popl %ebp
    ret

.globl get_cpu_features
get_cpu_features:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    xorl %eax, %eax
    movl $1, %eax
    cpuid
    # Return EDX feature flags
    movl %edx, %eax

    popl %edi
    popl %esi
    popl %ebx
    movl %ebp, %esp
    popl %ebp
    ret
