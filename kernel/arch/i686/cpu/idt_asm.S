	# SPDX-License-Identifier: AGPL-3.0-or-later

.code32

	# void __attribute__((cdecl)) i686_IDT_Load(IDTDescriptor* idtDescriptor);
.global i686_IDT_Load
i686_IDT_Load:

	# make new call frame
    pushl %ebp             # save old call frame
    movl %esp, %ebp         # initialize new call frame
    
	# load idt
    movl 8(%ebp), %eax
    lidt (%eax)

	# restore old call frame
    movl %ebp, %esp
    popl %ebp
    ret