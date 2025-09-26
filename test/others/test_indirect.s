
	.global	_start
_start:
	sipush	%lo16 target
	sethi	%hi16 target
	# jsr_indirect
	.byte	0xFE, 0x02

	aconst_null
target:
	iconst_m1
0:
	goto	0b

