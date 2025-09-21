#ifndef _ELF_H
#define _ELF_H

/* Definitions for parsing and executing ELF binaries */

#include <stdint.h>
#include <stdio.h>
#include "main.h"
#include "arm.h"

enum
{
	EV_CURRENT = 1,
};

enum ei_class_t
{
	ELFCLASSNONE,
	ELFCLASS32,
	ELFCLASS64,
};
extern enum ei_class_t ei_class;

enum ei_data_t
{
	ELFDATANONE,
	ELFDATA2LSB,
	ELFDATA2MSB,
};
extern enum ei_data_t ei_data;

enum e_machine_t
{
	EM_ARM = 40,
	EM_PJ = 91,
	EM_OLD_PICOJAVA = 99, // as given in the picoJava-II Programmer's Reference Manual, collides with EM_SNP1K
	EM_AARCH64 = 183,
};

enum
{
	// based on Linux source
	EF_ARM_EABI_MASK = 0xFF000000,
	EF_ARM_EABI_SHIFT = 24,
	EF_ARM_EABI_UNKNOWN = 0 << EF_ARM_EABI_SHIFT,
	EF_ARM_EABI_VER1 = 1 << EF_ARM_EABI_SHIFT,
	EF_ARM_EABI_VER2 = 2 << EF_ARM_EABI_SHIFT,
	EF_ARM_EABI_VER3 = 3 << EF_ARM_EABI_SHIFT,
	EF_ARM_EABI_VER4 = 4 << EF_ARM_EABI_SHIFT,
	EF_ARM_EABI_VER5 = 5 << EF_ARM_EABI_SHIFT,

	EF_ARM_OLD_ABI = 0x00000100,
	EF_ARM_NEW_ABI = 0x00000080,

	EF_ARM_SOFT_FLOAT = 0x200,
	EF_ARM_VFP_FLOAT = 0x400,
	EF_ARM_MAVERICK_FLOAT = 0x400,

	EF_ARM_LE8 = 0x00400000,
	EF_ARM_BE8 = 0x00800000,
};

enum
{
	PT_LOAD = 1,

	SHT_PROGBITS = 1,
	SHT_SYMTAB = 2,
};

enum
{
	A32_OABI_SYS_BASE = 0x900000,

	A32_SYS_EXIT = 1,
	A32_SYS_READ = 3,
	A32_SYS_WRITE = 4,
	A32_SYS_OPEN = 5,
	A32_SYS_CLOSE = 6,
	A32_SYS_CREAT = 7,
	A32_SYS_UNLINK = 10,
	A32_SYS_EXECVE = 11,
	A32_SYS_CHDIR = 12,
	A32_SYS_LSEEK = 19,
	A32_SYS_TIMES = 43,
	A32_SYS_BRK = 45,

	J32_SYS_GETBYTES = 0x10000000,

	A64_SYS_EXIT = 93,
	A64_SYS_READ = 63,
	A64_SYS_WRITE = 64,
	A64_SYS_OPENAT = 56,
	A64_SYS_CLOSE = 57,
	A64_SYS_UNLINKAT = 35,
	A64_SYS_EXECVE = 221,
	A64_SYS_CHDIR = 49,
	A64_SYS_LLSEEK = 62,
	A64_SYS_TIMES = 153,
	A64_SYS_BRK = 214,
};

// returns new stack
extern uint64_t build_initial_stack(arm_state_t * cpu, uint64_t stack, int argc, char ** argv, char ** envp);
extern void setup_initial_state(arm_state_t * cpu, environment_t * env, int argc, char ** argv, char ** envp);

extern void read_elf_file(FILE * input_file, environment_t * env);

extern bool a32_linux_oabi_syscall(arm_state_t * cpu, uint32_t swi_number);
extern bool a32_linux_eabi_syscall(arm_state_t * cpu);
extern bool a64_linux_syscall(arm_state_t * cpu);

// simulates a Linux syscall from Jazelle mode
extern bool j32_linux_syscall(arm_state_t * cpu);

#endif // _ELF_H
