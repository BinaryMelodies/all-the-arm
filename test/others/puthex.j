; Test hexadecimal output

.class public others/puthex
.super java/lang/Object

.method public <init>()V
	.limit	stack	1

	aload_0
	invokespecial java/lang/Object/<init>()V
	return
.end method

.method static public _start()V
	.limit	stack	1

	ldc 0x1A2B3C4D
	invokestatic others/puthex/putword(I)V
	invokestatic others/puthex/exit()V
	return
.end method

.method static public putword(I)V
	.limit	stack	2

	iload_0
	bipush 16
	ishr
	invokestatic others/puthex/puthalf(I)V
	iload_0
	invokestatic others/puthex/puthalf(I)V
	return
.end method

.method static public puthalf(I)V
	.limit	stack	2

	iload_0
	bipush 8
	ishr
	invokestatic others/puthex/putbyte(I)V
	iload_0
	invokestatic others/puthex/putbyte(I)V
	return
.end method

.method static public putbyte(I)V
	.limit	stack	2

	iload_0
	bipush 4
	ishr
	invokestatic others/puthex/putnibble(I)V
	iload_0
	invokestatic others/puthex/putnibble(I)V
	return
.end method

.method static public putnibble(I)V
	.limit	stack	3

	iload_0
	bipush 15
	iand
	dup
	bipush 10
	if_icmpge above9
	bipush 48
	goto eithercase
above9:
	bipush 55
eithercase:
	iadd
	invokestatic others/puthex/putchar(I)V
	return
.end method

.method static public putchar(I)V
	.limit	stack	6

	iconst_m1
	invokestatic abi/Linux/brk(I)I

	iconst_1
	bipush 1
	newarray byte
	dup
	iconst_0
	iload_0
	bastore
	iconst_0
	iconst_1
	invokestatic abi/Linux/write(I[BII)I
	pop

	invokestatic abi/Linux/brk(I)I

	return
.end method

.method static public exit()V
	.limit	stack	1

	iconst_0
	invokestatic abi/Linux/exit(I)V
	return
.end method

.method static public main([Ljava/lang/String;)V
	.limit	stack	2

	ldc "user.dir"
	invokestatic java/lang/System/getProperty(Ljava/lang/String;)Ljava/lang/String;
	ldc "/syscall.so"
	invokevirtual java/lang/String/concat(Ljava/lang/String;)Ljava/lang/String;
	invokestatic java/lang/System/load(Ljava/lang/String;)V
	invokestatic others/puthex/_start()V
	return
.end method

