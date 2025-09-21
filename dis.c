
/* Instruction parsing and disassembling */

#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dis.h"

uint8_t fread8(FILE * file)
{
	return fgetc(file);
}

uint16_t fread16le(FILE * file)
{
	uint16_t value;
	fread(&value, 2, 1, file);
	return le16toh(value);
}

uint16_t fread16be(FILE * file)
{
	uint16_t value;
	fread(&value, 2, 1, file);
	return be16toh(value);
}

uint32_t fread32le(FILE * file)
{
	uint32_t value;
	fread(&value, 4, 1, file);
	return le32toh(value);
}

uint32_t fread32be(FILE * file)
{
	uint32_t value;
	fread(&value, 4, 1, file);
	return be32toh(value);
}

uint64_t fread64le(FILE * file)
{
	uint64_t value;
	fread(&value, 8, 1, file);
	return le64toh(value);
}

uint64_t fread64be(FILE * file)
{
	uint64_t value;
	fread(&value, 8, 1, file);
	return be64toh(value);
}

uint8_t file_fetch8(arm_parser_state_t * dis)
{
	dis->pc += 1;

	if(dis->is_running)
		return arm_fetch_next8(dis->current_cpu) & 0xFF;

	return fread8(dis->input_file);
}

uint16_t file_fetch16(arm_parser_state_t * dis)
{
	dis->pc += 2;

	if(dis->is_running)
		return arm_fetch_next16(dis->current_cpu);

	return dis->bigendian ? fread16be(dis->input_file) : fread16le(dis->input_file);
}

uint16_t file_fetch16be(arm_parser_state_t * dis)
{
	dis->pc += 2;

	if(dis->is_running)
		return arm_fetch_next16be(dis->current_cpu);

	return fread16be(dis->input_file);
}

uint32_t file_fetch32(arm_parser_state_t * dis)
{
	dis->pc += 4;

	if(dis->is_running)
	{
		return arm_fetch_next32(dis->current_cpu);
	}

	return dis->bigendian ? fread32be(dis->input_file) : fread32le(dis->input_file);
}

uint32_t file_fetch32be(arm_parser_state_t * dis)
{
	dis->pc += 4;

	if(dis->is_running)
		return arm_fetch_next32be(dis->current_cpu);

	return fread32be(dis->input_file);
}

size_t file_fetch_align32(arm_parser_state_t * dis, char * buffer)
{
	size_t count = 0;
	while((dis->pc & 3) != 0)
	{
		uint8_t value = file_fetch8(dis);
		if(buffer != NULL)
			buffer[count] = value;
		count ++;
	}
	return count;
}

static const char * const a32_condition[16] = { "eq", "ne", "cs"/*"hs"*/, "cc"/*"lo"*/, "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", ""/*"al"*/, "nv" };
static const char * const a64_condition[16] = { "eq", "ne", "cs"/*"hs"*/, "cc"/*"lo"*/, "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", ""/*"al"*/, "" };

static inline void a32_print_register_list(uint32_t list)
{
	int range_start = -1;
	int range_end = -1;
	printf("{");
	for(int i = 0; i < 16; i++)
	{
		if(((list >> i) & 1))
		{
			if(range_start != -1)
			{
				if(range_end == i - 1)
				{
					range_end = i;
				}
				else
				{
					if(range_start != range_end)
						printf("-r%d", range_end);
					printf(",r%d", i);
					range_start = range_end = i;
				}
			}
			else
			{
				printf("r%d", i);
				range_start = range_end = i;
			}
		}
	}
	if(range_start != -1 && range_start != range_end)
	{
		printf("-r%d", range_end);
	}
	printf("}");
}

static inline void a32_print_fpregister_list(uint8_t start, uint8_t count, bool isfp64)
{
	if(!isfp64)
	{
		start = ((start << 1) | ((start >> 4) & 1)) & 0x1F;
	}

	if(count == 1)
		printf("{%c%d}", isfp64 ? 'd' : 's', start);
	else
		printf("{%c%d-%c%d}", isfp64 ? 'd' : 's', start, isfp64 ? 'd' : 's', start + count - 1);
}

//static inline void a32_print_simd_register_list(uint32_t list)
//{
//}

static inline void a64_print_register_operand(regnum_t number, bool_suppress_sp_t suppress_sp, bool is64bit)
{
	number &= 0x1F;
	if(number == A64_SP)
	{
		if(suppress_sp)
		{
			printf("%czr", is64bit ? 'x' : 'w');
		}
		else if(is64bit)
		{
			printf("sp");
		}
		else
		{
			printf("wsp");
		}
	}
	else
	{
		printf("%c%d", is64bit ? 'x' : 'w', number);
	}
}

static inline void a64_print_extension(uint8_t operation, uint8_t amount, bool is64bit)
{
	switch(operation)
	{
	case 0b000:
		printf(", uxtb");
		break;
	case 0b001:
		printf(", uxth");
		break;
	case 0b010:
		if(!is64bit && amount != 0)
		{
			printf(", lsl #%d", amount);
		}
		else
		{
			printf(", uxtw");
		}
		break;
	case 0b011:
		if(is64bit && amount != 0)
		{
			printf(", lsl #%d", amount);
		}
		else
		{
			printf(", uxtx");
		}
		break;
	case 0b100:
		printf(", sxtb");
		break;
	case 0b101:
		printf(", sxth");
		break;
	case 0b110:
		printf(", sxtw");
		break;
	case 0b111:
		printf(", sxtx");
		break;
	}
}

static inline void a32_print_operand_shift(uint32_t opcode)
{
	if((opcode & 0xFF0) == 0x060)
		printf(", rrx ");
	else if((opcode & 0xFF0) != 0x000)
	{
		switch((opcode >> 5) & 3)
		{
		case 0b00:
			printf(", lsl ");
			break;
		case 0b01:
			printf(", lsr ");
			break;
		case 0b10:
			printf(", asr ");
			break;
		case 0b11:
			printf(", ror ");
			break;
		}
		if((opcode & 0x010) == 0)
		{
			printf("#%d", (((opcode >> 7) - 1) & 0x1F) + 1);
		}
		else
		{
			printf("r%d", ((opcode >> 8) & 0xF));
		}
	}
}

static inline void t32_print_operand_shift(uint16_t opcode2)
{
	if((opcode2 & 0x70F0) == 0x0030)
		printf(", rrx ");
	else if((opcode2 & 0x70F0) != 0x0000)
	{
		switch((opcode2 >> 4) & 3)
		{
		case 0b00:
			printf(", lsl ");
			break;
		case 0b01:
			printf(", lsr ");
			break;
		case 0b10:
			printf(", asr ");
			break;
		case 0b11:
			printf(", ror ");
			break;
		}
		uint8_t value = (((opcode2 & 0x7000) >> 10) | ((opcode2 & 0x00C0) >> 6));
		printf("#%d", ((value - 1) & 0x1F) + 1);
	}
}

static inline void a32_print_fpa_operand(uint32_t opcode)
{
	if((opcode & 0x00000008))
	{
		printf("#%Lg", fpa_operands[opcode & 7]);
	}
	else
	{
		printf("f%d", opcode & 7);
	}
}

static inline void a32_print_ldc_stc_mem_operand(uint32_t opcode)
{
	bool preindex = opcode & 0x01000000;
	bool add = opcode & 0x00800000;
	bool writeback = opcode & 0x00200000;
	printf("[r%d", (opcode >> 16) & 0xF);

	if(preindex)
		printf(", ");
	else
		printf("], ");

	if(!preindex && !writeback)
	{
		printf("%d", opcode & 0x000000FF);
	}
	else
	{
		printf("#%s%08X", add ? "" : "-", (opcode & 0x000000FF) << 2);
	}

	if(!preindex)
	{
		printf("]");
		if(writeback)
			printf("!");
	}
}

static const char * const a32_banked_register_names[32] =
{
	"r8_usr",  "r9_usr",  "r10_usr", "r11_usr", "r12_usr", "r13_usr", "r14_usr", "?",
	"r8_fiq",  "r9_fiq",  "r10_fiq", "r11_fiq", "r12_fiq", "r13_fiq", "r14_fiq", "?",
	"r14_irq", "r13_irq", "r14_svc", "r13_svc", "r14_abt", "r13_abt", "r14_und", "r13_und",
	"r14_mon", "r13_mon", "r14_mon", "r13_mon", "elr_hyp", "r13_hyp", "elr_hyp", "r13_hyp",
};

static const char * const a32_banked_spsr_names[32] =
{
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "spsr_fiq", "?",
	"spsr_irq", "?", "spsr_svc", "?", "spsr_abt", "?", "spsr_und", "?",
	"?", "?", "?", "?", "spsr_mon", "?", "spsr_hyp", "?",
};

static const char * const a64_pstate_field_names[64] =
{
	"?", "?", "?", "uao", "pan", "spsel", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "daifset", "daifclr",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
	"?", "?", "?", "?", "?", "?", "?", "?",
};

static const char * const simd_operand_type[32] =
{
	"i32", "i32", "i32", "i32", "i32", "i32", "i32", "i32",
	"i16", "i16", "i16", "i16", "i32", "i32", "i8",  "f32",

	"i32", "i32", "i32", "i32", "i32", "i32", "i32", "i32",
	"i16", "i16", "i16", "i16", "i32", "i32", "i64", "",
};

static const char * const a64_vector_suffix[] = { "8b", "4h", "2s", "", "16b", "8h", "4s", "2d" };

static inline int it_get_condition(arm_parser_state_t * dis)
{
	if(dis->t32.it_block_count == 0)
		return COND_ALWAYS;
	else
		return dis->t32.it_block_condition ^ ((dis->t32.it_block_mask >> 3) & 1);
}

// convenience function
void parser_set_it_condition(arm_parser_state_t * dis, uint8_t itstate)
{
	dis->t32.it_block_condition = (itstate & 0xF0) >> 4;
	if((itstate & 0x01))
		dis->t32.it_block_count = 5;
	else if((itstate & 0x02))
		dis->t32.it_block_count = 4;
	else if((itstate & 0x04))
		dis->t32.it_block_count = 3;
	else if((itstate & 0x08))
		dis->t32.it_block_count = 2;
	dis->t32.it_block_mask = (itstate & 0x0E) >> 1;
}

void a32_parse(arm_parser_state_t * dis, bool is_arm26);
void a64_parse(arm_parser_state_t * dis);
void t32_parse(arm_parser_state_t * dis, bool is_thumbee);
void j32_parse(arm_parser_state_t * dis);

#include "parse.gen.c"

static inline void arm_disasm_clear(arm_parser_state_t * dis)
{
	memset(dis, 0, sizeof(arm_parser_state_t));

	dis->t32.it_block_condition = COND_ALWAYS;
	dis->t32.it_block_mask = 0;
	dis->t32.it_block_count = 0;
}

void arm_disasm_init(arm_parser_state_t * dis, arm_configuration_t config, arm_instruction_set_t isa, arm_syntax_t syntax)
{
	arm_disasm_clear(dis);
	dis->config = config;
	dis->isa = isa;
	dis->syntax = syntax;
	switch(dis->config.jazelle_implementation)
	{
	case ARM_JAVA_NONE:
		break;
	case ARM_JAVA_TRIVIAL:
		dis->config.jazelle_implementation = ARM_JAVA_NONE;
		break;
	case ARM_JAVA_JAZELLE:
	case ARM_JAVA_DEFAULT:
		dis->config.jazelle_implementation = ARM_JAVA_JVM;
		break;
	case ARM_JAVA_JVM:
	case ARM_JAVA_PICOJAVA:
	case ARM_JAVA_EXTENSION:
		break;
	}
}

void arm_disasm_set_file(arm_parser_state_t * dis, FILE * input_file, arm_endianness_t endian)
{
	dis->is_running = false;
	dis->input_file = input_file;
	dis->bigendian = endian != ARM_ENDIAN_LITTLE; // instructions are otherwise always in big endian order
}

void arm_disasm_set_cpu(arm_parser_state_t * dis, arm_state_t * cpu)
{
	dis->is_running = true;
	dis->current_cpu = cpu;
}

#include "emu.h" // needed to access the CPU state

void parse(arm_parser_state_t * dis)
{
	uint64_t current_pc = 0;
	if(dis->is_running)
	{
		dis->isa = arm_get_current_instruction_set(dis->current_cpu);
		current_pc = dis->current_cpu->r[PC];
		dis->pc = current_pc;
		parser_set_it_condition(dis, dis->current_cpu->pstate.it);
		dis->input_null_count = 0; // do not elide 0 opcodes
	}

	if(dis->isa == ISA_UNKNOWN)
	{
		uint64_t old_pc = dis->pc;
		uint8_t opcode = file_fetch8(dis);
		if(opcode == 0)
		{
			switch(dis->input_null_count++)
			{
			case 0:
				break;
			case 1:
				printf("...\n");
				goto finish;
			default:
				goto finish;
			}
		}
		else
		{
			dis->input_null_count = 0;
		}

		printf("[%08"PRIX64"]\t", old_pc);
		printf("<%02X>\n", opcode);
	}
	else switch(dis->isa)
	{
	case ISA_AARCH26:
		a32_parse(dis, true);
		break;
	case ISA_AARCH32:
		a32_parse(dis, false);
		break;
	case ISA_THUMB32:
		t32_parse(dis, false);
		break;
	case ISA_JAZELLE:
		j32_parse(dis);
		break;
	case ISA_THUMBEE:
		t32_parse(dis, true);
		break;
	case ISA_AARCH64:
		a64_parse(dis);
		break;
	}

finish:
	if(dis->is_running)
	{
		dis->current_cpu->r[PC] = current_pc;
	}
}

