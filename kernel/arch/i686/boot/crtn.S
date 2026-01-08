# SPDX-License-Identifier: AGPL-3.0-or-later

.section .init
	# gcc will nicely put the contents of crtend.o's .init section here.
	popl %ebp
	ret

.section .fini
	# gcc will nicely put the contents of crtend.o's .fini section here.
	popl %ebp
	ret
