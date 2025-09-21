
CFLAGS= -lm -Wall -DJ32_EMULATE_INTERNALS=1
CFLAGS+= -g

all: emu tests

clean:
	rm -rf emu parse.gen.c step.gen.c

distclean: clean
	rm -rf *~

emu: main.c main.h arm.h dis.c dis.h emu.c emu.h jazelle.c jazelle.h debug.c debug.h elf.c elf.h jvm.c jvm.h parse.gen.c step.gen.c
	gcc -o $@ main.c main.h dis.c emu.c elf.c jvm.c debug.c ${CFLAGS}

parse.gen.c step.gen.c: generate.py isa.dat
	python3 $^ parse.gen.c step.gen.c

.PHONY: all clean distclean tests

