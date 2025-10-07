
/* Jazelle emulation, must be included from emu.c for proper behavior */

// Note: this is just one possible implementation, based on information from https://hackspire.org/index.php/Jazelle and https://github.com/SonoSooS/libjz

/*
	The Java implementation here is based on publicly documented reverse engineering efforts by others
	As an extension, the emulator can execute a couple of instructions that most Jazelle implementation do not have, and that do not require memory management
	A number of supplementary instructions are also implemented to provide better interworking with ARM/Thumb mode
	When meaningful, these instructions are borrowed from the picoJava-II hardware implementation
	While picoJava and Jazelle have incompatible approaches to hardware execution of JVM bytecode, partial compatibility avoids the creation of a plethora of competing JVM bytecode extensions
	However, this is only possible by removing the Jazelle 0xFF byte that corresponds to the "bkpt #0" instruction
	Additional opcodes are added at byte value 0xFE, with currently two instructions implemented:
	* FE 00 - ret_from_jazelle: pops a 32-bit word value from stack and interprets it as an interworking target address for ARM/Thumb execution
	* FE 01 - swi: enters SVC mode in ARM/Thumb
*/

static inline void a32_bxj(arm_state_t * cpu, uint32_t no_jazelle_address)
{
	if(false)
	{
		a32_set_pc_mode(cpu, no_jazelle_address);
	}
	else
	{
		cpu->pstate.jt = PSTATE_JT_JAZELLE;
		cpu->r[PC] = a32_register_get32(cpu, A32_LR);
		a32_register_set32(cpu, J32_SHT, a32_register_get32(cpu, J32_SHT) & ~0x000003C0);
	}
}

// should only be used via j32_push_word
static inline void j32_push_word_memory(arm_state_t * cpu, uint32_t value)
{
	uint32_t sp = a32_register_get32(cpu, J32_TOS);
	a32_write32(cpu, sp, value);
	sp += 4;
	a32_register_set32(cpu, J32_TOS, sp);
}

// should only be used via j32_pop_word
static inline uint32_t j32_pop_word_memory(arm_state_t * cpu)
{
	uint32_t sp = a32_register_get32(cpu, J32_TOS);
	sp -= 4;
	uint32_t value = a32_read32(cpu, sp);
	a32_register_set32(cpu, J32_TOS, sp);
	return value;
}

#if J32_EMULATE_INTERNALS
// how many stack values are held in register R0-R3
uint32_t j32_get_fast_stack_size(arm_state_t * cpu)
{
	return (a32_register_get32(cpu, J32_SHT) >> 2) & 7;
}

// which register holds the top of the stack (R3 if empty, to get starting register of R0)
uint32_t j32_get_fast_stack_top(arm_state_t * cpu)
{
	return a32_register_get32(cpu, J32_SHT) & 0x1C ? a32_register_get32(cpu, J32_SHT) & 3 : 3;
}

uint32_t j32_get_fast_stack_element(arm_state_t * cpu, uint32_t offset)
{
	return a32_register_get32(cpu, J32_SHT) & 0x1C ? (a32_register_get32(cpu, J32_SHT) - offset) & 3 : (offset + 3) & 3;
}

// sets the register stack size (0 to 4) and top (R0 to R3)
static void j32_set_fast_stack_size_top(arm_state_t * cpu, unsigned size, unsigned top)
{
	if(size == 0)
		top = 0;
	uint64_t * sht = &a32_register(cpu, J32_SHT);
	*sht &= ~0x1F;
	*sht |= top & 3;
	*sht |= (size > 4 ? 4 : size) << 2;
}

// store as many registers into memory as required
static void j32_spill_fast_stack_size(arm_state_t * cpu, unsigned destination)
{
	unsigned current = j32_get_fast_stack_size(cpu);
	unsigned top = j32_get_fast_stack_top(cpu);
	if(current <= destination)
		return;
	while(current > destination)
	{
		j32_push_word_memory(cpu, a32_register_get32(cpu, (top - (current - 1)) & 3));
		current --;
	}
	j32_set_fast_stack_size_top(cpu, destination, top);
}

// reload as many registers from memory as required
static void j32_fill_fast_stack_size(arm_state_t * cpu, unsigned destination)
{
	unsigned current = j32_get_fast_stack_size(cpu);
	unsigned top = j32_get_fast_stack_top(cpu);
	if(current >= destination)
		return;
	if(current == 0)
		top = destination - 1; // bottom of stack should appear in R0
	while(current < destination)
	{
		uint32_t value = j32_pop_word_memory(cpu);
		a32_register_set32(cpu, (top - current) & 3, value);
		current ++;
	}
	j32_set_fast_stack_size_top(cpu, destination, top);
}
#endif

void j32_spill_fast_stack(arm_state_t * cpu)
{
#if J32_EMULATE_INTERNALS
	j32_spill_fast_stack_size(cpu, 0);
#endif
}

void j32_update_locals(arm_state_t * cpu)
{
#if J32_EMULATE_INTERNALS
	cpu->r[J32_LOC0] = arm_memory_read32_data(cpu, cpu->r[J32_LOC]);
#endif
}

static inline void j32_break(arm_state_t * cpu, uint32_t index)
{
	j32_spill_fast_stack(cpu);
	cpu->r[PC] = cpu->old_pc;
	if(!cpu->capture_breaks)
	{
		a32_register_set32(cpu, A32_LR, cpu->r[PC]);
		cpu->pstate.jt = PSTATE_JT_ARM;
		cpu->r[PC] = (a32_register_get32(cpu, J32_SHT) & 0xFFFFF000) + (index << 2);
	}
	else
	{
		switch(index)
		{
		default:
			cpu->result = ARM_EMU_JAZELLE_UNDEFINED;
			break;
		case J32_EXCEPTION_NULLPTR:
			cpu->result = ARM_EMU_JAZELLE_NULLPTR;
			break;
		case J32_EXCEPTION_OUT_OF_BOUNDS:
			cpu->result = ARM_EMU_JAZELLE_OUT_OF_BOUNDS;
			break;
		case J32_EXCEPTION_JAZELLE_DISABLED:
			cpu->result = ARM_EMU_JAZELLE_DISABLED;
			break;
		case J32_EXCEPTION_JAZELLE_INVALID:
			cpu->result = ARM_EMU_JAZELLE_INVALID;
			break;
		case J32_EXCEPTION_PREFETCH_ABORT:
			cpu->result = ARM_EMU_JAZELLE_PREFETCH_ABORT;
			break;
		}
	}
	longjmp(cpu->exc, 1);
}

void j32_push_word(arm_state_t * cpu, uint32_t value)
{
#if J32_EMULATE_INTERNALS
	unsigned top, size;
	size = j32_get_fast_stack_size(cpu);
	if(size == 4)
	{
		j32_spill_fast_stack_size(cpu, 3);
		size = 3;
	}
	top = j32_get_fast_stack_top(cpu);
	top = (top + 1) & 3;
	a32_register_set32(cpu, top, value);
	size ++;
	j32_set_fast_stack_size_top(cpu, size, top);
#else
	j32_push_word_memory(cpu, value);
#endif
}

uint32_t j32_pop_word(arm_state_t * cpu)
{
#if J32_EMULATE_INTERNALS
	unsigned top, size;
	size = j32_get_fast_stack_size(cpu);
	if(size == 0)
	{
		j32_fill_fast_stack_size(cpu, 1);
		size = 1;
	}
	top = j32_get_fast_stack_top(cpu);
	uint32_t value = a32_register_get32(cpu, top);
	top = (top - 1) & 3;
	size --;
	j32_set_fast_stack_size_top(cpu, size, top);
	return value;
#else
	return j32_pop_word_memory(cpu);
#endif
}

uint32_t j32_peek_word(arm_state_t * cpu, size_t index)
{
#if J32_EMULATE_INTERNALS
	if(index <= 3)
	{
		unsigned size;
		size = j32_get_fast_stack_size(cpu);
		if(size <= index)
		{
			j32_fill_fast_stack_size(cpu, index + 1);
			size = index + 1;
		}
		return a32_register_get32(cpu, j32_get_fast_stack_element(cpu, index));
	}
	else
	{
		unsigned size;
		size = j32_get_fast_stack_size(cpu);
		uint32_t sp = a32_register_get32(cpu, J32_TOS);
		return a32_read32(cpu, sp - 4 * (1 + index - size));
	}
#else
	uint32_t sp = a32_register_get32(cpu, J32_TOS);
	return a32_read32(cpu, sp - 4 * (1 + index));
#endif
}

void j32_push_dword(arm_state_t * cpu, uint64_t value)
{
	/* TODO: endianness - this is big endian */
	j32_push_word(cpu, value);
	j32_push_word(cpu, value >> 32);
}

uint64_t j32_pop_dword(arm_state_t * cpu)
{
	/* TODO: endianness - this is big endian */
	uint64_t value = j32_pop_word(cpu);
	return (value << 32) | j32_pop_word(cpu);
}

void j32_push_float(arm_state_t * cpu, float value)
{
	j32_push_word(cpu, float_as_word(value));
}

uint32_t j32_pop_float(arm_state_t * cpu)
{
	return word_as_float(j32_pop_word(cpu));
}

void j32_push_double(arm_state_t * cpu, double value)
{
	j32_push_dword(cpu, double_as_dword(value));
}

double j32_pop_double(arm_state_t * cpu)
{
	return dword_as_double(j32_pop_dword(cpu));
}

uint32_t j32_load_const_word(arm_state_t * cpu, uint32_t offset)
{
	return a32_read32(cpu, a32_register_get32(cpu, J32_CP) + offset * 4);
}

uint32_t j32_load_const_dword(arm_state_t * cpu, uint32_t offset)
{
	return a32_read64(cpu, a32_register_get32(cpu, J32_CP) + offset * 4);
}

uint32_t j32_read_local_word(arm_state_t * cpu, uint32_t offset)
{
#if J32_EMULATE_INTERNALS
	if(offset == 0)
		return a32_register_get32(cpu, J32_LOC0);
	else
#endif
		return a32_read32(cpu, a32_register_get32(cpu, J32_LOC) + offset * 4);
}

uint64_t j32_read_local_dword(arm_state_t * cpu, uint32_t offset)
{
	return a32_read64(cpu, a32_register_get32(cpu, J32_LOC) + offset * 4);
}

void j32_write_local_word(arm_state_t * cpu, uint32_t offset, uint32_t value)
{
#if J32_EMULATE_INTERNALS
	if(offset == 0)
		a32_register_set32(cpu, J32_LOC0, value);
#endif
	a32_write32(cpu, a32_register_get32(cpu, J32_LOC) + offset * 4, value);
}

void j32_write_local_dword(arm_state_t * cpu, uint32_t offset, uint64_t value)
{
	a32_write64(cpu, a32_register_get32(cpu, J32_LOC) + offset * 4, value);
}

static inline uint32_t j32_get_array_length(arm_state_t * cpu, uint32_t array)
{
	if(array == 0)
		j32_break(cpu, J32_EXCEPTION_NULLPTR);

	uint32_t length_offset = (cpu->jaolr & JAOLR_LENGTH_OFF_MASK) >> JAOLR_LENGTH_OFF_SHIFT;
	if((cpu->jaolr & JAOLR_LENGTH_SUB))
		length_offset = array - length_offset;
	else
		length_offset = array + length_offset;
	uint32_t length_value = a32_read32(cpu, length_offset);
	// TODO: this cannot work for non-object types, requires more research
//	uint32_t shift_value = (cpu->jaolr & JAOLR_LENSHIFT_MASK) >> JAOLR_LENSHIFT_SHIFT;
//	return length_value >> shift_value;
	return length_value;
}

static inline uint32_t j32_get_array_element_start_address(arm_state_t * cpu, uint32_t array)
{
	uint32_t element_offset = (cpu->jaolr & JAOLR_ELEMENT_OFF_MASK) >> JAOLR_ELEMENT_OFF_SHIFT;
	element_offset += array;
	if(!(cpu->joscr & JOSCR_FLAT_ARRAY))
	{
		element_offset = a32_read32(cpu, element_offset);
	}
	return element_offset;
}

static inline uint32_t j32_get_array_element_address(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t start = j32_get_array_element_start_address(cpu, array);
	uint32_t shift_value = (cpu->jaolr & JAOLR_LENSHIFT_MASK) >> JAOLR_LENSHIFT_SHIFT;
	// It is unclear how far the elements of an array are, but we will assume they are shift_value apart
	// TODO: what about byte/... arrays? this is probably wrong, and it can only work for Object array
	return start + (index << shift_value);
}

static inline uint32_t j32_get_array_byte(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index;
	return a32_read8(cpu, address);
}

static inline uint32_t j32_get_array_hword(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 2;
	return a32_read16(cpu, address);
}

static inline uint32_t j32_get_array_word(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 4;
	return a32_read32(cpu, address);
}

static inline uint64_t j32_get_array_dword(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 8;
	return a32_read64(cpu, address);
}

static inline uint64_t j32_get_array_reference(arm_state_t * cpu, uint32_t array, uint32_t index)
{
	uint32_t address = j32_get_array_element_address(cpu, array, index);
	if(address == 0)
		return 0;
	return a32_read32(cpu, address);
}

static inline void j32_set_array_byte(arm_state_t * cpu, uint32_t array, uint32_t index, uint32_t value)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index;
	a32_write8(cpu, address, value);
}

static inline void j32_set_array_hword(arm_state_t * cpu, uint32_t array, uint32_t index, uint32_t value)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 2;
	a32_write16(cpu, address, value);
}

static inline void j32_set_array_word(arm_state_t * cpu, uint32_t array, uint32_t index, uint32_t value)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 4;
	a32_write32(cpu, address, value);
}

static inline void j32_set_array_dword(arm_state_t * cpu, uint32_t array, uint32_t index, uint64_t value)
{
	if(index >= j32_get_array_length(cpu, array))
	{
		j32_break(cpu, J32_EXCEPTION_OUT_OF_BOUNDS);
	}

	uint32_t address = j32_get_array_element_start_address(cpu, array) + index * 8;
	a32_write64(cpu, address, value);
}

static inline void j32_set_array_reference(arm_state_t * cpu, uint32_t array, uint32_t index, uint64_t value)
{
	uint32_t address = j32_get_array_element_address(cpu, array, index);
	if(address == 0)
		return;
	a32_write32(cpu, address, value);
}

