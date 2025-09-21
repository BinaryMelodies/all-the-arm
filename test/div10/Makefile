
all: div10.a32 div10.m32 div10.t32 div10.a64 div10.class

clean:
	rm -rf div10.a32 div10.m32 div10.t32 div10.a64 div10.class

distclean: clean
	rm -rf *~

div10.a32: div10.c
	arm-none-eabi-gcc -nostdlib -march=armv3m          -o $@ $< -I.. # requires at least 3M for division by 10

div10.m32: div10.c
	arm-none-eabi-gcc -nostdlib -march=armv6t2 -mthumb -o $@ $< -I..

div10.t32: div10.c
	arm-none-eabi-gcc -nostdlib -march=armv7-m -mthumb -o $@ $< -I..
	../../makethumb $@

div10.a64: div10.c
	aarch64-elf-gcc   -nostdlib -march=armv8-a         -o $@ $< -I..

div10.class: div10.java
	javac --class-path .. div10.java

.PHONY: all clean distclean

