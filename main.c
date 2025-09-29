
/* The main interface for the ARM emulator/disassembler */

#include <assert.h>
#include <byteswap.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include "dis.h"
#include "emu.h"
#include "debug.h"
#include "elf.h"
#include "jvm.h"
#include "jazelle.h"

const char * const arm_instruction_set_names[] =
{
	[ISA_UNKNOWN] = "unknown",
	[ISA_AARCH26] = "ARM26",
	[ISA_AARCH32] = "ARM32",
	[ISA_THUMB32] = "Thumb",
	[ISA_JAZELLE] = "Java",
	[ISA_THUMBEE] = "ThumbEE",
	[ISA_AARCH64] = "ARM64",
};

const char * const arm_version_names[] =
{
	[ARM_UNKNOWN] = "unknown",
	[ARMV1] = "ARMv1",
	[ARMV2] = "ARMv2",
	[ARMV3] = "ARMv3",
	[ARMV4] = "ARMv4",
	[ARMV5] = "ARMv5",
	[ARMV6] = "ARMv6",
	[ARMV7] = "ARMv7",
	[ARMV8] = "ARMv8",
	[ARMV81] = "ARMv8.1",
	[ARMV82] = "ARMv8.2",
	[ARMV9] = "ARMv9",
};

const char * const arm_fp_version_names[] =
{
	[ARM_VFPV1] = "VFPv1", // ARM10 (v5TE)
	[ARM_VFPV2] = "VFPv2", // (v5TE)
	[ARM_VFPV3] = "VFPv3", // (v7)
	[ARM_VFPV4] = "VFPv4", // (v7)
	[ARM_VFPV5] = "VFPv5", // (v7E-M, v8-R)
};

const char * const arm_simd_version_names[ARM_VFPV5 + 1] =
{
	[ARM_VFPV3] = "Neon v1",
	[ARM_VFPV4] = "Neon v2",
};

uint64_t memory_changed_lowest = -1;
uint64_t memory_changed_highest = 0;

#if MEMORY_SINGLE_BLOCK

uint8_t * memory;

static bool _memory_read(arm_state_t * cpu, uint64_t address, void * buffer, size_t size, bool privileged_mode)
{
	memcpy(buffer, &memory[address], size);
	return true;
}

static bool _memory_write(arm_state_t * cpu, uint64_t address, const void * buffer, size_t size, bool privileged_mode)
{
	if(address < memory_changed_lowest)
		memory_changed_lowest = address;
	if(address + (size - 1) > memory_changed_highest)
		memory_changed_highest = address + (size - 1);
	memcpy(&memory[address], buffer, size);
	return true;
}

void memory_init(void)
{
	memory = malloc(0x04000000);
}

void * memory_acquire_block(uint64_t address, size_t size)
{
	return &memory[address];
}

void memory_synchronize_block(uint64_t address, size_t size, void * buffer)
{
}

void memory_release_block(uint64_t address, size_t size, void * buffer)
{
}

#else

#define PAGE_SIZE 0x10000 // must be a power of 2
#define PAGE_MASK ((uint64_t)PAGE_SIZE - 1)
typedef uint8_t page_t[PAGE_SIZE];
typedef page_t * page_table_t[0x10000];
typedef page_table_t * page_directory_t[0x10000];

page_directory_t * memory_contents[0x10000];

static page_t * _get_page(uint64_t address)
{
	uint16_t directory_index = address >> 48;
	page_directory_t * directory = memory_contents[directory_index];
	if(directory == NULL)
	{
		directory = memory_contents[directory_index] = malloc(sizeof(page_directory_t));
	}

	uint16_t table_index = (address >> 32) & 0xFFFF;
	page_table_t * table = (*directory)[table_index];
	if((*directory)[table_index] == NULL)
	{
		table = (*directory)[table_index] = malloc(sizeof(page_table_t));
	}

	uint16_t page_index = (address >> 16) & 0xFFFF;
	page_t * page = (*table)[page_index];
	if((*table)[page_index] == NULL)
	{
		page = (*table)[page_index] = malloc(sizeof(page_t));
	}

	return page;
}

static bool _memory_read(arm_state_t * cpu, uint64_t address, void * buffer, size_t size, bool privileged_mode)
{
	while(size > 0)
	{
		uint8_t * page = *_get_page(address);
		size_t count = PAGE_SIZE - (address & PAGE_MASK);
		if(size < count)
		{
			memcpy(buffer, &page[address & PAGE_MASK], size);
			break;
		}
		else
		{
			memcpy(buffer, &page[address & PAGE_MASK], count);
			buffer = (char *)buffer + count;
			size -= count;
			address += count;
		}
	}
	return true;
}

static bool _memory_write(arm_state_t * cpu, uint64_t address, const void * buffer, size_t size, bool privileged_mode)
{
	if(address < memory_changed_lowest)
		memory_changed_lowest = address;
	if(address + (size - 1) > memory_changed_highest)
		memory_changed_highest = address + (size - 1);

	while(size > 0)
	{
		uint8_t * page = *_get_page(address);
		size_t count = PAGE_SIZE - (address & PAGE_MASK);
		if(size < count)
		{
			memcpy(&page[address & PAGE_MASK], buffer, size);
			break;
		}
		else
		{
			memcpy(&page[address & PAGE_MASK], buffer, count);
			buffer = (const char *)buffer + count;
			size -= count;
			address += count;
		}
	}
	return true;
}

void memory_init(void)
{
}

void * memory_acquire_block(uint64_t address, size_t size)
{
	if(((address + size) & ~PAGE_MASK) == (address & ~PAGE_MASK))
	{
		return &(*_get_page(address))[address & PAGE_MASK];
	}
	else
	{
		void * buffer = malloc(size);
		_memory_read(NULL, address, buffer, size, false);
		return buffer;
	}
}

void memory_synchronize_block(uint64_t address, size_t size, void * buffer)
{
	if(((address + size) & ~PAGE_MASK) != (address & ~PAGE_MASK))
	{
		_memory_write(NULL, address, buffer, size, false);
	}
}

void memory_release_block(uint64_t address, size_t size, void * buffer)
{
	if(((address + size) & ~PAGE_MASK) != (address & ~PAGE_MASK))
	{
		free(buffer);
	}
}
#endif

void * memory_acquire_block_reversed(uint64_t address, size_t size)
{
	void * buffer = malloc(size);
	size_t offset = 0;

	if((address & 3) != 0)
	{
		for(int i = address & 3; i < 4; i++)
			_memory_read(NULL, (address + i) ^ 3, (uint8_t *)buffer + i, 1, false);
		offset = ~address & 3;
	}

	for(; offset < size; offset += 4)
	{
		uint32_t value;
		_memory_read(NULL, address + offset, &value, 4, false);
		value = bswap_32(value);
		memcpy((uint8_t *)buffer + offset, &value, 4);
	}

	if(offset != size)
	{
		offset -= 4;
		for(offset -= 4; offset < size; offset++)
			_memory_read(NULL, (address + offset) ^ 3, (uint8_t *)buffer + offset, 1, false);
	}

	return buffer;
}

void memory_synchronize_block_reversed(uint64_t address, size_t size, void * buffer)
{
	size_t offset = 0;

	if((address & 3) != 0)
	{
		for(int i = address & 3; i < 4; i++)
			_memory_write(NULL, (address + i) ^ 3, (uint8_t *)buffer + i, 1, false);
		offset = ~address & 3;
	}

	for(; offset < size; offset += 4)
	{
		uint32_t value;
		_memory_write(NULL, address + offset, &value, 4, false);
		value = bswap_32(value);
		memcpy((uint8_t *)buffer + offset, &value, 4);
	}

	if(offset != size)
	{
		offset -= 4;
		for(offset -= 4; offset < size; offset++)
			_memory_write(NULL, (address + offset) ^ 3, (uint8_t *)buffer + offset, 1, false);
	}
}

void memory_release_block_reversed(uint64_t address, size_t size, void * buffer)
{
	free(buffer);
}

// feature sets

#define ARMV1_DEFAULT_FEATURES ((1 << FEATURE_ARM26))
#define ARMV2_DEFAULT_FEATURES ARMV1_DEFAULT_FEATURES
#define ARMV3_DEFAULT_FEATURES (ARMV2_DEFAULT_FEATURES | (1 << FEATURE_SWP) | (1 << FEATURE_ARM32))
#define ARMV4_DEFAULT_FEATURES ((1 << FEATURE_SWP) | (1 << FEATURE_ARM32) | (1 << FEATURE_MULL))
#define ARMV5_DEFAULT_FEATURES ARMV4_DEFAULT_FEATURES
#define ARMV6_DEFAULT_FEATURES (ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) | (1 << FEATURE_JAZELLE))
#define ARMV7_DEFAULT_FEATURES (ARMV6_DEFAULT_FEATURES | (1 << FEATURE_THUMB2))
#define ARMV8_DEFAULT_FEATURES32 (ARMV7_DEFAULT_FEATURES | (1 << FEATURE_MULTIPROC) | (1 << FEATURE_SECURITY) | (1 << FEATURE_VIRTUALIZATION))
#define ARMV8_DEFAULT_FEATURES64 (ARMV7_DEFAULT_FEATURES | (1 << FEATURE_MULTIPROC) | (1 << FEATURE_SECURITY) | (1 << FEATURE_VIRTUALIZATION) | (1 << FEATURE_ARM64))

#define ARMV1_ISAS    (1 << ISA_AARCH26)
#define ARMV2_ISAS    ARMV1_ISAS
#define ARMV3_ISAS    (ARMV1_ISAS | (1 << ISA_AARCH32))
#define ARMV4_ISAS    (1 << ISA_AARCH32)
#define ARMV4T_ISAS   (ARMV4_ISAS | (1 << ISA_THUMB32))
#define ARMV5_ISAS    ARMV4_ISAS
#define ARMV5T_ISAS   ARMV4T_ISAS
#define ARMV5TEJ_ISAS (ARMV4T_ISAS | (1 << ISA_JAZELLE))
#define ARMV6_ISAS    ARMV5TEJ_ISAS
#define ARMV6_M_ISAS  (1 << ISA_THUMB32)
#define ARMV7_ISAS    (ARMV5TEJ_ISAS | (1 << ISA_THUMBEE))
#define ARMV7_M_ISAS  ARMV6_M_ISAS
#define ARMV8_ISAS    (ARMV5TEJ_ISAS | (1 << ISA_AARCH64))
#define ARMV8_M_ISAS  ARMV6_M_ISAS

static uint16_t arm_default_features[] =
{
	[ARMV1] = ARMV1_DEFAULT_FEATURES,
	[ARMV2] = ARMV2_DEFAULT_FEATURES,
	[ARMV3] = ARMV3_DEFAULT_FEATURES & ~(1 << FEATURE_ARM26),
	[ARMV4] = ARMV4_DEFAULT_FEATURES & ~(1 << FEATURE_MULL),
	[ARMV5] = ARMV5_DEFAULT_FEATURES & ~(1 << FEATURE_MULL),
	[ARMV6] = ARMV6_DEFAULT_FEATURES,
	[ARMV7] = ARMV7_DEFAULT_FEATURES,
	[ARMV8] = ARMV8_DEFAULT_FEATURES64,
};

static const struct
{
	const char * name;
	arm_version_t version;
	arm_part_number_t part_number;
	uint16_t supported_isas;
	uint32_t features;
} arm_names[] =
{
	{ "1",     ARMV1, ARM_PART_ARM1,        ARMV1_ISAS,     ARMV1_DEFAULT_FEATURES },
	{ "2",     ARMV2, ARM_PART_ARM2,        ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES },
	{ "2a",    ARMV2, ARM_PART_ARM3,        ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES | (1 << FEATURE_SWP) },
	{ "3",     ARMV3, ARM_PART_ARM600,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "3m",    ARMV3, ARM_PART_ARM600,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES | (1 << FEATURE_MULL) },
	{ "3g",    ARMV3, ARM_PART_ARM600,      ARMV4_ISAS,     ARMV3_DEFAULT_FEATURES & ~(1 << FEATURE_ARM26) },
	{ "4",     ARMV4, ARM_PART_ARM810,      ARMV4_ISAS,     ARMV4_DEFAULT_FEATURES },
	{ "4t",    ARMV4, ARM_PART_ARM720,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "4xm",   ARMV4, ARM_PART_ARM810,      ARMV4_ISAS,     ARMV4_DEFAULT_FEATURES & ~(1 << FEATURE_MULL) },
	{ "4txm",  ARMV4, ARM_PART_ARM720,      ARMV4T_ISAS,   (ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB)) & ~(1 << FEATURE_MULL) },
	{ "5",     ARMV5, ARM_PART_ARM946,      ARMV5_ISAS,     ARMV5_DEFAULT_FEATURES },
	{ "5t",    ARMV5, ARM_PART_ARM1022,     ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "5xm",   ARMV5, ARM_PART_ARM946,      ARMV5_ISAS,     ARMV5_DEFAULT_FEATURES & ~(1 << FEATURE_MULL) },
	{ "5txm",  ARMV5, ARM_PART_ARM1022,     ARMV5T_ISAS,   (ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB)) & ~(1 << FEATURE_MULL) },
	{ "5te",   ARMV5, ARM_PART_ARM1022,     ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) },
	{ "5texp", ARMV5, ARM_PART_ARM1022,     ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) },
	{ "5tej",  ARMV5, ARM_PART_ARM1026,     ARMV5TEJ_ISAS,  ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) | (1 << FEATURE_JAZELLE) },
	{ "6",     ARMV6, ARM_PART_ARM1136,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES },
	{ "6-m",   ARMV6, ARM_PART_CORTEX_M0,   ARMV6_M_ISAS,   ARMV6_DEFAULT_FEATURES | ARM_PROFILE_M },
	{ "6z",    ARMV6, ARM_PART_ARM1176,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_SECURITY) },
	{ "6k",    ARMV6, ARM_PART_ARM11MPCORE, ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_MULTIPROC) },
	{ "6kz",   ARMV6, ARM_PART_ARM11MPCORE, ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_MULTIPROC) | (1 << FEATURE_SECURITY) },
	{ "6t2",   ARMV6, ARM_PART_ARM1156,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_THUMB2) },
	{ "7",     ARMV7, ARM_PART_CORTEX_A17,  ARMV7_ISAS,     ARMV7_DEFAULT_FEATURES | ARM_PROFILE_A },
	{ "7-a",   ARMV7, ARM_PART_CORTEX_A17,  ARMV7_ISAS,     ARMV7_DEFAULT_FEATURES | ARM_PROFILE_A },
	{ "7-r",   ARMV7, ARM_PART_CORTEX_R4,   ARMV7_ISAS,     ARMV7_DEFAULT_FEATURES | ARM_PROFILE_R },
	{ "7-m",   ARMV7, ARM_PART_CORTEX_M3,   ARMV7_M_ISAS,   ARMV7_DEFAULT_FEATURES | ARM_PROFILE_M },
	{ "7ve",   ARMV7, ARM_PART_CORTEX_A17,  ARMV7_ISAS,     ARMV7_DEFAULT_FEATURES | ARM_PROFILE_A | (1 << FEATURE_VIRTUALIZATION) },
	{ "8",     ARMV8, ARM_PART_CORTEX_A32,  ARMV8_ISAS,     ARMV8_DEFAULT_FEATURES64 | ARM_PROFILE_A },
	{ "8-a",   ARMV8, ARM_PART_CORTEX_A32,  ARMV8_ISAS,     ARMV8_DEFAULT_FEATURES64 | ARM_PROFILE_A },
	{ "8-r",   ARMV8, ARM_PART_CORTEX_R52,  ARMV8_M_ISAS,   ARMV8_DEFAULT_FEATURES32 | ARM_PROFILE_R },
// TODO: R82 implements AArch64
	{ "8-r64", ARMV8, ARM_PART_CORTEX_R52,  ARMV8_ISAS,    (ARMV8_DEFAULT_FEATURES64 | ARM_PROFILE_R) & ~(1 << FEATURE_ARM32) },
	{ "8-m",   ARMV8, ARM_PART_CORTEX_M33,  ARMV7_ISAS,     ARMV8_DEFAULT_FEATURES32 | ARM_PROFILE_M },

// very rough approximation of specific chips
	{ "arm1",        ARMV1, ARM_PART_ARM1,        ARMV1_ISAS,     ARMV1_DEFAULT_FEATURES },
	{ "arm2",        ARMV2, ARM_PART_ARM2,        ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES },
	{ "arm250",      ARMV2, ARM_PART_ARM250,      ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES | (1 << FEATURE_SWP) },
	{ "arm2as",      ARMV2, ARM_PART_ARM250,      ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES | (1 << FEATURE_SWP) },
	{ "arm3",        ARMV2, ARM_PART_ARM3,        ARMV2_ISAS,     ARMV2_DEFAULT_FEATURES | (1 << FEATURE_SWP) },
//	{ "arm60",       ARMV3, ARM_PART_ARM60,       ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm600",      ARMV3, ARM_PART_ARM600,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm610",      ARMV3, ARM_PART_ARM610,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm6",        ARMV3, ARM_PART_ARM610,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm620",      ARMV3, ARM_PART_ARM620,      ARMV3_ISAS,     ARMV3_DEFAULT_FEATURES }, // mentioned in ARM documents
//	{ "arm700",      ARMV3, ARM_PART_ARM700,      ARMV4_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm710",      ARMV3, ARM_PART_ARM710,      ARMV4_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm710a",     ARMV3, ARM_PART_ARM710,      ARMV4_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm7",        ARMV3, ARM_PART_ARM710,      ARMV4_ISAS,     ARMV3_DEFAULT_FEATURES },
	{ "arm710t",     ARMV4, ARM_PART_ARM710,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm7tdmi",    ARMV4, ARM_PART_ARM710,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm720t",     ARMV4, ARM_PART_ARM720,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
//	{ "arm740t",     ARMV4, ARM_PART_ARM740,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm7ej",      ARMV5, ARM_PART_ARM720,      ARMV5TEJ_ISAS,  ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) | (1 << FEATURE_JAZELLE) }, // TODO: part number?
	{ "arm810",      ARMV4, ARM_PART_ARM810,      ARMV4_ISAS,     ARMV4_DEFAULT_FEATURES },
	{ "arm8",        ARMV4, ARM_PART_ARM810,      ARMV4_ISAS,     ARMV4_DEFAULT_FEATURES },
	{ "arm920t",     ARMV4, ARM_PART_ARM920,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm922t",     ARMV4, ARM_PART_ARM922,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm940t",     ARMV4, ARM_PART_ARM940,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm9tdmi",    ARMV4, ARM_PART_ARM940,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm9t",       ARMV4, ARM_PART_ARM940,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm9",        ARMV4, ARM_PART_ARM940,      ARMV4T_ISAS,    ARMV4_DEFAULT_FEATURES | (1 << FEATURE_THUMB) },
	{ "arm946e",     ARMV5, ARM_PART_ARM946,      ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) },
	{ "arm966e",     ARMV5, ARM_PART_ARM966,      ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) },
	{ "arm968e",     ARMV5, ARM_PART_ARM968,      ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) },
	{ "arm996hs",    ARMV5, ARM_PART_ARM996,      ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) },
	{ "arm926ej",    ARMV5, ARM_PART_ARM926,      ARMV5TEJ_ISAS,  ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) | (1 << FEATURE_JAZELLE) },
	{ "arm1020e",    ARMV5, ARM_PART_ARM1020,     ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_VFP) },
	{ "arm1022e",    ARMV5, ARM_PART_ARM1022,     ARMV5T_ISAS,    ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_VFP) },
	{ "arm1026ej",   ARMV5, ARM_PART_ARM1026,     ARMV5TEJ_ISAS,  ARMV5_DEFAULT_FEATURES | (1 << FEATURE_THUMB) | (1 << FEATURE_ENH_DSP) | (1 << FEATURE_DSP_PAIR) | (1 << FEATURE_JAZELLE) },
	{ "arm1136j",    ARMV6, ARM_PART_ARM1136,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES },
	{ "arm1156t2",   ARMV6, ARM_PART_ARM1156,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_THUMB2) },
	{ "arm1176jz",   ARMV6, ARM_PART_ARM1176,     ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_SECURITY) },
	{ "arm11mpcore", ARMV6, ARM_PART_ARM11MPCORE, ARMV6_ISAS,     ARMV6_DEFAULT_FEATURES | (1 << FEATURE_MULTIPROC) | (1 << FEATURE_SECURITY) },
};

static const struct
{
	const char * name;
	uint16_t supported_isas;
	uint32_t features;
	arm_fp_version_t min_fp; // also selects Advanced SIMD version
	arm_java_implementation_t min_java;
	uint32_t remove_features;
} arm_option_names[] = // TODO
{
	{ "nofp", 0, 0, .remove_features = (1 << FEATURE_FPA) | (1 << FEATURE_VFP) },
	{ "fp", 0, (1 << FEATURE_VFP) }, // select default fp
	{ "aka", 0, (1 << FEATURE_FPA) }, // TODO: signal AKA interface (as opposed to FPA)
	{ "fpa", 0, (1 << FEATURE_FPA) },
	//{ "fpsp", 0, 0 }, // TODO: single precision
	{ "dp", 0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG), .min_fp = ARM_VFPV1 },

	{ "vfpv1",     0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG), .min_fp = ARM_VFPV1 },
	{ "vfpv1d",    0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG), .min_fp = ARM_VFPV1 },
	{ "vfpv1xd",   0, (1 << FEATURE_VFP), .min_fp = ARM_VFPV1 },
	{ "vfpv2",     0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG), .min_fp = ARM_VFPV2 },
	{ "vfpv3",     0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG) | (1 << FEATURE_32_DREG), .min_fp = ARM_VFPV3 },
	{ "vfpv3-d16", 0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG), 0, .min_fp = ARM_VFPV3 },
	{ "vfpv3-d32", 0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG) | (1 << FEATURE_32_DREG), .min_fp = ARM_VFPV3 },
	{ "vfpv4",     0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG) | (1 << FEATURE_32_DREG) | (1 << FEATURE_FP16), .min_fp = ARM_VFPV4 },
	{ "vfpv4-d16", 0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG)                          | (1 << FEATURE_FP16), .min_fp = ARM_VFPV4 },
	{ "vfpv4-d32", 0, (1 << FEATURE_VFP) | (1 << FEATURE_DREG) | (1 << FEATURE_32_DREG) | (1 << FEATURE_FP16), .min_fp = ARM_VFPV4 },

	{ "fp16", 0, (1 << FEATURE_FP16), .min_fp = ARM_VFPV3  },
	{ "mp", 0, (1 << FEATURE_MULTIPROC) },
	{ "sec", 0, (1 << FEATURE_SECURITY) },
	{ "nosimd", 0, 0, .remove_features = (1 << FEATURE_SIMD) },
	{ "simd", 0, (1 << FEATURE_SIMD) }, // select default fp
	{ "simdv1", 0, (1 << FEATURE_SIMD), .min_fp = ARM_VFPV3 },
	{ "simdv2", 0, (1 << FEATURE_SIMD), .min_fp = ARM_VFPV4 },
	{ "crc", 0, 0 }, // TODO, minimum armv8
	{ "crypto", 0, 0 }, // TODO
	{ "sb", 0, 0 }, // TODO
	{ "predres", 0, 0 }, // TODO
	{ "fp16fml", 0, 0 }, // TODO
	{ "dotprod", 0, 0 }, // TODO
	{ "i8mm", 0, 0 }, // TODO
	{ "bf16", 0, 0 }, // TODO
	{ "idiv", 0, 0 }, // TODO
	{ "dsp", 0, 0 }, // TODO
	{ "mve", 0, 0 }, // TODO
	{ "pacbti", 0, 0 }, // TODO

	{ "nojava", 0, 0, .min_java = ARM_JAVA_NONE },
	{ "trivjava", 0, 0, .min_java = ARM_JAVA_TRIVIAL },
	{ "java", 0, 0, .min_java = ARM_JAVA_JAZELLE },
	{ "jvm", 0, 0, .min_java = ARM_JAVA_JVM },
	{ "picojava", 0, 0, .min_java = ARM_JAVA_PICOJAVA },
	{ "javaext", 0, 0, .min_java = ARM_JAVA_EXTENSION },
};

static const memory_interface_t _memory_interface =
{
	.read = _memory_read,
	.write = _memory_write,
};

void init_isa(arm_configuration_t * cfg, arm_instruction_set_t * isa, arm_syntax_t * syntax, thumb2_support_t thumb2, bool force32bit)
{
	if(*isa == ISA_UNKNOWN)
	{
		// attempt to settle on a default instruction set

		if(cfg->version == ARM_UNKNOWN)
		{
			// ARMv7 supports the most instruction sets (ARM, Thumb, Jazelle, ThumbEE)
			cfg->version = ARMV7;
		}

		// select the most appropriate ARM instruction set
		switch(cfg->version)
		{
		case ARMV1:
		case ARMV2:
			// these versions only support 26-bit
			*isa = ISA_AARCH26;
			break;
		case ARMV3:
		case ARMV4:
		case ARMV5:
		case ARMV6:
		case ARMV7:
			// these versions normally support only 32-bit, and ARMv3 normally runs in 32-bit as well
			*isa = ISA_AARCH32;
			break;
		case ARMV8:
		case ARMV81:
		case ARMV82:
		case ARMV83:
		case ARMV9:
			// unless 32-bit is specifically requested, use 64-bit
			*isa = force32bit ? ISA_AARCH32 : ISA_AARCH64;
			break;
		}
	}
	else switch(*isa)
	{
	case ISA_AARCH26:
		// ARMv1, ARMv2, ARMv3 support 26-bit mode
		if(cfg->version == ARM_UNKNOWN)
		{
			// pick the latest
			cfg->version = ARMV3;
		}
		else if(cfg->version > ARMV3)
		{
			fprintf(stderr, "ARM26 not supported from ARMv4 on\n");
			cfg->version = ARMV3;
		}

		cfg->features |= 1 << FEATURE_ARM26;
		break;

	case ISA_AARCH32:
		// all versions since ARMv3 support 32-bit ARM mode (except M profile processors)
		if(cfg->version == ARM_UNKNOWN)
		{
			// ARMv6 supports the most configurations (endianness, alignment), so we pick that one
			// unless the syntax is specifically UAL, then it should be at least ARMv7
			//cfg->version = *syntax != SYNTAX_UNIFIED ? ARMV6 : ARMV7;
			cfg->version = ARMV7;
		}
		else if(cfg->version < ARMV3)
		{
			fprintf(stderr, "ARM26 not supported before ARMv3\n");
			cfg->version = ARMV3;
		}

		cfg->features |= 1 << FEATURE_ARM32;
		break;

	case ISA_THUMB32:
		// ARMv4T, ARMv5T, ARMv6 and later support Thumb, ARMv6T2, ARMv7 and later support Thumb-2
		if(cfg->version == ARM_UNKNOWN)
		{
			// if Thumb mode is specifically set to pre-v6T2, pick ARMv7
			// otherwise, ARMv6 supports the most configurations (endianness, alignment), so we pick that one
			// unless the syntax is specifically UAL, then it should be at least ARMv7
			//cfg->version = thumb2 == THUMB2_EXCLUDED ? ARMV6 : *syntax != SYNTAX_UNIFIED ? ARMV6 : ARMV7;
			cfg->version = thumb2 == THUMB2_EXCLUDED ? ARMV6 : ARMV7;
		}
		else if(thumb2 == THUMB2_EXPECTED && cfg->version < ARMV6)
		{
			fprintf(stderr, "Thumb-2 not supported before ARMv6\n");
			cfg->version = ARMV6;
		}
		else if(thumb2 == THUMB2_EXCLUDED && cfg->version >= ARMV7)
		{
			fprintf(stderr, "Adding support for Thumb-2\n");
			thumb2 = THUMB2_EXPECTED;
		}
		else if(cfg->version < ARMV4)
		{
			fprintf(stderr, "Thumb not supported before ARMv4\n");
			cfg->version = ARMV4;
		}

		cfg->features |= 1 << FEATURE_THUMB;
		if(thumb2 == THUMB2_EXPECTED)
			cfg->features |= 1 << FEATURE_THUMB2;
		break;

	case ISA_JAZELLE:
		// Jazelle is supported in ARMv5TEJ, ARMv6 and ARMv7
		if(cfg->version == ARM_UNKNOWN)
		{
			// only the trivial implementation is provided when virtualization extensions are present
			// since the author is not sure if ARMv7 supports non-trivial execution, we select ARMv6
			cfg->version = ARMV6;
		}
		else if(cfg->version < ARMV5)
		{
			fprintf(stderr, "Jazelle not supported before ARMv5\n");
			cfg->version = ARMV5;
		}

		cfg->features |= 1 << FEATURE_JAZELLE;
		break;

	case ISA_THUMBEE:
		// only ARMv7 supports ThumbEE
		if(cfg->version == ARM_UNKNOWN)
		{
			cfg->version = ARMV7;
		}
		else if(cfg->version < ARMV7)
		{
			fprintf(stderr, "ThumbEE not supported before ARMv7\n");
			cfg->version = ARMV7;
		}
		else if(cfg->version >= ARMV8)
		{
			fprintf(stderr, "ThumbEE not supported after ARMv7\n");
			cfg->version = ARMV7;
		}
		break;

	case ISA_AARCH64:
		// ARMv8 and later have (optional) 64-bit support
		if(cfg->version == ARM_UNKNOWN)
		{
			cfg->version = ARMV8;
		}
		else if(cfg->version < ARMV8)
		{
			fprintf(stderr, "AArch64 not supported before ARMv8\n");
			cfg->version = ARMV8;
		}
		break;
	}

	if(*syntax == SYNTAX_UNKNOWN)
		*syntax = cfg->version < ARMV7 ? SYNTAX_DIVIDED : SYNTAX_UNIFIED;

	cfg->features |= arm_default_features[cfg->version];

	// selects default coprocessors
	if(cfg->fp_version == 0 && (cfg->features & ((1 << FEATURE_VFP) | 1 << FEATURE_SIMD)))
	{
		bool need_fp = cfg->features & (1 << FEATURE_VFP);
		bool need_simd = cfg->features & (1 << FEATURE_SIMD);

		switch(cfg->version)
		{
		case ARMV1:
			fprintf(stderr, "No %s support for selected architecture\n",
				need_fp ? need_simd ? "floating point and SIMD" : "floating point" : "SIMD");
			cfg->features &= ~((1 << FEATURE_VFP) | (1 << FEATURE_SIMD));
			break;
		case ARMV2:
		case ARMV3:
		case ARMV4: // FPA only through emulation
			if(need_simd)
			{
				fprintf(stderr, "No SIMD support for selected architecture\n");
				cfg->features &= ~(1 << FEATURE_SIMD);
			}
			if(need_fp)
			{
				// only FPA support
				cfg->features &= ~(1 << FEATURE_VFP);
				cfg->features |= (1 << FEATURE_FPA);
			}
			break;
		case ARMV5:
		case ARMV6:
			if(need_simd)
			{
				fprintf(stderr, "No SIMD support for selected architecture\n");
				cfg->features &= ~(1 << FEATURE_SIMD);
			}
			if(need_fp)
			{
				cfg->fp_version = ARM_VFPV2;
			}
			break;
		case ARMV7:
			// TODO: M-profile has MVE, not AdvSIMD
			if((cfg->features & FEATURE_PROFILE_MASK) == ARM_PROFILE_M)
				cfg->fp_version = ARM_VFPV5;
			else if(!(cfg->features & (1 << FEATURE_VIRTUALIZATION)))
				cfg->fp_version = ARM_VFPV3;
			else
				cfg->fp_version = ARM_VFPV4;
			break;
		case ARMV8:
		case ARMV81:
		case ARMV82:
		case ARMV83:
		case ARMV9:
			cfg->fp_version = ARM_V8FP;
			break;
		}
	}
}

void isa_display(arm_configuration_t config, arm_instruction_set_t isa, arm_syntax_t syntax, bool disasm, arm_endianness_t endian)
{
	printf("Instruction set: ");
	switch(isa)
	{
	default:
		printf("%s", arm_instruction_set_names[isa]);
		break;
	case ISA_THUMB32:
		if(!(config.features & (1 << FEATURE_THUMB2)))
			printf("Thumb");
		else
			printf("Thumb-2");
		break;
	}
	if(isa != ISA_JAZELLE)
	{
		printf(", version: %s", arm_version_names[config.version]);
		if((config.features & (1 << FEATURE_FPA)) != 0)
			printf(", FPA");
		else if((config.features & (1 << FEATURE_VFP)) != 0)
			printf(", %s", arm_fp_version_names[config.fp_version]);

		if((config.features & (1 << FEATURE_SIMD)) != 0)
			printf(", Advanced SIMD (Neon)");
		else if((config.features & (1 << FEATURE_MVE)) != 0)
			printf(", MVE (Helium)");

		if(disasm && (isa == ISA_AARCH32 || isa == ISA_THUMB32 || isa == ISA_THUMBEE))
		{
			printf(", syntax: ");
			switch(syntax)
			{
			case SYNTAX_UNKNOWN:
				assert(false);
			case SYNTAX_DIVIDED:
				printf("divided (pre-UAL)");
				break;
			case SYNTAX_UNIFIED:
				printf("unified (UAL)");
				break;
			}
		}

		switch(endian)
		{
		case ARM_ENDIAN_LITTLE:
			printf(", little endian (LE)");
			break;
		case ARM_ENDIAN_BIG:
			printf(", big endian, byte invariant (BE-8)");
			break;
		case ARM_ENDIAN_SWAPPED:
			printf(", big endian, word invariant (BE-32)");
			break;
		}
	}
	printf("\n");
}

int main(int argc, char * argv[], char * envp[])
{
	environment_t env[1];

	memset(env, 0, sizeof(environment_t));
	env->config.jazelle_implementation = ARM_JAVA_DEFAULT; // TODO

	env->memory_interface = &_memory_interface;

	env->isa = ISA_UNKNOWN;
	env->syntax = SYNTAX_UNKNOWN;
	env->endian = ARM_ENDIAN_DEFAULT;
	env->thumb2 = THUMB2_PERMITTED;

	arm_part_number_t part_number = 0;

	uint64_t load_address = -1;
	uint64_t start_offset = 0;
	env->entry = 0;
	bool run = false;
	bool disasm = false;
	int argi = 1;
	enum
	{
		RUN_MODE_BARE_CPU, // only set up the CPU and entry point
		RUN_MODE_MINIMAL, // set up CPU and system calls, but no initial stack
		RUN_MODE_LINUX, // emulate system calls and initial stack
	} run_mode = RUN_MODE_BARE_CPU;

	while(argi < argc)
	{
		if(argv[argi][0] == '-')
		{
			if(argv[argi][0] == '-' && argv[argi][1] == 'r')
			{
				run = true;
			}
			else if(argv[argi][0] == '-' && argv[argi][1] == 'l' && argv[argi][2] == '=')
			{
				load_address = strtoll(&argv[argi][3], NULL, 0);
			}
			else if(argv[argi][0] == '-' && argv[argi][1] == 'd')
			{
				disasm = true;
			}
			else if(argv[argi][0] == '-' && argv[argi][1] == 's' && argv[argi][2] == '=')
			{
				start_offset = strtoll(&argv[argi][3], NULL, 0);
			}
			else if(strcasecmp(argv[argi], "-u") == 0)
			{
				run_mode = RUN_MODE_MINIMAL;
			}
			else if(strcasecmp(argv[argi], "-le") == 0)
			{
				env->endian = ARM_ENDIAN_LITTLE;
			}
			else if(strcasecmp(argv[argi], "-be8") == 0)
			{
				env->endian = ARM_ENDIAN_BIG;
			}
			else if(strcasecmp(argv[argi], "-be32") == 0)
			{
				env->endian = ARM_ENDIAN_SWAPPED;
			}
			else if(strcasecmp(argv[argi], "-be") == 0)
			{
				env->endian = ARM_ENDIAN_BE_VERSION_SPECIFIC;
			}
			else if(strcasecmp(argv[argi], "-a26") == 0
			|| strcasecmp(argv[argi], "-arm26") == 0)
			{
				env->isa = ISA_AARCH26;
			}
			else if(strcasecmp(argv[argi], "-a32") == 0
			|| strcasecmp(argv[argi], "-arm32") == 0
			|| strcasecmp(argv[argi], "-aarch32") == 0)
			{
				env->isa = ISA_AARCH32;
			}
			else if(strcasecmp(argv[argi], "-t16") == 0
			|| strcasecmp(argv[argi], "-thumb") == 0
			|| strcasecmp(argv[argi], "-thumb32") == 0)
			{
				env->isa = ISA_THUMB32;
			}
			else if(strcasecmp(argv[argi], "-t32") == 0
			|| strcasecmp(argv[argi], "-thumb2") == 0)
			{
				env->isa = ISA_THUMB32;
				env->thumb2 = THUMB2_EXPECTED;
			}
			else if(strcasecmp(argv[argi], "-thumb1") == 0)
			{
				env->isa = ISA_THUMB32;
				env->thumb2 = THUMB2_EXCLUDED;
			}
			else if(strcasecmp(argv[argi], "-tee") == 0
			|| strcasecmp(argv[argi], "-e32") == 0
			|| strcasecmp(argv[argi], "-thumbee") == 0)
			{
				env->isa = ISA_THUMBEE;
			}
			else if(strcasecmp(argv[argi], "-j") == 0
			|| strcasecmp(argv[argi], "-j32") == 0
			|| strcasecmp(argv[argi], "-java") == 0
			|| strcasecmp(argv[argi], "-jazelle") == 0)
			{
				env->isa = ISA_JAZELLE;
			}
			else if(strcasecmp(argv[argi], "-a64") == 0
			|| strcasecmp(argv[argi], "-arm64") == 0
			|| strcasecmp(argv[argi], "-aarch64") == 0
			|| strcasecmp(argv[argi], "-armv8") == 0)
			{
				env->isa = ISA_AARCH64;
			}
			else if(strcasecmp(argv[argi], "-ual") == 0
			|| strcasecmp(argv[argi], "-new") == 0)
			{
				env->syntax = SYNTAX_UNIFIED;
			}
			else if(strcasecmp(argv[argi], "-old") == 0)
			{
				env->syntax = SYNTAX_DIVIDED;
			}
			else if(argv[argi][0] == '-' && argv[argi][1] == 'v')
			{
				bool found = false;
				char * plus = strchr(argv[argi], '+');
				if(plus)
					*plus = '\0';
				for(size_t index = 0; index < sizeof arm_names / sizeof arm_names[0]; index++)
				{
					if(strcasecmp(&argv[argi][2], arm_names[index].name) == 0)
					{
						env->config.version = arm_names[index].version;
						env->config.features = arm_names[index].features;
						env->supported_isas = arm_names[index].supported_isas;
						part_number = arm_names[index].part_number;
						found = true;
						break;
					}
				}
				if(!found)
				{
					fprintf(stderr, "Invalid option: %s\n", argv[argi]);
					exit(1);
				}

				while(plus)
				{
					char * arg = plus + 1;
					plus = strchr(arg, '+');
					found = false;
					if(plus)
						*plus = '\0';
					for(size_t index = 0; index < sizeof arm_option_names / sizeof arm_option_names[0]; index++)
					{
						if(strcasecmp(arg, arm_option_names[index].name) == 0)
						{
							env->config.features &= ~arm_option_names[index].remove_features;
							env->config.features |= arm_option_names[index].features;
							env->supported_isas |= arm_option_names[index].supported_isas;
							env->config.fp_version = MAX(env->config.fp_version, arm_option_names[index].min_fp);
							env->config.jazelle_implementation = MAX(env->config.jazelle_implementation, arm_option_names[index].min_java);
							found = true;
							break;
						}
					}
					if(!found)
					{
						fprintf(stderr, "Invalid option: %s\n", arg);
						exit(1);
					}
				}
			}
		}
		else
		{
			break;
		}
		argi++;
	}

	env->supported_isas |= 1 << env->isa;

	env->stack = 0;
	// Java specific
	env->cp_start = 0;
	env->loc_count = 0;
	env->clinit_entry = 0;
	env->clinit_loc_count = 0;

	FILE * input_file = fopen(argv[argi], "rb");
	if(input_file == NULL)
	{
		fprintf(stderr, "Fatal error: unable to open file %s, leaving\n", argv[argi]);
		exit(1);
	}

	char signature[4];
	fread(signature, 4, 1, input_file);

	// attempt to read ELF file
	if(load_address == (uint64_t)-1 && memcmp(signature, "\x7F" "ELF", 4) == 0)
	{
		// ELF file
		printf("Parsing as ELF file\n");

		run_mode = RUN_MODE_LINUX;

		if(!run && !disasm)
		{
			run = true;
		}

		if(run)
			memory_init();

		env->purpose = run ? PURPOSE_LOAD : PURPOSE_PARSE;

		read_elf_file(input_file, env);
	}
	else if(load_address == (uint64_t)-1 && memcmp(signature, "\xCA\xFE\xBA\xBE", 4) == 0)
	{
		// Java class file
		printf("Parsing as Java class file\n");

		run_mode = RUN_MODE_LINUX;

		if(!run && !disasm)
		{
			run = true;
		}

		if(run)
			memory_init();

		env->purpose = run ? PURPOSE_LOAD : PURPOSE_PARSE;

		read_class_file(input_file, env);
	}
	else
	{
		if(!run && !disasm)
		{
			if(load_address != (uint64_t)-1)
				run = true;
			else
				disasm = true;
		}

		if(run)
			memory_init();

		fseek(input_file, start_offset, SEEK_SET);
		env->entry = load_address;

		init_isa(&env->config, &env->isa, &env->syntax, env->thumb2, false);

		arm_parser_state_t dis[1];
		arm_disasm_init(dis, env->config, env->isa, env->syntax);

		if(env->endian == ARM_ENDIAN_BE_VERSION_SPECIFIC)
		{
			if(dis->config.version < ARMV6)
			{
				// before v6, only this mode was supported
				env->endian = ARM_ENDIAN_SWAPPED;
			}
			else
			{
				// v6 supports both, we will pick the latter option
				env->endian = ARM_ENDIAN_BIG;
			}
		}
		else if(env->endian == ARM_ENDIAN_DEFAULT)
		{
			env->endian = ARM_ENDIAN_LITTLE;
		}

		arm_disasm_set_file(dis, input_file, env->endian);

		if(disasm && !run)
			isa_display(dis->config, dis->isa, dis->syntax, disasm, env->endian);

		if(run)
		{
			for(uint64_t offset = 0; ; offset++)
			{
				int c = fgetc(input_file);
				if(c == -1)
					break;
				uint64_t address = load_address + offset;
				arm_memory_write8(env->memory_interface, address, c, env->endian == ARM_ENDIAN_SWAPPED ? ARM_ENDIAN_SWAPPED : ARM_ENDIAN_LITTLE);
			}
		}
		else
		{
			dis->pc = 0;
			while(!feof(input_file))
				parse(dis);
		}
	}

	fclose(input_file);

	if(run)
	{
		arm_state_t cpu[1];
		arm_emu_init(cpu, env->config, env->supported_isas, &_memory_interface);
		arm_set_isa(cpu, env->isa);
		cpu->part_number = part_number;
		cpu->vendor = ARM_VENDOR_ARM;

		isa_display(cpu->config, arm_get_current_instruction_set(cpu), env->syntax, disasm, env->endian);

		switch(env->endian)
		{
		case ARM_ENDIAN_LITTLE:
			break;
		case ARM_ENDIAN_BIG:
			// v6+
			cpu->pstate.e = 1;
			break;
		case ARM_ENDIAN_SWAPPED:
			// until v7
			cpu->sctlr_el1 |= SCTLR_B;
			break;
		}

		if(run_mode != RUN_MODE_BARE_CPU)
		{
			// array layout:
			/*
			struct
			{
				uint32_t size_in_dwords;
			actual start of array:
				uint32_t elements[1];
			};
			*/
			cpu->joscr = JOSCR_FLAT_ARRAY;
			cpu->jaolr = JAOLR_LENGTH_SUB | (4 << JAOLR_LENGTH_OFF_SHIFT) | (0 << JAOLR_ELEMENT_OFF_SHIFT) | (2 << JAOLR_LENSHIFT_SHIFT);
		}
		else
		{
			// array layout:
			/*
			struct
			{
				uint32_t size_in_dwords;
				uint64_t * elements;
			};
			*/
			cpu->joscr = 0;
			cpu->jaolr = (0 << JAOLR_LENGTH_OFF_SHIFT) | (9 << JAOLR_ELEMENT_OFF_SHIFT) | (3 << JAOLR_LENSHIFT_SHIFT);
		}

		switch(run_mode)
		{
		case RUN_MODE_BARE_CPU:
		case RUN_MODE_MINIMAL:
			cpu->r[PC] = env->entry;
			switch(arm_get_current_instruction_set(cpu))
			{
			case ISA_AARCH26:
			case ISA_AARCH32:
			case ISA_THUMB32:
			case ISA_THUMBEE:
			default:
				cpu->r[A32_SP] = env->stack;
				break;
			case ISA_JAZELLE:
				cpu->r[J32_TOS] = (cpu->r[J32_LOC] + env->loc_count + 3) & ~3;
				break;
			case ISA_AARCH64:
				cpu->r[A64_SP] = env->stack;
				break;
			}
			break;
		case RUN_MODE_LINUX:
			setup_initial_state(cpu, env, argc - argi, &argv[argi], envp);
			break;
		}

		arm_parser_state_t dis[1];
		arm_disasm_init(dis, env->config, env->isa, env->syntax);
		arm_disasm_set_cpu(dis, cpu);

		cpu->capture_breaks = run_mode != RUN_MODE_BARE_CPU;

		arm_debug_state_t debug_state[1];
		arm_get_debug_state(debug_state, cpu);
		memory_changed_lowest = -1;
		memory_changed_highest = 0;

		uint64_t loop_address = 0;
		for(;;)
		{
			if(disasm && cpu->r[PC] >= loop_address)
			{
				loop_address = 0;

				debug_state->memory_changed_lowest = memory_changed_lowest;
				debug_state->memory_changed_highest = memory_changed_highest;

				debug(stdout, cpu, debug_state);

				memory_changed_lowest = -1;
				memory_changed_highest = 0;

				uint64_t current_pc = cpu->r[PC];

				parse(dis);

				int c = getchar();
				switch(c)
				{
				case 'g':
					if((c = getchar()) == '\n')
					{
						loop_address = current_pc + 1;
					}
					else
					{
						ungetc(c, stdin);
						scanf("%"PRIX64, &loop_address);
						while((c = getchar()) != '\n')
							;
					}
					break;
				case '\n':
					break;
				}
			}
			step(cpu);
			switch(cpu->result)
			{
			case ARM_EMU_OK:
				break;
			case ARM_EMU_RESET:
				printf("RESET\n");
				exit(0);
			case ARM_EMU_SVC:
				switch(arm_get_current_instruction_set(cpu))
				{
				case ISA_JAZELLE:
					if(!j32_linux_syscall(cpu))
					{
						printf("%s\n", env->syntax == SYNTAX_DIVIDED ? "SWI" : "SVC");
						printf("Unknown Jazelle system call: %d\n", j32_peek_word(cpu, 0));
						exit(0);
					}
					break;
				case ISA_AARCH32:
				case ISA_THUMB32:
				case ISA_THUMBEE:
				default:
					{
						uint32_t swi_number;
						switch(arm_get_current_instruction_set(cpu))
						{
						case ISA_THUMB32:
						case ISA_THUMBEE:
							swi_number = arm_fetch16(cpu, cpu->r[PC] - 2) & 0x00FF;
							if(swi_number != 0 && swi_number != 1)
							{
								// only EABI allowed
								printf("%s #0x%04X\n", env->syntax == SYNTAX_DIVIDED ? "SWI" : "SVC", swi_number);
								exit(0);
							}
							break;
						default:
							swi_number = arm_fetch32(cpu, cpu->r[PC] - 4) & 0x00FFFFFF;
							break;
						}

						if(swi_number == 1 && arm_get_current_instruction_set(cpu) != ISA_THUMBEE)
						{
							// extension: switch modes between 26/32/64-bit modes
							uint32_t lr = cpu->r[PC];
							uint32_t pc = cpu->r[A32_LR];
							regnum_t lrnum = A32_LR;
							if(arm_get_current_instruction_set(cpu) == ISA_AARCH26)
							{
								// 00: A32, 01/11: T32, 10: A64

								lr &= ~3;

								if((pc & 1))
								{
									arm_set_isa(cpu, ISA_THUMB32);
									pc &= ~1;
								}
								else if((pc & 2))
								{
									arm_set_isa(cpu, ISA_AARCH64);
									pc &= ~3;
									lrnum = A64_LR;
								}
								else
								{
									arm_set_isa(cpu, ISA_AARCH32);
									pc &= ~3;
								}
							}
							else
							{
								// 00/01: A26, 10/11: A64

								if(arm_get_current_instruction_set(cpu) == ISA_AARCH32)
									lr &= ~3;
								else
									lr |= 1;

								if((pc & 2))
								{
									arm_set_isa(cpu, ISA_AARCH64);
									lrnum = A64_LR;
								}
								else
								{
									arm_set_isa(cpu, ISA_AARCH26);
								}

								pc = cpu->r[A32_LR] & ~3;
							}
							cpu->r[PC] = pc;
							cpu->r[lrnum] = lr;
						}
						else if(swi_number == 0)
						{
							// EABI
							if(!a32_linux_eabi_syscall(cpu))
							{
								printf("%s #0x%06X\n", env->syntax == SYNTAX_DIVIDED ? "SWI" : "SVC", swi_number);
								printf("Unknown EABI system call: %d\n", (uint32_t)cpu->r[7]);
								exit(0);
							}
						}
						else
						{
							// OABI
							if(!a32_linux_oabi_syscall(cpu, swi_number))
							{
								printf("%s #0x%06X\n", env->syntax == SYNTAX_DIVIDED ? "SWI" : "SVC", swi_number);
								if(swi_number >= A32_OABI_SYS_BASE)
									printf("Unknown OABI system call: %d\n", swi_number - A32_OABI_SYS_BASE);
								exit(0);
							}
						}
					}
					break;
				case ISA_AARCH64:
					{
						uint16_t swi_number = (arm_fetch32(cpu, cpu->r[PC] - 4) >> 5) & 0xFFFF;

						if(swi_number == 1)
						{
							// extension: switch modes between 26/32/64-bit modes
							// 00: A32, 01/11: T32, 10: A26
							uint32_t lr = cpu->r[PC];
							uint32_t pc = cpu->r[A64_LR];

							lr &= ~3;

							if((pc & 1))
							{
								arm_set_isa(cpu, ISA_THUMB32);
								pc &= ~1;
							}
							else if((pc & 2))
							{
								arm_set_isa(cpu, ISA_AARCH26);
								pc &= ~3;
							}
							else
							{
								arm_set_isa(cpu, ISA_AARCH32);
								pc &= ~3;
							}

							cpu->r[PC] = pc;
							cpu->r[A32_LR] = lr;
						}
						else if(swi_number != 0)
						{
							printf("SVC #0x%04X\n", swi_number);
							exit(0);
						}
						else if(!a64_linux_syscall(cpu))
						{
							printf("SVC #0\n");
							printf("Unknown 64-bit system call: %"PRId64"\n", cpu->r[8]);
							exit(0);
						}
					}
					break;
				}
				break;
			case ARM_EMU_UNDEFINED:
				switch(arm_get_current_instruction_set(cpu))
				{
				case ISA_JAZELLE:
					// this should never happen, Jazelle undefined instructions go through a handler table
					assert(false);
				case ISA_THUMB32:
				case ISA_THUMBEE:
					{
						uint16_t opcode = arm_fetch16(cpu, cpu->r[PC] - 2);
						printf("UNDEFINED 0x%04X\n", opcode);
					}
					break;
				case ISA_AARCH26:
				case ISA_AARCH32:
				case ISA_AARCH64:
					{
						uint32_t opcode = arm_fetch32(cpu, cpu->r[PC] - 4);
						printf("UNDEFINED 0x%08X\n", opcode);
					}
					break;
				}
				exit(0);
			case ARM_EMU_PREFETCH_ABORT:
				printf("PREFETCH ABORT\n");
				exit(0);
			case ARM_EMU_DATA_ABORT:
				printf("DATA ABORT\n");
				exit(0);
			case ARM_EMU_ADDRESS26:
				printf("ADDRESS (in 26-bit mode)\n");
				exit(0);
			case ARM_EMU_IRQ:
				printf("IRQ\n");
				exit(0);
			case ARM_EMU_FIQ:
				printf("FIQ\n");
				exit(0);
			case ARM_EMU_BREAKPOINT:
				printf("BREAKPOINT\n");
				exit(0);
			case ARM_EMU_UNALIGNED:
				printf("UNALIGNED\n");
				exit(0);
			case ARM_EMU_UNALIGNED_PC:
				printf("UNALIGNED PC\n");
				exit(0);
			case ARM_EMU_UNALIGNED_SP:
				printf("UNALIGNED SP\n");
				exit(0);
			case ARM_EMU_SERROR:
				printf("SERROR\n");
				exit(0);
			case ARM_EMU_SMC:
				printf("SMC\n");
				exit(0);
			case ARM_EMU_HVC:
				printf("HVC\n");
				exit(0);
			case ARM_EMU_SOFTWARE_STEP:
				printf("SOFTWARE STEP\n");
				exit(0);

			case ARM_EMU_JAZELLE_UNDEFINED:
				if(!j32_simulate_instruction(cpu, env->heap_start))
				{
					uint8_t opcode = arm_fetch8(cpu, cpu->r[PC] - 1);
					printf("UNDEFINED (Jazelle) %02X\n", opcode);
					exit(0);
				}
				break;
			case ARM_EMU_JAZELLE_NULLPTR:
				printf("NULL POINTER (Jazelle)\n");
				exit(0);
			case ARM_EMU_JAZELLE_OUT_OF_BOUNDS:
				printf("OUT OF BOUNDS (Jazelle)\n");
				exit(0);
			case ARM_EMU_JAZELLE_DISABLED:
				printf("JAZELLE DISABLED\n");
				exit(0);
			case ARM_EMU_JAZELLE_INVALID:
				printf("JAZELLE INVALID\n");
				exit(0);
			case ARM_EMU_JAZELLE_PREFETCH_ABORT:
				printf("PREFETCH ABORT (Jazelle)\n");
				exit(0);

			case ARM_EMU_THUMBEE_OUT_OF_BOUNDS:
				printf("OUT OF BOUNDS (ThumbEE)\n");
				exit(0);
			case ARM_EMU_THUMBEE_NULLPTR:
				printf("NULL POINTER (ThumbEE)\n");
				exit(0);
			}
		}
	}
	return 0;
}

