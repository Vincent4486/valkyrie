# SPDX-License-Identifier: GPL-3.0-only

.section .init
.global _init
_init:
	pushl %ebp
	movl %esp, %ebp
	# gcc will nicely put the contents of crtbegin.o's .init section here.

.section .fini
.global _fini
_fini:
	pushl %ebp
	movl %esp, %ebp
	# gcc will nicely put the contents of crtbegin.o's .fini section here.

.section .note.GNU-stack,"",@progbits
