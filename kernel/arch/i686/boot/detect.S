# SPDX-License-Identifier: GPL-3.0-only

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

    # Ask CPUID leaf 0 for the highest supported leaf.
    xorl %eax, %eax
    cpuid
    movl %eax, %esi        # esi = max leaf

    # Prefer leaf 0x16 (CPU base/bus/max frequencies in MHz, Skylake+).
    cmpl $0x16, %esi
    jge .use_leaf_16

    # Fall back to leaf 0x15 (TSC/core-crystal ratio, Broadwell+).
    cmpl $0x15, %esi
    jge .use_leaf_15

    # Neither leaf is available (e.g. QEMU i486/qemu32 default CPU).
    # Return 0 – frequency is unknown; no PIT timing loop that could hang.
    xorl %eax, %eax
    jmp .freq_done

.use_leaf_16:
    movl $0x16, %eax
    cpuid
    # EAX = base frequency in MHz (0 if not enumerated)
    jmp .freq_done

.use_leaf_15:
    movl $0x15, %eax
    cpuid
    # EAX = denominator, EBX = numerator, ECX = crystal Hz
    # freq_MHz = (ECX * EBX / EAX) / 1_000_000
    testl %eax, %eax
    jz .freq_unknown
    testl %ebx, %ebx
    jz .freq_unknown
    testl %ecx, %ecx
    jz .freq_unknown

    # eax = crystal_Hz / denom * numer / 1_000_000
    # To avoid overflow: compute (ECX / 1_000_000) * EBX / EAX first.
    pushl %ecx             # save crystal Hz
    pushl %eax             # save denom
    movl %ecx, %eax        # eax = crystal Hz
    xorl %edx, %edx
    movl $1000000, %ecx
    divl %ecx              # eax = crystal_MHz (edx = remainder, discarded)
    movl %eax, %ecx        # ecx = crystal_MHz
    imull %ebx, %ecx       # ecx = crystal_MHz * numer
    popl %ebx              # ebx = denom (was denom, saved above)
    movl %ecx, %eax
    xorl %edx, %edx
    divl %ebx              # eax = MHz
    addl $4, %esp          # discard saved crystal Hz
    jmp .freq_done

.freq_unknown:
    xorl %eax, %eax

.freq_done:
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
