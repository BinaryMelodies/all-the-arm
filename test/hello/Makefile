
# hello.a32: 32-bit ARM executable for ARMv2
# hello.a32v7: 32-bit ARM executable for ARMv7
# hello.m32: 32-bit Thumb executable for ARMv4T, starts in ARM mode
# hello.t32: 32-bit Thumb-2 executable for ARMv7
# hello.a64: 64-bit ARM executable for ARMv8
# hello.class: Java class (see microjvm.txt)

all: hello.a32 hello.a32.be32 hello.a32.be8 hello.m32 hello.m32.be32 hello.m32.be8 hello.a32v7 hello.t32 hello.a64 hello.a64.be hello.class

clean:
	rm -f hello.a32 hello.a32.be32 hello.a32.be8 hello.m32 hello.m32.be32 hello.m32.be8 hello.a32v7 hello.t32 hello.a64 hello.a64.be hello.class

distclean: clean
	rm -f *~ abi/*~

hello.a32: hello.c
	arm-none-eabi-gcc -nostdlib -march=armv2 -o $@ $< -I..

hello.a32.be32 hello.a32.be8: hello.c
	arm-none-eabi-gcc -nostdlib -mbig-endian -march=armv2 -o hello.a32.be32 $< -I..
	../../be32tobe8 hello.a32.be32 hello.a32.be8

hello.m32: hello.c
	arm-none-eabi-gcc -nostdlib -march=armv4t -mthumb -mthumb-interwork -o $@ $< -I..

hello.m32.be32 hello.m32.be8: hello.c
	arm-none-eabi-gcc -nostdlib -mbig-endian -march=armv4t -mthumb -mthumb-interwork -o hello.m32.be32 $< -I..
	../../be32tobe8 hello.m32.be32 hello.m32.be8

hello.a32v7: hello.c
	arm-none-eabi-gcc -nostdlib -march=armv7-a -marm -mthumb-interwork -o $@ $< -I..

hello.t32: hello.c
	arm-none-eabi-gcc -nostdlib -march=armv7-m -mthumb -mthumb-interwork -o $@ $< -I..
	../../makethumb $@

hello.a64: hello.c
	aarch64-elf-gcc -nostdlib -march=armv8-a -o $@ $< -I..

hello.a64.be: hello.c
	aarch64-elf-gcc -nostdlib -mbig-endian -march=armv8-a -o $@ $< -I ..

hello.class: hello.java
	javac --class-path .. hello.java

.PHONY: all clean distclean

