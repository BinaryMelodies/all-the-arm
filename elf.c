
/* Handling ELF binaries and emulating a very simple Linux environment */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "elf.h"
#include "emu.h"
#include "dis.h"
#include "jazelle.h"
#include "jvm.h"

enum ei_class_t ei_class;
enum ei_data_t ei_data;

uint16_t fread16(FILE * file)
{
	switch(ei_data)
	{
	case ELFDATA2LSB:
		return fread16le(file);
	case ELFDATA2MSB:
		return fread16be(file);
	default:
		assert(false);
	}
}

uint32_t fread32(FILE * file)
{
	switch(ei_data)
	{
	case ELFDATA2LSB:
		return fread32le(file);
	case ELFDATA2MSB:
		return fread32be(file);
	default:
		assert(false);
	}
}

uint64_t fread64(FILE * file)
{
	switch(ei_data)
	{
	case ELFDATA2LSB:
		return fread64le(file);
	case ELFDATA2MSB:
		return fread64be(file);
	default:
		assert(false);
	}
}

uint64_t freadword(FILE * file)
{
	switch(ei_class)
	{
	case ELFCLASS32:
		return fread32(file);
	case ELFCLASS64:
		return fread64(file);
	default:
		assert(false);
	}
}

uint64_t build_initial_stack(arm_state_t * cpu, uint64_t stack, int argc, char ** argv, char ** envp)
{
	bool is_jazelle = arm_get_current_instruction_set(cpu) == ISA_JAZELLE;

	size_t envp_count;
	size_t envp_content_size = 0;
	for(envp_count = 0; envp[envp_count] != NULL; envp_count++)
	{
		envp_content_size += strlen(envp[envp_count]) + 1;
		if(is_jazelle)
			envp_content_size = (envp_content_size + 4 + 3) & ~3; // add length bytes and align to 4-byte boundary
	}

	size_t argv_content_size = 0;
	for(size_t i = 0; i < argc; i++)
	{
		argv_content_size += strlen(argv[i]) + 1;
		if(is_jazelle)
			argv_content_size = (argv_content_size + 4 + 3) & ~3; // add length bytes and align to 4-byte boundary
	}

	uint64_t string_offset;
	uint64_t argc_address;
	uint64_t argv_contents;
	uint64_t envp_contents;
	size_t ptr_shift;

	// stack layout:
	// argc argv[0] ... argv[argc - 1] NULL envp[0] ... envp[envp_count - 1] *argv[0] ... *envp[0]
	// Jazelle:
	// strlen(argv[0]) *argv[0] ... strlen(envp[0]) *envp[0] ... argc argv[0] ... argv[argc - 1] envp_count+1 envp[0] ... envp[envp_count - 1] NULL argv envp

	switch(arm_get_current_instruction_set(cpu))
	{
	default:
		ptr_shift = 2;

		stack -= envp_content_size + argv_content_size;
		string_offset = stack;
		stack -= 4 * (3 + argc + envp_count);
		stack &= ~3;

		argc_address = stack;
		argv_contents = stack + 4;
		envp_contents = argv_contents + argc * 4 + 4;
		break;
	case ISA_JAZELLE:
		ptr_shift = 2;

		string_offset = (stack + 3) & ~3;
		argc_address = string_offset + argv_content_size + envp_content_size;
		argv_contents = argc_address + 4;
		envp_contents = argv_contents + 4 * argc + 4;
		arm_memory_write32_data(cpu, envp_contents - 4, envp_count + 1);
		stack = envp_contents + 4 * envp_count + 4;
		arm_memory_write32_data(cpu, stack - 4, 0); // last envp entry is null
		arm_memory_write32_data(cpu, stack,     argv_contents);
		arm_memory_write32_data(cpu, stack + 4, envp_contents);
		break;
	case ISA_AARCH64:
		ptr_shift = 3;

		stack -= envp_content_size + argv_content_size;
		string_offset = stack;
		stack -= 8 * (3 + argc + envp_count);
		stack &= ~0xF;

		argc_address = stack;
		argv_contents = stack + 8;
		envp_contents = argv_contents + argc * 8 + 8;
		break;
	}

	if(ptr_shift == 2)
		arm_memory_write32_data(cpu, argc_address, argc);
	else
		arm_memory_write64_data(cpu, argc_address, argc);

	for(size_t i = 0; i < argc; i++)
	{
		if(is_jazelle)
		{
			// first store string length, and adjust string store so that it points to the array contents
			arm_memory_write32_data(cpu, string_offset, strlen(argv[i]));
			string_offset += 4;
		}

		if(ptr_shift == 2)
			arm_memory_write32_data(cpu, argv_contents + (i << ptr_shift), string_offset);
		else
			arm_memory_write64_data(cpu, argv_contents + (i << ptr_shift), string_offset);

		size_t j = 0;
		do
		{
			arm_memory_write8_data(cpu, string_offset + j, argv[i][j]);
		} while(argv[i][j++] != '\0');

		string_offset += j;
		if(is_jazelle)
		{
			// align string store to 4-byte boundary
			string_offset = (string_offset + 3) & ~3;
		}
	}

	for(size_t i = 0; i < envp_count; i++)
	{
		if(is_jazelle)
		{
			// first store string length, and adjust string store so that it points to the array contents
			arm_memory_write32_data(cpu, string_offset, strlen(envp[i]));
			string_offset += 4;
		}

		if(ptr_shift == 2)
			arm_memory_write32_data(cpu, envp_contents + (i << ptr_shift), string_offset);
		else
			arm_memory_write64_data(cpu, envp_contents + (i << ptr_shift), string_offset);

		size_t j = 0;
		do
		{
			arm_memory_write8_data(cpu, string_offset + j, envp[i][j]);
		} while(envp[i][j++] != '\0');
		string_offset += j;
	}

	if(ptr_shift == 2)
		arm_memory_write32_data(cpu, envp_contents + (envp_count << ptr_shift), 0);
	else
		arm_memory_write64_data(cpu, envp_contents + (envp_count << ptr_shift), 0);

	return stack;
}

void setup_initial_state(arm_state_t * cpu, environment_t * env, int argc, char ** argv, char ** envp)
{
	env->stack = build_initial_stack(cpu, env->stack, argc, argv, envp);
	env->heap_start = (env->stack + 0x10000 + 3) & ~3; // TODO: a better memory handling rather than a fixed sized 16KiB stack

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
		cpu->r[J32_LOC] = (env->stack + 3) & ~3;
		cpu->r[J32_TOS] = (cpu->r[J32_LOC] + env->loc_count + 3) & ~3;
		cpu->r[J32_CP] = env->cp_start;

		cpu->r[J32_LINK] = 0;
		cpu->r[J32_HEAP] = env->heap_start;
		if(env->clinit_entry != 0)
		{
			j32_invoke(cpu, 0, env->clinit_loc_count, env->clinit_entry);
		}
		j32_update_locals(cpu);
		break;
	case ISA_AARCH64:
		cpu->r[A64_SP] = env->stack;
		break;
	}
}

void read_elf_file(FILE * input_file, environment_t * env)
{
	ei_class = fgetc(input_file);

	switch(ei_class)
	{
	case ELFCLASS32:
		// 32-bit
		break;
	case ELFCLASS64:
		// 64-bit (AArch64 only)
		break;
	default:
		fprintf(stderr, "Invalid ELF class\n");
		exit(1);
	}

	ei_data = fgetc(input_file);

	switch(ei_data)
	{
	case ELFDATA2LSB:
		// little endian
		break;
	case ELFDATA2MSB:
		// big endian
		break;
	default:
		fprintf(stderr, "Invalid ELF data format\n");
		exit(1);
	}

	if(fgetc(input_file) != EV_CURRENT)
	{
		fprintf(stderr, "Invalid ELF header version\n");
		exit(1);
	}

	fseek(input_file, 0x10L, SEEK_SET);

	uint16_t type = fread16(input_file);

	if(type != 2)
	{
		fprintf(stderr, "Not executable\n");
		exit(1);
	}

	enum e_machine_t e_machine = fread16(input_file);
	bool force32bit = false;

	switch(e_machine)
	{
	case EM_ARM:
		// 32-bit ARM
		break;
	case EM_AARCH64:
		// 64-bit AArch64
		break;
	case EM_PJ:
	case EM_OLD_PICOJAVA:
		// picoJava
		env->config.jazelle_implementation = ARM_JAVA_EXTENSION;
		break;
	default:
		fprintf(stderr, "Invalid machine type\n");
		exit(1);
	}

	if(fread32(input_file) != EV_CURRENT)
	{
		fprintf(stderr, "Invalid object version\n");
		exit(1);
	}

	env->entry = freadword(input_file);
	env->stack = ei_class == ELFCLASS32 ? 0x40800000 : 0x000040000080000;

	uint64_t phoff = freadword(input_file);
	uint64_t shoff = freadword(input_file);

	uint32_t flags = fread32(input_file);

	if(env->isa == ISA_UNKNOWN)
	{
		if(ei_class == ELFCLASS64)
		{
			env->isa = ISA_AARCH64;
		}
		else
		{
			switch(e_machine)
			{
			case EM_AARCH64:
				env->isa = ISA_AARCH64;
				break;
			case EM_PJ:
			case EM_OLD_PICOJAVA:
				env->isa = ISA_JAZELLE;
				break;
			case EM_ARM:
				if(env->purpose == PURPOSE_LOAD)
				{
					if((env->entry & 1) != 0)
					{
						env->isa = ISA_THUMB32;
						env->entry &= ~1;
					}
					else
					{
						env->isa = ISA_AARCH32;
					}
				}
				else
				{
					force32bit = true; // figure out actual instruction set during parsing
				}
				break;
			}
		}
	}

	// for big endian 32-bit executables, we have to check the flags to make sure what type the executable is
	if(ei_data == ELFDATA2MSB)
	{
		if(e_machine == EM_ARM && (flags & EF_ARM_BE8) == 0)
		{
			// BE-32 executable
			if(env->endian == ARM_ENDIAN_DEFAULT || env->endian == ARM_ENDIAN_BE_VERSION_SPECIFIC)
				env->endian = ARM_ENDIAN_SWAPPED;
			else if(env->endian != ARM_ENDIAN_SWAPPED)
				fprintf(stderr, "Invalid endian directive\n");
		}
		else
		{
			// BE-8 executable
			if(env->endian == ARM_ENDIAN_DEFAULT || env->endian == ARM_ENDIAN_BE_VERSION_SPECIFIC)
				env->endian = ARM_ENDIAN_BIG;
			else if(env->endian != ARM_ENDIAN_BIG)
				fprintf(stderr, "Invalid endian directive\n");
		}
	}
	else
	{
		// LE executable
		if(env->endian == ARM_ENDIAN_DEFAULT)
			env->endian = ARM_ENDIAN_LITTLE;
		else if(env->endian != ARM_ENDIAN_LITTLE)
			fprintf(stderr, "Invalid endian directive\n");
	}

	fseek(input_file, 2L, SEEK_CUR);

	uint16_t phentsize = fread16(input_file);
	uint16_t phnum = fread16(input_file);
	uint16_t shentsize = fread16(input_file);
	uint16_t shnum = fread16(input_file);

	// if the instruction set is unknown/changes throughout the code (32-bit and not specified at launch time), we need to collect them
	bool follow_mapping_symbols = env->isa == ISA_UNKNOWN;
	init_isa(&env->config, &env->isa, &env->syntax, env->thumb2, force32bit);

	struct mapping
	{
		uint32_t address;
		arm_instruction_set_t isa;
	};

	struct mapping * mapping_symbols = NULL;
	uint32_t mapping_symbol_count = 0;

	if(env->purpose == PURPOSE_PARSE && follow_mapping_symbols && shnum != 0)
	{
		// collect mapping symbols for proper disassembly (not needed if executable is actually executed)

		// first, count number of mapping symbols
		for(uint16_t i = 0; i < shnum; i++)
		{
			fseek(input_file, shoff + i * shentsize + 4, SEEK_SET);
			uint32_t type = fread32(input_file);
			if(type == SHT_SYMTAB)
			{
				fseek(input_file, ei_class == ELFCLASS32 ? 8 : 16, SEEK_CUR);
				uint64_t offset = freadword(input_file);
				uint64_t size = freadword(input_file);
				uint32_t link = fread32(input_file);
				fseek(input_file, ei_class == ELFCLASS32 ? 8 : 12, SEEK_CUR);
				uint64_t entsize = freadword(input_file);

				fseek(input_file, shoff + link * shentsize + (ei_class == ELFCLASS32 ? 16 : 24), SEEK_SET);
				uint64_t strtab_offset = freadword(input_file);

				for(uint64_t stoff = 0; stoff < size; stoff += entsize)
				{
					fseek(input_file, offset + stoff, SEEK_SET);
					uint32_t name = fread32(input_file);
					fseek(input_file, strtab_offset + name, SEEK_SET);
					if(fgetc(input_file) == '$')
					{
						switch(fgetc(input_file))
						{
						case 'a': // only ARM binaries
						case 't': // only ARM binaries
						case 'd':
						case 'x': // only AARCH64 binaries (we actually never reach this branch, we do not collect mapping symbols for 64-bit binaries)
						case 'j': // custom extension for Jazelle (never found in binaries generated by GNU binutils)
							mapping_symbol_count ++;
							break;
						}
					}
					/*printf("Symbol: ");
					for(;;)
					{
						int c = fgetc(input_file);
						if(c == '\0' || c == -1)
							break;
						printf("%c", c);
					}
					printf("\n");*/
				}
			}
		}

		mapping_symbols = malloc(sizeof(struct mapping) * mapping_symbol_count);
		uint32_t mapping_symbol_index = 0;

		// then, collect mapping symbols and sort them via their addresses
		for(uint16_t i = 0; i < shnum; i++)
		{
			fseek(input_file, shoff + i * shentsize + 4, SEEK_SET);
			uint32_t type = fread32(input_file);
			if(type == SHT_SYMTAB)
			{
				fseek(input_file, ei_class == ELFCLASS32 ? 8 : 16, SEEK_CUR);
				uint64_t offset = freadword(input_file);
				uint64_t size = freadword(input_file);
				uint32_t link = fread32(input_file);
				fseek(input_file, ei_class == ELFCLASS32 ? 8 : 12, SEEK_CUR);
				uint64_t entsize = freadword(input_file);

				fseek(input_file, shoff + link * shentsize + (ei_class == ELFCLASS32 ? 16 : 24), SEEK_SET);
				uint64_t strtab_offset = freadword(input_file);

				for(uint64_t stoff = 0; stoff < size; stoff += entsize)
				{
					fseek(input_file, offset + stoff, SEEK_SET);
					uint32_t name = fread32(input_file);
					if(ei_class == ELFCLASS64)
						fseek(input_file, 4, SEEK_CUR);
					uint64_t address = freadword(input_file);
					fseek(input_file, strtab_offset + name, SEEK_SET);
					if(fgetc(input_file) == '$')
					{
						uint32_t current_index = mapping_symbol_index;
						arm_instruction_set_t next_isa = (arm_instruction_set_t)-1;
						switch(fgetc(input_file))
						{
						case 'a': // only ARM binaries
							next_isa = ISA_AARCH32;
							break;
						case 't': // only ARM binaries
							if(fgetc(input_file) == '.' && fgetc(input_file) == 'x')
							{
								next_isa = ISA_THUMBEE;
							}
							else
							{
								next_isa = ISA_THUMB32;
							}
							break;
						case 'x': // only AARCH64 binaries (we actually never reach this branch, we do not collect mapping symbols for 64-bit binaries)
							next_isa = ISA_AARCH64;
							break;
						case 'd':
							next_isa = ISA_UNKNOWN;
							break;
						case 'j': // custom extension (never found in binaries generated by GNU binutils)
							next_isa = ISA_JAZELLE;
							break;
						}

						if(next_isa != (arm_instruction_set_t)-1)
						{
							while(current_index > 0 && mapping_symbols[current_index - 1].address > address)
							{
								// simplistic sorting, O(n^2) but best case and most likely scenario is O(1)
								mapping_symbols[current_index] = mapping_symbols[current_index - 1];
								current_index--;
							}
							mapping_symbols[current_index].address = address;
							mapping_symbols[current_index].isa = next_isa;
							mapping_symbol_index++;
						}
					}
				}
			}
		}
	}

	arm_parser_state_t dis[1];

	if(env->purpose == PURPOSE_PARSE)
	{
		arm_disasm_init(dis, env->config, env->isa, env->syntax);
		arm_disasm_set_file(dis, input_file, env->endian);

		isa_display(env->config, env->isa, env->syntax, true, env->endian);
	}

	for(uint16_t i = 0; i < phnum; i++)
	{
		fseek(input_file, phoff + i * phentsize, SEEK_SET);
		uint32_t type = fread32(input_file);
		if(type == PT_LOAD)
		{
			if(ei_class == ELFCLASS64)
				fseek(input_file, 4, SEEK_CUR); // skip flags

			uint64_t offset = freadword(input_file);
			uint64_t v_address = freadword(input_file);

			fseek(input_file, ei_class == ELFCLASS32 ? 4 : 8, SEEK_CUR); // skip p_address

			uint64_t filesize = freadword(input_file);

			fseek(input_file, offset, SEEK_SET);

			uint32_t mapping_symbol_index = 0;

			if(env->purpose == PURPOSE_PARSE)
			{
				dis->pc = v_address;

				if(mapping_symbol_count != 0)
					dis->isa = ISA_UNKNOWN;

				while(dis->pc < v_address + filesize)
				{
					arm_instruction_set_t isa = dis->isa;
					while(mapping_symbol_index < mapping_symbol_count && mapping_symbols[mapping_symbol_index].address <= dis->pc)
					{
						dis->isa = mapping_symbols[mapping_symbol_index++].isa;
					}

					if(isa != dis->isa || dis->pc == v_address)
					{
						switch((int)dis->isa)
						{
						case ISA_AARCH32:
							printf("Instruction set: ARM32\n");
							break;
						case ISA_THUMBEE:
							printf("Instruction set: ThumbEE\n");
							break;
						case ISA_THUMB32:
							printf("Instruction set: Thumb32\n");
							break;
						case ISA_AARCH64:
							printf("Instruction set: ARM64\n");
							break;
						case ISA_UNKNOWN:
							printf("Instruction set: none (data)\n");
							break;
						case ISA_JAZELLE:
							printf("Instruction set: Java\n");
							break;
						}
					}

					parse(dis);
				}
			}
			else
			{
				// load binary into memory

				uint64_t offset;

				for(offset = 0; offset < filesize; offset++)
				{
					uint64_t address = v_address + offset;

					char c = fgetc(input_file);

					arm_memory_write8(env->memory_interface, address, c, env->endian == ARM_ENDIAN_SWAPPED ? ARM_ENDIAN_SWAPPED : ARM_ENDIAN_LITTLE);
				}
			}
		}
	}
}

bool a32_linux_oabi_syscall(arm_state_t * cpu, uint32_t swi_number)
{
	switch(swi_number)
	{
	case A32_OABI_SYS_BASE + A32_SYS_EXIT:
		exit(cpu->r[0]);
		return true;
	case A32_OABI_SYS_BASE + A32_SYS_WRITE:
		{
			uint64_t address = cpu->r[1];
			uint64_t count = cpu->r[2];
			void * buffer;

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				buffer = memory_acquire_block(address, count);
			else
				buffer = memory_acquire_block_reversed(address, count);

			cpu->r[0] = write(cpu->r[0], buffer, count);

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				memory_release_block(address, count, buffer);
			else
				memory_release_block_reversed(address, count, buffer);
		}
		return true;
	default:
		return false;
	}
}

bool a32_linux_eabi_syscall(arm_state_t * cpu)
{
	switch(cpu->r[7])
	{
	case A32_SYS_EXIT:
		exit(cpu->r[0]);
		return true;
	case A32_SYS_WRITE:
		{
			uint64_t address = cpu->r[1];
			uint64_t count = cpu->r[2];
			void * buffer;

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				buffer = memory_acquire_block(address, count);
			else
				buffer = memory_acquire_block_reversed(address, count);

			cpu->r[0] = write(cpu->r[0], buffer, count);

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				memory_release_block(address, count, buffer);
			else
				memory_release_block_reversed(address, count, buffer);
		}
		return true;
	default:
		return false;
	}
}

bool a64_linux_syscall(arm_state_t * cpu)
{
	switch(cpu->r[8])
	{
	case A64_SYS_EXIT:
		exit(cpu->r[0]);
		return true;
	case A64_SYS_WRITE:
		{
			uint64_t address = cpu->r[1];
			uint64_t count = cpu->r[2];
			void * buffer = memory_acquire_block(address, count);
			cpu->r[0] = write(cpu->r[0], buffer, count);
			memory_release_block(address, count, buffer);
		}
		return true;
	default:
		return false;
	}
}

bool j32_linux_syscall(arm_state_t * cpu)
{
	uint32_t syscall_number = j32_peek_word(cpu, 0);
	switch(syscall_number)
	{
	case A32_SYS_EXIT:
		{
			j32_pop_word(cpu);
			int32_t status = j32_pop_word(cpu);
			exit(status);
		}
		return true;
	case A32_SYS_WRITE:
		{
			j32_pop_word(cpu);
			int32_t count = j32_pop_word(cpu);
			uint32_t address = j32_pop_word(cpu);
			int32_t fd = j32_pop_word(cpu);
			void * buffer;

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				buffer = memory_acquire_block(address, count);
			else
				buffer = memory_acquire_block_reversed(address, count);

			uint32_t result = write(fd, buffer, count);

			j32_push_word(cpu, result);

			if(a32_get_data_endianness(cpu) != ARM_ENDIAN_SWAPPED)
				memory_release_block(address, count, buffer);
			else
				memory_release_block_reversed(address, count, buffer);
		}
		return true;
	default:
		return false;
	}
}

