
	.text
	.global	_start
	.syntax	unified

_start:
.ifdef	THUMB32
	.arm
	ldr	r0, =_thumb_start
	bx	r0

	.thumb
	.thumb_func
_thumb_start:
.endif

	ldr	sp, =stack_top
	ldr	r0, =0x1A2B3C4D
	bl	putword
	bl	exit

putword:
	stmdb	sp!, {r0, lr}
	lsr	r0, r0, #16
	bl	puthalf
	ldmia	sp!, {r0, lr}

puthalf:
	stmdb	sp!, {r0, lr}
	lsr	r0, r0, #8
	bl	putbyte
	ldmia	sp!, {r0, lr}

putbyte:
	stmdb	sp!, {r0, lr}
	lsr	r0, r0, #4
	bl	putnibble
	ldmia	sp!, {r0, lr}

putnibble:
	and	r0, r0, #0xF
	cmp	r0, #10
	ite	lt
	addlt	r0, r0, #'0'
	addge	r0, r0, #'A' - 10

putchar:
	str	r0, [sp, #-4]!
	mov	r0, #1
	mov	r1, sp
	mov	r2, #1
.ifdef	AARCH32
	swi	0x900000 + 4
.endif
.ifdef	THUMB32
	mov	r7, #4
	swi	0
.endif
	add	sp, sp, #4
	mov	pc, lr

exit:
	mov	r0, #0
.ifdef	AARCH32
	swi	0x900000 + 1
.endif
.ifdef	THUMB32
	mov	r7, #1
	swi	0
.endif

	.bss

	.skip	0x200
stack_top:

