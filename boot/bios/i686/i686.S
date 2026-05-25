/* SPDX-License-Identifier: GPL-3.0-only */

.section .text, "ax"

/* ------------------------------------------------------------------ */
/*  void outb(uint16_t port, uint8_t val)                              */
/*  Write one byte to an I/O port.                                     */
/* ------------------------------------------------------------------ */
.global outb
.type   outb, @function
outb:
	movl	4(%esp), %edx		/* port */
	movl	8(%esp), %eax		/* val */
	outb	%al, %dx
	ret

/* ------------------------------------------------------------------ */
/*  uint8_t inb(uint16_t port)                                         */
/*  Read one byte from an I/O port.                                    */
/* ------------------------------------------------------------------ */
.global inb
.type   inb, @function
inb:
	movl	4(%esp), %edx		/* port */
	xorl	%eax, %eax
	inb	%dx, %al
	ret
