#ifndef _MAIN_H
#define _MAIN_H

/* Common functionality required for the emulator/disassembler */

#include <stdint.h>
#include <stdio.h>
#include "arm.h"

typedef enum read_purpose_t
{
	PURPOSE_PARSE,
	PURPOSE_LOAD,
} read_purpose_t;

typedef enum thumb2_support_t
{
	THUMB2_PERMITTED = -1,
	THUMB2_EXCLUDED = 0,
	THUMB2_EXPECTED = 1,
} thumb2_support_t;

typedef struct environment_t
{
	// input parameters
	read_purpose_t purpose;
	const memory_interface_t * memory_interface;
	thumb2_support_t thumb2;

	// input/output parameters
	arm_configuration_t config;
	uint16_t supported_isas;
	arm_instruction_set_t isa;
	arm_syntax_t syntax;
	arm_endianness_t endian;

	// output parameters
	uint64_t entry;
	uint64_t stack;

	// output parameters (Jazelle only)
	uint32_t cp_start;
	uint32_t loc_count;
	uint32_t clinit_entry;
	uint32_t clinit_loc_count;
	uint32_t heap_start;
} environment_t;

#define ARM_ENDIAN_DEFAULT ((arm_endianness_t)-1)
#define ARM_ENDIAN_BE_VERSION_SPECIFIC ((arm_endianness_t)-2)

extern uint16_t fread16(FILE * file);
extern uint32_t fread32(FILE * file);
extern uint64_t fread64(FILE * file);
extern uint64_t freadword(FILE * file);

extern void * memory_acquire_block(uint64_t address, size_t size);
extern void memory_synchronize_block(uint64_t address, size_t size, void * buffer);
extern void memory_release_block(uint64_t address, size_t size, void * buffer);

extern void * memory_acquire_block_reversed(uint64_t address, size_t size);
extern void memory_synchronize_block_reversed(uint64_t address, size_t size, void * buffer);
extern void memory_release_block_reversed(uint64_t address, size_t size, void * buffer);

extern void init_isa(arm_configuration_t * cfg, arm_instruction_set_t * isa, arm_syntax_t * syntax, thumb2_support_t thumb2, bool force32bit);
extern void isa_display(arm_configuration_t config, arm_instruction_set_t isa, arm_syntax_t syntax, bool disasm, arm_endianness_t endian);

#endif // _MAIN_H
