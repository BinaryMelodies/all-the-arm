#ifndef DIS_H
#define DIS_H

/* Functionality relevant for the disassembler */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "arm.h"

typedef enum j32_parse_state_mode_t
{
	J32_PARSE_INS, // parsing single instructions
	J32_PARSE_LINE, // jump targets, as part of a tableswitch instruction
	J32_PARSE_PAIR, // pairs of values and jump targets, as part of a lookupswitch instruction
} j32_parse_state_mode_t;

typedef struct arm_parser_state_t
{
	arm_configuration_t config;
	bool bigendian;
	arm_instruction_set_t isa;
	arm_syntax_t syntax;

	bool is_running;
	union
	{
		arm_state_t * current_cpu;
		FILE * input_file;
	};
	uint64_t pc;

	struct
	{
		int it_block_condition;
		int it_block_mask;
		int it_block_count;
		uint16_t jump_instruction;
	} t32;

	struct
	{
		bool wide;
		uint32_t old_pc;
		j32_parse_state_mode_t parse_state;
		int32_t parse_state_count;
	} j32;

	int input_null_count;
} arm_parser_state_t;

void arm_disasm_init(arm_parser_state_t * dis, arm_configuration_t config, arm_instruction_set_t isa, arm_syntax_t syntax);
void parse(arm_parser_state_t * dis);
void arm_disasm_set_file(arm_parser_state_t * dis, FILE * file, arm_endianness_t endian);
void arm_disasm_set_cpu(arm_parser_state_t * dis, arm_state_t * cpu);
void parser_set_it_condition(arm_parser_state_t * dis, uint8_t itstate);

uint8_t fread8(FILE * file);
uint16_t fread16le(FILE * file);
uint16_t fread16be(FILE * file);
uint32_t fread32le(FILE * file);
uint32_t fread32be(FILE * file);
uint64_t fread64le(FILE * file);
uint64_t fread64be(FILE * file);

#endif // DIS_H
