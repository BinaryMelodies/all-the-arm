
all: cat.a32 cat.m32 cat.t32 cat.a64 cat.class

clean:
	rm -rf cat.a32 cat.m32 cat.t32 cat.a64 cat.class *.s

distclean: clean
	rm -rf *~

cat.a32: cat.c
	arm-none-eabi-gcc -nostdlib -march=armv2           -o $@ $< -I.. # requires at least 3M for division by 10
	arm-none-eabi-gcc -nostdlib -march=armv2           -o $@.s $< -I.. -S

cat.m32: cat.c
	arm-none-eabi-gcc -nostdlib -march=armv6t2 -mthumb -o $@ $< -I..

cat.t32: cat.c
	arm-none-eabi-gcc -nostdlib -march=armv7-m -mthumb -o $@ $< -I..
	../../makethumb $@

cat.a64: cat.c
	aarch64-elf-gcc   -nostdlib -march=armv8-a         -o $@ $< -I..
	aarch64-elf-gcc   -nostdlib -march=armv8-a         -o $@.s $< -I.. -S

cat.class: cat.java
	javac --class-path .. cat.java

.PHONY: all clean distclean

