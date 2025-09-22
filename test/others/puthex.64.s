
# Test hexadecimal output

	.text
	.global	_start

_start:
	ldr	x0, =stack_top
	mov	sp, x0
	ldr	x0, =0x1A2B3C4D
	bl	putword
	bl	exit

putword:
	stp	x0, x30, [sp, #-16]!
	lsr	x0, x0, #16
	bl	puthalf
	ldp	x0, x30, [sp], #16

puthalf:
	stp	x0, x30, [sp, #-16]!
	lsr	x0, x0, #8
	bl	putbyte
	ldp	x0, x30, [sp], #16

putbyte:
	stp	x0, x30, [sp, #-16]!
	lsr	x0, x0, #4
	bl	putnibble
	ldp	x0, x30, [sp], #16

putnibble:
	and	x0, x0, #0xF
	add	x1, x0, #'0'
	add	x2, x0, #'A' - 10
	cmp	x0, #10
	csel	x0, x1, x2, lt

putchar:
	str	x0, [sp, #-16]!
	mov	x0, #1
	mov	x1, sp
	mov	x2, #1
	mov	w8, #64
	svc	0
	add	sp, sp, #16
	ret

exit:
	mov	x0, #0
	mov	w8, #93
	svc	0

	.bss

	.skip	0x200
	.align	16
stack_top:

