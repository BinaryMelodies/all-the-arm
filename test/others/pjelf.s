
	.global	_start
_start:
	sipush	1
	sipush	%lo16 message
	sethi	%hi16 message
	sipush	message_length
	sipush	4
	.byte	0xFE, 0x01

	sipush	123
	sipush	1
	.byte	0xFE, 0x01

message:
	.ascii	"Hello!"
	.byte	10
	.equ	message_length, . - message

