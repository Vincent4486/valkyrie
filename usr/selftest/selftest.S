// SPDX-License-Identifier: BSD-3-Clause

.section .data
filepath:
    .asciz "/test/usr.txt"
content:
    .ascii "Hello from Valkyrie!\n"
    content_len = . - content

.section .bss
    .lcomm buffer, 64    # Reserve 64 bytes to read the file back into

.section .text
.global _start

_start:
    # 1. OPEN for Writing (O_CREAT | O_WRONLY | O_TRUNC = 01101 in octal or 0x241)
    # We use 0644 for permissions (rw-r--r--)
    movl $5, %eax        # SYS_OPEN
    movl $filepath, %ebx
    movl $0x241, %ecx    # Flags: Create, Write-Only, Truncate
    movl $0644, %edx     # Mode: Permissions
    int  $0x80
    
    pushl %eax           # Save the File Descriptor (FD) returned in %eax

    # 2. WRITE to the file
    movl $4, %eax        # SYS_WRITE
    movl (%esp), %ebx    # Use the FD we just pushed
    movl $content, %ecx
    movl $content_len, %edx
    int  $0x80

    # 3. CLOSE the file
    movl $6, %eax        # SYS_CLOSE
    popl %ebx            # Pop the FD into %ebx to close it
    int  $0x80

    # 4. OPEN for Reading (O_RDONLY = 0)
    movl $5, %eax        # SYS_OPEN
    movl $filepath, %ebx
    movl $0, %ecx        # Flags: Read-Only
    int  $0x80
    
    pushl %eax           # Save the new FD

    # 5. READ from the file into our .bss buffer
    movl $3, %eax        # SYS_READ
    movl (%esp), %ebx    # Use the FD
    movl $buffer, %ecx   # Destination buffer
    movl $64, %edx       # Max bytes to read
    int  $0x80
    
    pushl %eax           # Save the number of bytes actually read (return value)

    # 6. WRITE the buffer to STDOUT (1) to print it
    movl $4, %eax        # SYS_WRITE
    movl $1, %ebx        # stdout
    movl $buffer, %ecx
    movl 0(%esp), %edx   # Use the number of bytes read as the write count
    int  $0x80

    # 7. CLOSE and EXIT
    addl $4, %esp        # Clean up the byte count
    popl %ebx            # Get the FD back
    movl $6, %eax        # SYS_CLOSE
    int  $0x80

    movl $1, %eax        # SYS_EXIT
    xorl %ebx, %ebx      # Status 0
    int  $0x80
