
@ Tests out switching between instruction sets (ARM32, Thumb, ThumbEE and Jazelle with extensions)

@ must be loaded at 0x8000
@ run with -v7+javaext
	.global	_start

	.arm

_start:
	nop

	mov	r0, #1
	ldr	r1, =message_arm
	mov	r2, #length_message_arm
	swi	#0x900000 + 4

	nop

	ldr	r0, =thumb_start+1
	bx	r0

	.thumb
thumb_start:
	nop

	mov	r0, #1
	ldr	r1, =message_thumb
	mov	r2, #length_message_thumb
	mov	r7, #4
	svc	#0

	@ enterx
	.inst.w	0xF3BF8F1F

$t.x:
	mov	r0, #1
	ldr	r1, =message_thumbee
	mov	r2, #length_message_thumbee
	mov	r7, #4
	svc	#0


	ldr	r1, =1
	ldr	r0, =0
	@ chka r1, r0
	.inst.n	0xCA01

	@ leavex
	.inst.w	0xF3BF8F0F

$t:
	nop
	ldr	r0, =arm_start
	bx	r0

	.arm

arm_start:
	nop
	ldr	lr, =jazelle_start
	ldr	r6, =0xC00
	ldr	r12, =0f
0:
	bxj	r12

# buffer byte so the GNU assembler does automatically insert $d before all data directives
	.byte	0
$j:
jazelle_start:
	@ nop
	.byte	0x00
	@ sipush 1
	.byte	0x11, 0, 1
	@ sipush message_jazelle & 0xFFFF
	.byte	0x11
	.byte	((message_jazelle - _start + 0x8000) >> 8) & 0xFF
	.byte	(message_jazelle - _start + 0x8000) & 0xFF
	@ sethi message_jazelle >> 16
	.byte	0xED
	.byte	((message_jazelle - _start + 0x8000) >> 24) & 0xFF
	.byte	((message_jazelle - _start + 0x8000) >> 16) & 0xFF
	@ sipush length_message_jazelle
	.byte	0x11
	.byte	(length_message_jazelle >> 8) & 0xFF
	.byte	length_message_jazelle & 0xFF
	@ sipush 4
	.byte	0x11, 0, 4
	@ swi
	.byte	0xFE, 0x01
	@ nop
	.byte	0x00
	@ sipush (thumb_exit+1) & 0xFFFF
	.byte	0x11
	.byte	((thumb_exit - _start + 0x8000 + 1) >> 8) & 0xFF
	.byte	(thumb_exit - _start + 0x8000 + 1) & 0xFF
	@ sethi (thumb_exit+1) >> 16
	.byte	0xED
	.byte	((thumb_exit - _start + 0x8000 + 1) >> 24) & 0xFF
	.byte	((thumb_exit - _start + 0x8000 + 1) >> 16) & 0xFF
	@ ret_from_jazelle
	.byte	0xFE, 0x00

	.align	2
	.thumb
thumb_exit:
	mov	r7, #1
	swi	#0
0:	b	0b

message_arm:
	.ascii	"Called from ARM mode"
	.byte	10
	.equ	length_message_arm, . - message_arm

message_thumb:
	.ascii	"Called from Thumb mode"
	.byte	10
	.equ	length_message_thumb, . - message_thumb

message_thumbee:
	.ascii	"Called from ThumbEE mode"
	.byte	10
	.equ	length_message_thumbee, . - message_thumbee

message_jazelle:
	.ascii	"Called from Jazelle mode"
	.byte	10
	.equ	length_message_jazelle, . - message_jazelle

