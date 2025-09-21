
CFLAGS= -lm -Wall -DJ32_EMULATE_INTERNALS=1
#CFLAGS+= -m32
CFLAGS+= -g

all: emu tests
	make -C test

clean:
	rm -rf emu parse.gen.c step.gen.c
	make -C test clean

distclean: clean
	rm -rf *~
	make -C test distclean

emu: main.c main.h arm.h dis.c dis.h emu.c emu.h jazelle.c jazelle.h debug.c debug.h elf.c elf.h jvm.c jvm.h parse.gen.c step.gen.c
	gcc -o $@ main.c main.h dis.c emu.c elf.c jvm.c debug.c ${CFLAGS}

parse.gen.c step.gen.c: generate.py isa.dat
	python3 $^ parse.gen.c step.gen.c

.PHONY: all clean distclean tests

