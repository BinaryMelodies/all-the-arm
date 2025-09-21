
all: env.a32 env.m32 env.t32 env.a64 env.class

clean:
	rm -rf env.a32 env.m32 env.t32 env.a64 env.class *.s

distclean: clean
	rm -rf *~

env.a32: env.c
	arm-none-eabi-gcc -nostdlib -march=armv3m          -o $@ $< -I.. # requires at least 3M for division by 10
	arm-none-eabi-gcc -nostdlib -march=armv3m          -o $@.s $< -I.. -S

env.m32: env.c
	arm-none-eabi-gcc -nostdlib -march=armv6t2 -mthumb -o $@ $< -I..

env.t32: env.c
	arm-none-eabi-gcc -nostdlib -march=armv7-m -mthumb -o $@ $< -I..
	../../makethumb $@

env.a64: env.c
	aarch64-elf-gcc   -nostdlib -march=armv8-a         -o $@ $< -I..
	aarch64-elf-gcc   -nostdlib -march=armv8-a         -o $@.s $< -I.. -S

env.class: env.java
	javac --class-path .. env.java

.PHONY: all clean distclean

