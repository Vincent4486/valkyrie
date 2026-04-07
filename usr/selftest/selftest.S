// SPDX-License-Identifier: BSD-3-Clause

.section .data
stdin_prompt:
   .ascii "stdin blocking test: type text and press Enter\n"
stdin_prompt_end:
stdin_prefix:
   .ascii "stdin bytes: "
stdin_prefix_end:
dirpath:
   .asciz "/"
newline:
   .byte 0x0A

.section .bss
   .lcomm stdinbuf, 128
   .lcomm dirbuf, 512
   .lcomm outbuf, 32

.section .text
.global _start

.equ SYS_EXIT, 1
.equ SYS_READ, 3
.equ SYS_WRITE, 4
.equ SYS_OPEN, 5
.equ SYS_CLOSE, 6

.equ O_RDONLY, 0

# FAT directory entry offsets/sizes
.equ DIR_ENTRY_SIZE, 32
.equ DIR_NAME_OFF, 0
.equ DIR_ATTR_OFF, 11

_start:
   # Blocking stdin read test (fd 0)
   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $stdin_prompt, %ecx
   movl $(stdin_prompt_end - stdin_prompt), %edx
   int $0x80

   movl $SYS_READ, %eax
   xorl %ebx, %ebx
   movl $stdinbuf, %ecx
   movl $127, %edx
   int $0x80
   testl %eax, %eax
   jle fail

   # Preserve byte count across later syscalls.
   pushl %eax

   # Print what was read from stdin.
   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $stdin_prefix, %ecx
   movl $(stdin_prefix_end - stdin_prefix), %edx
   int $0x80

   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $stdinbuf, %ecx
   movl (%esp), %edx
   int $0x80

   # Emit trailing newline if input did not end with one.
   movl (%esp), %edx
   testl %edx, %edx
   jle stdin_line_done
   decl %edx
   movb stdinbuf(%edx), %al
   cmpb $0x0A, %al
   je stdin_line_done

   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $newline, %ecx
   movl $1, %edx
   int $0x80

stdin_line_done:
   addl $4, %esp

   # Open /test as read-only directory stream
   movl $SYS_OPEN, %eax
   movl $dirpath, %ebx
   movl $O_RDONLY, %ecx
   xorl %edx, %edx
   int $0x80
   testl %eax, %eax
   js fail

   pushl %eax

read_sector:
   # Read next chunk of directory bytes
   movl $SYS_READ, %eax
   movl (%esp), %ebx
   movl $dirbuf, %ecx
   movl $512, %edx
   int $0x80
   testl %eax, %eax
   jle done

   # ecx = bytes read
   movl %eax, %ecx
   movl $dirbuf, %esi

entry_loop:
   cmpl $DIR_ENTRY_SIZE, %ecx
   jl read_sector

   # End marker (0x00) => end of directory
   cmpb $0x00, DIR_NAME_OFF(%esi)
   je done

   # Deleted entry (0xE5) => skip
   cmpb $0xE5, DIR_NAME_OFF(%esi)
   je skip_entry

   # Long filename entry (attr 0x0F) => skip
   movzbl DIR_ATTR_OFF(%esi), %eax
   andl $0x0F, %eax
   cmpl $0x0F, %eax
   je skip_entry

   # Build printable 8.3 name into outbuf
   movl $outbuf, %edi

   # Copy base name (up to 8 chars, stop at spaces)
   xorl %edx, %edx
base_loop:
   cmpl $8, %edx
   jge ext_check
   movb DIR_NAME_OFF(%esi,%edx,1), %al
   cmpb $' ', %al
   je ext_check
   movb %al, (%edi)
   incl %edi
   incl %edx
   jmp base_loop

ext_check:
   # Determine if extension exists (non-space in bytes 8..10)
   movb DIR_NAME_OFF+8(%esi), %al
   cmpb $' ', %al
   jne ext_emit_dot
   movb DIR_NAME_OFF+9(%esi), %al
   cmpb $' ', %al
   jne ext_emit_dot
   movb DIR_NAME_OFF+10(%esi), %al
   cmpb $' ', %al
   je emit_line

ext_emit_dot:
   movb $'.', (%edi)
   incl %edi

   # Copy extension chars
   movl $8, %edx
ext_loop:
   cmpl $11, %edx
   jge emit_line
   movb DIR_NAME_OFF(%esi,%edx,1), %al
   cmpb $' ', %al
   je emit_line
   movb %al, (%edi)
   incl %edi
   incl %edx
   jmp ext_loop

emit_line:
   # name length = edi - outbuf
   movl %edi, %edx
   subl $outbuf, %edx

   # Write name
   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $outbuf, %ecx
   int $0x80

   # Write newline
   movl $SYS_WRITE, %eax
   movl $1, %ebx
   movl $newline, %ecx
   movl $1, %edx
   int $0x80

skip_entry:
   addl $DIR_ENTRY_SIZE, %esi
   subl $DIR_ENTRY_SIZE, %ecx
   jmp entry_loop

done:
   movl $SYS_CLOSE, %eax
   popl %ebx
   int $0x80

   movl $SYS_EXIT, %eax
   xorl %ebx, %ebx
   int $0x80

fail:
   movl $SYS_EXIT, %eax
   movl $1, %ebx
   int $0x80
