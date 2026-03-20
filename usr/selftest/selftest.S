// SPDX-License-Identifier: BSD-3-Clause

.section .data
filepath:
   .asciz "/test/usr.txt"
content:
   .ascii "Hello from Valkyrie!\n"
   content_len = . - content
expected_eacces:
   .long -13

.section .bss
   .lcomm buffer, 64

.section .text
.global _start

.equ SYS_EXIT, 1
.equ SYS_READ, 3
.equ SYS_WRITE, 4
.equ SYS_OPEN, 5
.equ SYS_CLOSE, 6
.equ SYS_CHMOD, 15
.equ SYS_SETUID, 23
.equ SYS_CHOWN, 182

.equ O_RDONLY, 0
.equ O_WRONLY, 1
.equ O_TRUNC, 0x0200
.equ O_CREAT, 0x0040

_start:
   # Create file and write content
   movl $SYS_OPEN, %eax
   movl $filepath, %ebx
   movl $(O_CREAT | O_WRONLY | O_TRUNC), %ecx
   movl $0644, %edx
   int $0x80
   testl %eax, %eax
   js fail

   pushl %eax

   movl $SYS_WRITE, %eax
   movl (%esp), %ebx
   movl $content, %ecx
   movl $content_len, %edx
   int $0x80
   cmpl $content_len, %eax
   jne fail_popfd

   movl $SYS_CLOSE, %eax
   popl %ebx
   int $0x80
   testl %eax, %eax
   js fail

   # Root: chmod/chown should succeed
   movl $SYS_CHMOD, %eax
   movl $filepath, %ebx
   movl $0600, %ecx
   int $0x80
   testl %eax, %eax
   js fail

   movl $SYS_CHOWN, %eax
   movl $filepath, %ebx
   movl $0, %ecx
   movl $0, %edx
   int $0x80
   testl %eax, %eax
   js fail

   # Drop to non-root
   movl $SYS_SETUID, %eax
   movl $1000, %ebx
   int $0x80
   testl %eax, %eax
   js fail

   # Non-root: chmod/chown must fail with -EACCES
   movl $SYS_CHMOD, %eax
   movl $filepath, %ebx
   movl $0644, %ecx
   int $0x80
   cmpl expected_eacces, %eax
   jne fail

   movl $SYS_CHOWN, %eax
   movl $filepath, %ebx
   movl $1000, %ecx
   movl $1000, %edx
   int $0x80
   cmpl expected_eacces, %eax
   jne fail

   # Non-root open for write should fail on 0600 root-owned file
   movl $SYS_OPEN, %eax
   movl $filepath, %ebx
   movl $O_WRONLY, %ecx
   xorl %edx, %edx
   int $0x80
   cmpl expected_eacces, %eax
   jne fail

   # Non-root open for read should also fail on 0600 root-owned file
   movl $SYS_OPEN, %eax
   movl $filepath, %ebx
   movl $O_RDONLY, %ecx
   xorl %edx, %edx
   int $0x80
   cmpl expected_eacces, %eax
   jne fail

   movl $SYS_EXIT, %eax
   xorl %ebx, %ebx
   int $0x80

fail_popfd:
   movl $SYS_CLOSE, %eax
   popl %ebx
   int $0x80

fail:
   movl $SYS_EXIT, %eax
   movl $1, %ebx
   int $0x80
