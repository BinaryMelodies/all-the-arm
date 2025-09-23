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
	SHT_ARM_ATTRIBUTES = 0x70000003,

	Tag_CPU_raw_name = 4,
	Tag_CPU_name = 5,
	Tag_CPU_arch = 6,
	Tag_CPU_arch_profile = 7,
	Tag_ARM_ISA_use = 8,
	Tag_THUMB_ISA_use = 9,
	Tag_FP_arch = 10,
	Tag_WMMX_arch = 11,
	Tag_Advanced_SIMD_arch = 12,
	Tag_PCS_config = 13,
	Tag_ABI_PCS_R9_use = 14,
	Tag_ABI_PCS_RW_data = 15,
	Tag_ABI_PCS_RO_data = 16,
	Tag_ABI_PCS_GOT_use = 17,
	Tag_ABI_PCS_wchar_t = 18,
	Tag_ABI_FP_rounding = 19,
	Tag_ABI_FP_denormal = 20,
	Tag_ABI_FP_exceptions = 21,
	Tag_ABI_FP_user_exceptions = 22,
	Tag_ABI_FP_number_model = 23,
	Tag_ABI_align_needed = 24,
	Tag_ABI_align_preserved = 25,
	Tag_ABI_enum_size = 26,
	Tag_ABI_HardFP_use = 27,
	Tag_ABI_VFP_use = 28,
	Tag_ABI_WMMX_args = 29,
	Tag_ABI_optimization_goals = 30,
	Tag_ABI_FP_optimization_goals = 31,
	Tag_compatibility = 32,
	Tag_CPU_unaligned_access = 34,
	Tag_FP_HP_extension = 36,
	Tag_ABI_FP_16bit_format = 38,
	Tag_MPextension_use = 42,
	Tag_DIV_use = 44,
	Tag_DSP_extension = 46,
	Tag_MVE_arch = 48,
	Tag_PAC_extension = 50,
	Tag_BTI_extension = 52,
	Tag_nodefaults = 64,
	Tag_also_compatible_with = 65,
	Tag_T2EE_use = 66,
	Tag_conformance = 67,
	Tag_Virtualization_use = 68,
	Tag_FramePointer_use = 72,
	Tag_BTI_use = 74,
	Tag_PACRET_use = 76,
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
