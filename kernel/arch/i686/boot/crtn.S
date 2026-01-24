# SPDX-License-Identifier: GPL-3.0-only

.section .init
	# gcc will nicely put the contents of crtend.o's .init section here.
	popl %ebp
	ret

.section .fini
	# gcc will nicely put the contents of crtend.o's .fini section here.
	popl %ebp
	ret

.section .note.GNU-stack,"",@progbits
