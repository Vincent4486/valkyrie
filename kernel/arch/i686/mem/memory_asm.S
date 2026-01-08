	.global memcpy_asm
.global memcmp_asm
.global memset_asm

.extern mem_fault_handler

.section .text

	# void *memcpy_asm(void *dst, const void *src, size_t n)
	# cdecl: dst @ [ebp+8], src @ [ebp+12], n @ [ebp+16]
memcpy_asm:
	pushl %ebp
	movl %esp, %ebp
	pushl %esi
	pushl %edi

	movl 8(%ebp), %edi    // dst
	movl 12(%ebp), %esi   // src
	movl 16(%ebp), %ecx   // n

	# -if n == 0, nothing to do
	testl %ecx, %ecx
	jz .memcpy_done

	# -if src or dst is NULL, do nothing (safe behavior)
	testl %esi, %esi
	jz .memcpy_done
	testl %edi, %edi
	jz .memcpy_done

	# detect 32-bit wrap for src and dst: if (addr + n) < addr then it's an overflow
	movl %esi, %eax
	addl %ecx, %eax
	cmpl %esi, %eax
	jb .memcpy_fault

	movl %edi, %eax
	addl %ecx, %eax
	cmpl %edi, %eax
	jb .memcpy_fault

	# compute src_end = esi + ecx
	movl %esi, %eax
	addl %ecx, %eax

	# -if dst < src or dst >= src_end => forward copy
	cmpl %esi, %edi
	jb .forward_copy
	cmpl %eax, %edi
	jae .forward_copy

	# overlapping with dst > src => backward copy (memmove semantics)
	# set pointers to last byte
	leal -1(%esi,%ecx), %esi
	leal -1(%edi,%ecx), %edi
	std
	# try aligned dword reverse copy if both aligned
	movl %esi, %eax
	testl $3, %eax
	jnz .rev_byte_copy
	movl %edi, %eax
	testl $3, %eax
	jnz .rev_byte_copy
	# both aligned: use movsd for bulk
	movl %ecx, %eax
	shrl $2, %ecx
	rep movsl
	movl %eax, %ecx
	andl $3, %ecx
	rep movsb
	cld
	jmp .memcpy_done

.rev_byte_copy:
	rep movsb
	cld


.forward_copy:
	# attempt aligned forward copy for speed when both pointers are dword-aligned
	movl %esi, %eax
	testl $3, %eax
	jnz .fwd_byte_copy
	movl %edi, %eax
	testl $3, %eax
	jnz .fwd_byte_copy
	# both aligned: use movsd for bulk copy
	movl %ecx, %eax
	shrl $2, %ecx
	rep movsl
	movl %eax, %ecx
	andl $3, %ecx
	rep movsb
	jmp .memcpy_done

.fwd_byte_copy:
	cld
	rep movsb

.memcpy_done:
	movl 8(%ebp), %eax    // return dst
	popl %edi
	popl %esi
	popl %ebp
	ret

.memcpy_fault:
	# push arguments and call mem_fault_handler(dst, n, 1)
	pushl $1
	pushl 16(%ebp) // n
	pushl 8(%ebp)  // dst
	call mem_fault_handler
	addl $12, %esp
	jmp .memcpy_done

// int memcmp_asm(const void *s1, const void *s2, size_t n)
	# returns (unsigned char)s1[i] - (unsigned char)s2[i]
	# cdecl: s1@[ebp+8], s2@[ebp+12], n@[ebp+16]
memcmp_asm:
	pushl %ebp
	movl %esp, %ebp
	pushl %esi
	pushl %edi

	movl 8(%ebp), %esi    // s1
	movl 12(%ebp), %edi   // s2
	movl 16(%ebp), %ecx   // n

	# -if n == 0 -> equal
	testl %ecx, %ecx
	jz .mc_equal

	# -if either pointer NULL -> treat as equal (safe, avoids crash)
	testl %esi, %esi
	jz .mc_equal
	testl %edi, %edi
	jz .mc_equal

	# detect wrap for s1 and s2: if (addr + n) < addr then unsafe -> treat as equal
	movl %esi, %eax
	addl %ecx, %eax
	cmpl %esi, %eax
	jb .mc_equal
	movl %edi, %eax
	addl %ecx, %eax
	cmpl %edi, %eax
	jb .mc_fault

	cld
	repe cmpsb
	jne .mc_mismatch

.mc_equal:
	movl $0, %eax
	jmp .mc_done

.mc_mismatch:
	# esi and edi have advanced past mismatch by 1
	movzbl -1(%esi), %eax
	movzbl -1(%edi), %edx
	subl %edx, %eax

.mc_done:
	popl %edi
	popl %esi
	popl %ebp
	ret

.mc_fault:
	# call mem_fault_handler(s1, n, 2)
	pushl $2
	pushl 16(%ebp)
	pushl 8(%ebp)
	call mem_fault_handler
	addl $12, %esp
	jmp .mc_equal

	# void *memset_asm(void *ptr, int value, size_t n)
	# cdecl: ptr@[ebp+8], value@[ebp+12], n@[ebp+16]
memset_asm:
	pushl %ebp
	movl %esp, %ebp
	pushl %edi

	movl 8(%ebp), %edi    // ptr
	movl 16(%ebp), %ecx   // n

	# -if n == 0, nothing to do
	testl %ecx, %ecx
	jz .ms_done

	# -if ptr is NULL, do nothing
	testl %edi, %edi
	jz .ms_done


	# detect wrap: if (ptr + n) < ptr -> unsafe -> call fault handler
	movl %edi, %eax
	addl %ecx, %eax
	cmpl %edi, %eax
	jb .ms_fault

	movb 12(%ebp), %al // value (low byte)
	cld
	rep stosb

.ms_done:
	movl 8(%ebp), %eax    // return ptr
	popl %edi
	popl %ebp
	ret

.ms_fault:
	# call mem_fault_handler(ptr, n, 3)
	pushl $3
	pushl 16(%ebp)
	pushl 8(%ebp)
	call mem_fault_handler
	addl $12, %esp
	jmp .ms_done

