
all: all_isa puthex.a32 puthex.t32 puthex.a64 puthex.class test_clinit.class test_static.class test_string.class test_indirect.class

clean:
	rm -f all_isa all_isa.o puthex.a32 puthex.a32.o puthex.t32 puthex.t32.o puthex.a64 puthex.a64.o puthex.class test_clinit.class test_static.class test_string.class test_indirect.class test_indirect\$$Call.class

distclean: clean
	rm -f *~

all_isa: all_isa.s
	arm-none-eabi-as -o all_isa.o all_isa.s
	arm-none-eabi-ld -o all_isa all_isa.o

puthex.a32: puthex.32.s
	arm-none-eabi-as -march=armv2 -o $@.o $< --defsym=AARCH32=1
	arm-none-eabi-ld -o $@ $@.o

puthex.t32: puthex.32.s
	arm-none-eabi-as -march=armv6t2 -mthumb -o $@.o $< --defsym=THUMB32=1
	arm-none-eabi-ld -o $@ $@.o

puthex.a64: puthex.64.s
	aarch64-elf-as -march=armv8-a -o $@.o $< --defsym=AARCH64=1
	aarch64-elf-ld -o $@ $@.o

puthex.class: puthex.j
	jasmin puthex.j -d ..

# Note: this requires GNU Binutils for picoJava
pjelf: pjelf.s
	pj-elf-as $< -o $@.o
	pj-elf-ld $@.o -o $@

# Note: this requires GNU Binutils for picoJava
test_indirect: test_indirect.s
	pj-elf-as $< -o $@.o
	pj-elf-ld $@.o -o $@

%.class: %.java
	javac --class-path .. $<

.PHONY: all clean distclean

