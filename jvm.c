
/* Java class file parsing and JVM emulation */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jvm.h"
#include "emu.h"
#include "jazelle.h"
#include "dis.h"
#include "elf.h"

jvm_constant_t * constant_pool;

#define CHECK_STR(__utf8, __strlit) ((__utf8).length == sizeof(__strlit) - 1 && memcmp((__utf8).bytes, (__strlit), sizeof(__strlit) - 1) == 0)
#define CHECK_NAME_TYPE(__ref, __name, __type) (CHECK_STR(constant_pool[(__ref).name_index].utf8, __name) && CHECK_STR(constant_pool[(__ref).type_index].utf8, __type))

void read_class_file(FILE * input_file, environment_t * env)
{
	uint16_t constant_pool_count;

	if(env->isa == ISA_UNKNOWN)
		env->isa = ISA_JAZELLE;

	if(env->endian != ARM_ENDIAN_LITTLE && env->endian != ARM_ENDIAN_BIG && env->endian != ARM_ENDIAN_SWAPPED)
		env->endian = ARM_ENDIAN_LITTLE; // Class file and instruction stream is parsed as big-endian either way
	init_isa(&env->config, &env->isa, &env->syntax, env->thumb2, false);

	/*
		we place the constant pool at the start address
		this is then followed by the class data (fields and methods)
		then the stack area and heap area follow
	*/
	uint32_t address = 0x10004;
	env->cp_start = address;

	fseek(input_file, 4L, SEEK_CUR);
	constant_pool_count = fread16be(input_file);
	constant_pool = malloc(sizeof(jvm_constant_t) * constant_pool_count);
	memset(constant_pool, 0, sizeof(jvm_constant_t) * constant_pool_count);
	for(uint16_t i = 1; i < constant_pool_count; i++)
	{
		constant_pool[i].type = fread8(input_file);
		switch(constant_pool[i].type)
		{
		case CONSTANT_Utf8:
			constant_pool[i].utf8.length = fread16be(input_file);
			constant_pool[i].utf8.bytes = malloc(constant_pool[i].utf8.length);
			fread(constant_pool[i].utf8.bytes, 1, constant_pool[i].utf8.length, input_file);
			break;
		case CONSTANT_Integer:
		case CONSTANT_Float:
			constant_pool[i].integer = fread32be(input_file);
			break;
		case CONSTANT_Long:
		case CONSTANT_Double:
			constant_pool[i]._long = fread64be(input_file);
			i ++;
			break;
		case CONSTANT_Class:
		case CONSTANT_String:
			constant_pool[i]._class = fread16be(input_file);
			break;
		case CONSTANT_Fieldref:
		case CONSTANT_Methodref:
		case CONSTANT_InterfaceMethodref:
		case CONSTANT_NameAndType:
			constant_pool[i].fieldref.class_index = fread16be(input_file);
			constant_pool[i].fieldref.name_and_type_index = fread16be(input_file);
			break;
		case CONSTANT_MethodHandle:
			fseek(input_file, 3L, SEEK_CUR);
			break;
		case CONSTANT_MethodType:
			fseek(input_file, 2L, SEEK_CUR);
			break;
		case CONSTANT_InvokeDynamic:
			fseek(input_file, 4L, SEEK_CUR);
			break;
		default:
			fprintf(stderr, "Invalid constant pool entry\n");
			exit(1);
		}
	}

	address += 4 * constant_pool_count;

	fseek(input_file, 2L, SEEK_CUR);
	uint16_t this_class_index = fread16be(input_file);
	(void) this_class_index;
	fseek(input_file, 2L, SEEK_CUR);
	uint16_t interfaces_count = fread16be(input_file);
	fseek(input_file, interfaces_count * 2L, SEEK_CUR);
	uint16_t fields_count = fread16be(input_file);

	struct jvm_field
	{
		uint16_t name_index;
		uint16_t type_index;
		uint32_t address;
	} * fields = malloc(sizeof(struct jvm_field) * fields_count);

	for(uint16_t i = 0; i < fields_count; i++)
	{
		uint16_t access_flags = fread16be(input_file);
		fields[i].name_index = fread16be(input_file);
		fields[i].type_index = fread16be(input_file);
		uint16_t attributes_count = fread16be(input_file);
		size_t elsize = 0;

		if((access_flags & ACC_STATIC))
		{
			switch(constant_pool[fields[i].type_index].utf8.bytes[0])
			{
			case 'B':
			case 'Z':
				elsize = 1;
				break;
			case 'C':
			case 'S':
				elsize = 2;
				break;
			case 'F':
			case 'I':
			case 'L':
			case '[':
				elsize = 4;
				break;
			case 'D':
			case 'J':
				elsize = 8;
				break;
			}
			address = (address + elsize - 1) & ~(elsize - 1);
			fields[i].address = address;
			address += elsize;
		}
		else
		{
			fields[i].address = 0;
		}

		for(uint16_t j = 0; j < attributes_count; j++)
		{
			uint16_t name_index = fread16be(input_file);
			uint32_t attribute_length = fread32be(input_file);
			if(attribute_length == 2 && CHECK_STR(constant_pool[name_index].utf8, "ConstantValue"))
			{
				uint16_t index = fread16be(input_file);
				if(fields[i].address != 0)
				{
					switch(constant_pool[index].type)
					{
					case CONSTANT_Integer:
						switch(elsize)
						{
						case 1:
							arm_memory_write8(env->memory_interface, fields[i].address, constant_pool[index].integer, env->endian);
							break;
						case 2:
							arm_memory_write16(env->memory_interface, fields[i].address, constant_pool[index].integer, env->endian);
							break;
						case 4:
							arm_memory_write32(env->memory_interface, fields[i].address, constant_pool[index].integer, env->endian);
							break;
						}
						break;
					case CONSTANT_Float:
						arm_memory_write32(env->memory_interface, fields[i].address, constant_pool[index].integer, env->endian);
						break;
					case CONSTANT_Long:
					case CONSTANT_Double:
						arm_memory_write64(env->memory_interface, fields[i].address, constant_pool[index]._long, env->endian);
						break;
					case CONSTANT_String:
						// TODO
						break;
					default:
						break;
					}
				}
			}
			else
			{
				fseek(input_file, attribute_length, SEEK_CUR);
			}
		}
	}

	uint16_t methods_count = fread16be(input_file);

	struct jvm_method
	{
		uint16_t name_index;
		uint16_t type_index;
		uint32_t address;
	} * methods = malloc(sizeof(struct jvm_method) * methods_count);

	arm_parser_state_t dis[1];

	if(env->purpose == PURPOSE_PARSE)
	{
		arm_disasm_init(dis, env->config, env->isa, env->syntax);
		arm_disasm_set_file(dis, input_file, env->endian);

		isa_display(env->config, env->isa, env->syntax, true, env->endian);
	}

	for(uint16_t i = 0; i < methods_count; i++)
	{
		uint16_t access_flags = fread16be(input_file);
		methods[i].name_index = fread16be(input_file);
		methods[i].type_index = fread16be(input_file);
		uint16_t attributes_count = fread16be(input_file);

		/*
			the first 3 words of a method shall consist of:
				the constant pool address
				the number of argument bytes
				the number of local bytes
			if the constant pool address is 0, it refers to a system call (native function with predefined name and type)
			the local bytes count will instead hold the 32-bit system call number
		*/

		for(uint16_t j = 0; j < attributes_count; j++)
		{
			uint16_t name_index = fread16be(input_file);
			uint32_t attribute_length = fread32be(input_file);
			if(constant_pool[name_index].type == CONSTANT_Utf8 && CHECK_STR(constant_pool[name_index].utf8, "Code"))
			{
				uint16_t max_stack = fread16be(input_file);
				uint16_t max_locals = fread16be(input_file);
				uint32_t code_length = fread32be(input_file);

				(void) max_stack;

				if(env->purpose == PURPOSE_PARSE)
				{
					printf("%.*s%.*s:\n",
						constant_pool[methods[i].name_index].utf8.length, constant_pool[methods[i].name_index].utf8.bytes,
						constant_pool[methods[i].type_index].utf8.length, constant_pool[methods[i].type_index].utf8.bytes
					);

					dis->pc = 0;

					while(dis->pc < code_length)
					{
						parse(dis);
					}
				}
				else if(env->purpose == PURPOSE_LOAD)
				{
					if((access_flags) & 0x0500) // abstract or native
					{
						fprintf(stderr, "Error: native/abstract method with body\n");
						exit(1);
					}

					address = (address + 3) & ~3;
					methods[i].address = address;

					// TODO: type should be either ()V or ([[B[[B)V
					if(CHECK_STR(constant_pool[methods[i].name_index].utf8, "_start"))
					{
						env->entry = address + 12;
					}
					else if(CHECK_STR(constant_pool[methods[i].name_index].utf8, "<clinit>"))
					{
						env->clinit_entry = address + 12;
					}

					uint64_t offset;
					{
						arm_memory_write32(env->memory_interface, address, env->cp_start, env->endian);

						uint32_t arg_bytes = 0;
						for(int j = 1; j < constant_pool[methods[i].type_index].utf8.length && constant_pool[methods[i].type_index].utf8.bytes[j] != ')'; j++)
						{
							switch(constant_pool[methods[i].type_index].utf8.bytes[j])
							{
							case 'B':
							case 'C':
							case 'F':
							case 'I':
							case 'S':
							case 'Z':
								arg_bytes += 4;
								break;
							case 'D':
							case 'J':
								arg_bytes += 8;
								break;
							case '[':
								arg_bytes += 4;
								j ++;
								while(j < constant_pool[methods[i].type_index].utf8.length && constant_pool[methods[i].type_index].utf8.bytes[j] == '[')
									j++;
								if(j < constant_pool[methods[i].type_index].utf8.length && constant_pool[methods[i].type_index].utf8.bytes[j] == 'L')
								{
									j ++;
									while(j < constant_pool[methods[i].type_index].utf8.length && constant_pool[methods[i].type_index].utf8.bytes[j] != ';')
										j++;
								}
								break;
							case 'L':
								arg_bytes += 4;
								j ++;
								while(j < constant_pool[methods[i].type_index].utf8.length && constant_pool[methods[i].type_index].utf8.bytes[j] != ';')
									j++;
								break;
							}
						}

						arm_memory_write32(env->memory_interface, address + 4, arg_bytes, env->endian);

						arm_memory_write32(env->memory_interface, address + 8, 4 * max_locals, env->endian);

						if(env->entry == address + 12)
							env->loc_count = 4 * max_locals;
						else if(env->clinit_entry == address + 12)
							env->clinit_loc_count = 4 * max_locals;
					}
					address += 12;

					for(offset = 0; offset < code_length; offset++)
					{
						char c = fgetc(input_file);
						arm_memory_write8(env->memory_interface, address + offset, c, env->endian == ARM_ENDIAN_SWAPPED ? ARM_ENDIAN_SWAPPED : ARM_ENDIAN_LITTLE);
					}
					address += code_length;
					address = (address + 3) & ~3;
				}

				fseek(input_file, attribute_length - 8L - code_length, SEEK_CUR);
			}
			else
			{
				fseek(input_file, attribute_length, SEEK_CUR);
			}
		}
	}

	for(uint16_t i = 1; i < constant_pool_count; i++)
	{
		switch(constant_pool[i].type)
		{
		case CONSTANT_Utf8:
			// ignore
			//constant_pool[i].utf8.length = fread16be(input_file);
			//constant_pool[i].utf8.bytes = malloc(constant_pool[i].utf8.length);
			break;
		case CONSTANT_Integer:
		case CONSTANT_Float:
			arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, constant_pool[i].integer, env->endian);
			break;
		case CONSTANT_Long:
		case CONSTANT_Double:
			arm_memory_write64(env->memory_interface, env->cp_start + 4 * i, constant_pool[i]._long, env->endian);
			i ++;
			break;
		case CONSTANT_Class:
			// ignore
			//constant_pool[i]._class = fread16be(input_file);
			break;
		case CONSTANT_String:
			// ignore
			{
				address = (address + 3) & ~3;
				arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, address + 4, env->endian);
				arm_memory_write32(env->memory_interface, address, constant_pool[constant_pool[i].string].utf8.length, env->endian);
				for(uint16_t pos = 0; pos < constant_pool[constant_pool[i].string].utf8.length; pos++)
				{
					arm_memory_write8(env->memory_interface, address + 4 + pos, constant_pool[constant_pool[i].string].utf8.bytes[pos], env->endian);
				}
				address += 4 + constant_pool[constant_pool[i].string].utf8.length;
			}
			break;
		case CONSTANT_Fieldref:
			// ignore
			{
				int j;
				for(j = 0; j < fields_count; j++)
				{
					if(constant_pool[constant_pool[i].fieldref.name_and_type_index].name_and_type.name_index == fields[j].name_index
					&& constant_pool[constant_pool[i].fieldref.name_and_type_index].name_and_type.type_index == fields[j].type_index)
					{
						arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, fields[j].address, env->endian);
						break;
					}
				}
			}
			break;
		case CONSTANT_Methodref:
			{
				int j;
				for(j = 0; j < methods_count; j++)
				{
					if(constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type.name_index == methods[j].name_index
					&& constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type.type_index == methods[j].type_index)
					{
						arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, methods[j].address, env->endian);
						break;
					}
				}

				if(j == methods_count)
				{
					if(CHECK_STR(constant_pool[constant_pool[constant_pool[i].methodref.class_index]._class].utf8, "abi/Linux"))
					{
						arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, address, env->endian);

						arm_memory_write32(env->memory_interface, address, 0, env->endian); // no constant pool, system call
						arm_memory_write32(env->memory_interface, address + 4, 0, env->endian); // ignore argument count for now

						uint32_t syscall_number = address;
						if(CHECK_NAME_TYPE(constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type, "exit", "(I)V"))
						{
							syscall_number = A32_SYS_EXIT;
						}
						else if(CHECK_NAME_TYPE(constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type, "write", "(I[BII)I"))
						{
							syscall_number = A32_SYS_WRITE;
						}
						else if(CHECK_NAME_TYPE(constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type, "brk", "(I)I"))
						{
							syscall_number = A32_SYS_BRK;
						}
						else
						{
							syscall_number = 0;
						}

						arm_memory_write32(env->memory_interface, address + 8, syscall_number, env->endian);

						address += 12;
					}
					else if(CHECK_STR(constant_pool[constant_pool[constant_pool[i].methodref.class_index]._class].utf8, "java/lang/String")
					&& CHECK_NAME_TYPE(constant_pool[constant_pool[i].methodref.name_and_type_index].name_and_type, "getBytes", "()[B"))
					{
						arm_memory_write32(env->memory_interface, env->cp_start + 4 * i, address, env->endian);

						arm_memory_write32(env->memory_interface, address, 0, env->endian); // no constant pool, system call
						arm_memory_write32(env->memory_interface, address + 4, 0, env->endian); // ignore argument count for now
						arm_memory_write32(env->memory_interface, address + 8, J32_SYS_GETBYTES, env->endian);

						address += 12;
					}
				}
			}
			break;
		case CONSTANT_InterfaceMethodref:
			// ignore
			//constant_pool[i].interfacemethodref.class_index = fread16be(input_file);
			//constant_pool[i].interfacemethodref.name_and_type_index = fread16be(input_file);
			break;
		case CONSTANT_NameAndType:
			// ignore
			//constant_pool[i].name_and_type.name_index = fread16be(input_file);
			//constant_pool[i].name_and_type.type_index = fread16be(input_file);
			break;
		default:
			break;
		}
	}

	free(methods);

	env->stack = address;
}

void j32_invoke(arm_state_t * cpu, uint32_t argument_count, uint32_t local_count, uint32_t address)
{
	uint32_t old_loc = cpu->r[J32_LOC];
	cpu->r[J32_LOC] = cpu->r[J32_TOS] - argument_count;
	cpu->r[J32_TOS] = cpu->r[J32_LOC] + local_count;
	j32_push_word(cpu, cpu->r[PC]);
	j32_push_word(cpu, old_loc);
	j32_push_word(cpu, cpu->r[J32_CP]);
	j32_push_word(cpu, cpu->r[J32_LINK]);
	j32_spill_fast_stack(cpu); // needed to update TOS properly
	cpu->r[J32_LINK] = cpu->r[J32_TOS];
	cpu->r[PC] = address;
}

bool j32_simulate_instruction(arm_state_t * cpu, uint32_t heap_start)
{
	j32_spill_fast_stack(cpu);
	switch(arm_fetch8(cpu, cpu->r[PC]++))
	{
	case 0xB6:
		// invokevirtual
		{
			uint16_t index = arm_fetch16be(cpu, cpu->r[PC]);
			uint32_t method_address = arm_memory_read32_data(cpu, cpu->r[J32_CP] + 4 * index);
			uint32_t new_cp_address = arm_memory_read32_data(cpu, method_address);
			if(new_cp_address == 0)
			{
				// system call
				uint32_t syscall_num = arm_memory_read32_data(cpu, method_address + 8);
				switch(syscall_num)
				{
				case J32_SYS_GETBYTES:
					break;
				default:
					return false;
				}
				cpu->r[PC] += 2;
			}
			else
			{
				return false;
			}
		}
		break;

	case 0xB8:
		// invokestatic
		{
			uint16_t index = arm_fetch16be(cpu, cpu->r[PC]);
			uint32_t method_address = arm_memory_read32_data(cpu, cpu->r[J32_CP] + 4 * index);
			uint32_t new_cp_address = arm_memory_read32_data(cpu, method_address);
			if(new_cp_address == 0)
			{
				// system call
				uint32_t syscall_num = arm_memory_read32_data(cpu, method_address + 8);
				switch(syscall_num)
				{
				case A32_SYS_EXIT:
					{
						int32_t status = j32_pop_word(cpu);
						exit(status);
					}
					break;
				case A32_SYS_WRITE:
					{
						int32_t count = j32_pop_word(cpu);
						uint32_t buf = j32_pop_word(cpu);
						int32_t offset = j32_pop_word(cpu);
						int32_t fd = j32_pop_word(cpu);

						void * buffer = memory_acquire_block(buf + offset, count);
						int32_t result = write(fd, buffer, count);
						memory_release_block(buf + offset, count, buffer);

						j32_push_word(cpu, result);
					}
					break;
				case A32_SYS_BRK:
					{
						int32_t new_heap = j32_pop_word(cpu);
						j32_push_word(cpu, cpu->r[J32_HEAP]);
						if(new_heap >= (int32_t)heap_start)
							cpu->r[J32_HEAP] = new_heap;
					}
					break;
				default:
					return false;
				}
				cpu->r[PC] += 2;
			}
			else
			{
				uint32_t argument_count = arm_memory_read32_data(cpu, method_address + 4);
				uint32_t local_count = arm_memory_read32_data(cpu, method_address + 8);
				cpu->r[PC] += 2;
				j32_invoke(cpu, argument_count, local_count, method_address + 12);
				cpu->r[J32_CP] = new_cp_address;
			}
		}
		break;
	case 0xAC:
		// ireturn
	case 0xAE:
		// freturn
	case 0xB0:
		// areturn
		{
			int32_t result = j32_pop_word(cpu);

			cpu->r[J32_TOS] = cpu->r[J32_LINK];
			cpu->r[J32_LINK] = j32_pop_word(cpu);
			cpu->r[J32_CP] = j32_pop_word(cpu);
			uint32_t new_sp = cpu->r[J32_LOC];
			cpu->r[J32_LOC] = j32_pop_word(cpu);
			cpu->r[PC] = j32_pop_word(cpu);
			cpu->r[J32_TOS] = new_sp;

			j32_push_word(cpu, result);
		}
		break;
	case 0xAD:
		// lreturn
	case 0xAF:
		// dreturn
		{
			int64_t result = j32_pop_dword(cpu);

			cpu->r[J32_TOS] = cpu->r[J32_LINK];
			cpu->r[J32_LINK] = j32_pop_word(cpu);
			cpu->r[J32_CP] = j32_pop_word(cpu);
			uint32_t new_sp = cpu->r[J32_LOC];
			cpu->r[J32_LOC] = j32_pop_word(cpu);
			cpu->r[PC] = j32_pop_word(cpu);
			cpu->r[J32_TOS] = new_sp;

			j32_push_dword(cpu, result);
		}
		break;
	case 0xB1:
		// return
		{
			cpu->r[J32_TOS] = cpu->r[J32_LINK];
			cpu->r[J32_LINK] = j32_pop_word(cpu);
			cpu->r[J32_CP] = j32_pop_word(cpu);
			uint32_t new_sp = cpu->r[J32_LOC];
			cpu->r[J32_LOC] = j32_pop_word(cpu);
			cpu->r[PC] = j32_pop_word(cpu);
			cpu->r[J32_TOS] = new_sp;
		}
		break;
	case 0xB2:
		// getstatic
		{
			uint16_t index = arm_fetch16be(cpu, cpu->r[PC]);
			uint32_t field_address = arm_memory_read32_data(cpu, cpu->r[J32_CP] + 4 * index);
			switch(constant_pool[constant_pool[constant_pool[index].fieldref.name_and_type_index].name_and_type.type_index].utf8.bytes[0])
			{
			case 'B':
				j32_push_word(cpu, sign_extend(1, arm_memory_read8_data(cpu, field_address)));
				break;
			case 'Z':
				j32_push_word(cpu, arm_memory_read8_data(cpu, field_address));
				break;
			case 'C':
				j32_push_word(cpu, arm_memory_read16_data(cpu, field_address));
				break;
			case 'S':
				j32_push_word(cpu, sign_extend(2, arm_memory_read16_data(cpu, field_address)));
				break;
			case 'F':
			case 'I':
			case 'L':
			case '[':
				j32_push_word(cpu, arm_memory_read32_data(cpu, field_address));
				break;
			case 'D':
			case 'J':
				j32_push_dword(cpu, arm_memory_read64_data(cpu, field_address));
				break;
			}
		}
		cpu->r[PC] += 2;
		break;
	case 0xB3:
		// putstatic
		{
			uint16_t index = arm_fetch16be(cpu, cpu->r[PC]);
			uint32_t field_address = arm_memory_read32_data(cpu, cpu->r[J32_CP] + 4 * index);
			switch(constant_pool[constant_pool[constant_pool[index].fieldref.name_and_type_index].name_and_type.type_index].utf8.bytes[0])
			{
			case 'B':
			case 'Z':
				arm_memory_write8_data(cpu, field_address, j32_pop_word(cpu));
			case 'C':
			case 'S':
				arm_memory_write16_data(cpu, field_address, j32_pop_word(cpu));
				break;
			case 'F':
			case 'I':
			case 'L':
			case '[':
				arm_memory_write32_data(cpu, field_address, j32_pop_word(cpu));
				break;
			case 'D':
			case 'J':
				arm_memory_write64_data(cpu, field_address, j32_pop_dword(cpu));
				break;
			}
		}
		cpu->r[PC] += 2;
		break;
	case 0xBC:
		// newarray
		{
			int32_t size = j32_pop_word(cpu);
			size_t elsize;
			switch(arm_fetch8(cpu, cpu->r[PC]++))
			{
			case T_BYTE:
			case T_BOOLEAN:
				elsize = 1;
				break;
			case T_SHORT:
			case T_CHAR:
				elsize = 2;
				break;
			case T_INT:
			case T_FLOAT:
				elsize = 4;
				break;
			case T_LONG:
			case T_DOUBLE:
				elsize = 8;
				break;
			default:
				return false;
			}
			j32_push_word(cpu, cpu->r[J32_HEAP] + 4);
			arm_memory_write32_data(cpu, cpu->r[J32_HEAP], size);
			cpu->r[J32_HEAP] += 4 + elsize * size;
		}
		break;
	case 0xBE:
		// anewarray
		{
			cpu->r[PC] += 2;
			int32_t size = j32_pop_word(cpu);
			size_t elsize = 4;
			j32_push_word(cpu, cpu->r[J32_HEAP] + 4);
			arm_memory_write32_data(cpu, cpu->r[J32_HEAP], size);
			cpu->r[J32_HEAP] += 4 + elsize * size;
		}
		break;
	default:
		return false;
	}
	// return and invoke will require updating this, it's easier to do it here
	j32_update_locals(cpu);
	j32_spill_fast_stack(cpu);
	return true;
}

