
/* The core CPU emulation */

#include <assert.h>
#include <byteswap.h>
#include <endian.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "emu.h"
#include "jazelle.h"

/*
	Operating modes
		ARMv1 - ARM26
		ARMv2 - ARM26
		ARMv3 - ARM32, ARM26
		ARMv4 - ARM32, Thumb
		ARMv5 - ARM32, Thumb, Java
		ARMv6 - ARM32, Thumb-2, Java
		ARMv7 - ARM32, Thumb-2, ThumbEE
		ARMv8 - ARM32, Thumb-2, ARM64
*/

enum
{
	A32_VECTOR_RESET          = 0x00000000,
	A32_VECTOR_UNDEFINED      = 0x00000004,
	A32_VECTOR_SWI            = 0x00000008,
	A32_VECTOR_PREFETCH_ABORT = 0x0000000C,
	A32_VECTOR_DATA_ABORT     = 0x00000010,
	A32_VECTOR_ADDRESS        = 0x00000014,
	A32_VECTOR_IRQ            = 0x00000018,
	A32_VECTOR_FIQ            = 0x0000001C,

	A64_VECTOR_SYNCHRONOUS    = 0x000,
	A64_VECTOR_IRQ            = 0x080,
	A64_VECTOR_FIQ            = 0x100,
	A64_VECTOR_SERROR         = 0x180,
};

/*
	An AARCH32 processor can be
	* 26-bit processor (ARMv1, ARMv2)
	* 32-bit processor (ARMv3 or later)

	A 32-bit processor can have
	* a 26-bit configuration (SCTLR.P cleared), with SCTLR.D cleared or set
	* a 32-bit configuration (SCTLR.P set), with SCTLR.D always set, always the case on ARMv3G, ARMv4 or later

	A 32-bit configuration can be in
	* 26-bit mode (CPSR.M4 cleared)
	* 32-bit mode (CPSR.M4 set), always the case on ARMv3G, ARMv4 or later
*/

bool is_supported_isa(arm_state_t * cpu, arm_instruction_set_t isa)
{
	switch(isa)
	{
	case ISA_AARCH26:
		return cpu->config.features & (1 << FEATURE_ARM26);
	case ISA_AARCH32:
		return cpu->config.features & (1 << FEATURE_ARM32);
	case ISA_THUMB32:
		return cpu->config.features & (1 << FEATURE_THUMB);
	case ISA_JAZELLE:
		return cpu->config.features & (1 << FEATURE_JAZELLE);
	case ISA_THUMBEE:
		return (cpu->config.features & (1 << FEATURE_THUMB)) && (cpu->config.version == ARMV7);
	case ISA_AARCH64:
		return cpu->config.features & (1 << FEATURE_ARM64);
	default:
		assert(false);
	}
}

/* Convenience functions */

void arm_set_isa(arm_state_t * cpu, arm_instruction_set_t isa)
{
	switch(isa)
	{
	case ISA_AARCH26:
		cpu->pstate.rw = PSTATE_RW_26;
		cpu->pstate.jt = PSTATE_JT_ARM;
		break;
	case ISA_AARCH32:
		cpu->pstate.rw = PSTATE_RW_32;
		cpu->pstate.jt = PSTATE_JT_ARM;
		cpu->sctlr_el1 = SCTLR_P | SCTLR_D;
		break;
	case ISA_THUMB32:
		cpu->pstate.rw = PSTATE_RW_32;
		cpu->pstate.jt = PSTATE_JT_THUMB;
		break;
	case ISA_JAZELLE:
		cpu->pstate.rw = PSTATE_RW_32;
		cpu->pstate.jt = PSTATE_JT_JAZELLE;
		break;
	case ISA_THUMBEE:
		cpu->pstate.rw = PSTATE_RW_32;
		cpu->pstate.jt = PSTATE_JT_THUMBEE;
		break;
	case ISA_AARCH64:
		cpu->pstate.rw = PSTATE_RW_64;
		cpu->pstate.jt = PSTATE_JT_ARM;
		break;
	}
}

arm_instruction_set_t arm_get_current_instruction_set(arm_state_t * cpu)
{
	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
		return ISA_AARCH26;
	case PSTATE_RW_32:
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			return ISA_AARCH32;
		case PSTATE_JT_THUMB:
			return ISA_THUMB32;
		case PSTATE_JT_JAZELLE:
			return ISA_JAZELLE;
		case PSTATE_JT_THUMBEE:
			return ISA_THUMBEE;
		}
		assert(false);
	case PSTATE_RW_64:
		return ISA_AARCH64;
	}
	assert(false);
}

static inline bool a32_is_prog26(arm_state_t * cpu)
{
	if(!is_supported_isa(cpu, ISA_AARCH26))
		return false;
	else if(!is_supported_isa(cpu, ISA_AARCH32))
		return true;
	else
		// bit redefined in ARMv8
		return cpu->config.version < ARMV8 && !(cpu->sctlr_el1 & SCTLR_P);
}

static inline bool a32_is_data26(arm_state_t * cpu)
{
	if(!is_supported_isa(cpu, ISA_AARCH26))
		return false;
	else if(!is_supported_isa(cpu, ISA_AARCH32))
		return true;
	else
		// bit redefined in ARMv8
		return cpu->config.version < ARMV8 && !(cpu->sctlr_el1 & SCTLR_D);
}

uint32_t a32_get_cpsr(arm_state_t * cpu)
{
	uint32_t cpsr = cpu->pstate.mode | (cpu->pstate.rw & 1 ? CPSR_M4 : 0) | (cpu->pstate.f ? CPSR_F : 0) | (cpu->pstate.i ? CPSR_I : 0)
		| (cpu->pstate.v ? CPSR_V : 0) | (cpu->pstate.c ? CPSR_C : 0) | (cpu->pstate.z ? CPSR_Z : 0) | (cpu->pstate.n ? CPSR_N : 0);
	if((cpu->config.features & (1 << FEATURE_THUMB)))
		cpsr |= cpu->pstate.jt & 1 ? CPSR_T : 0;
	if((cpu->config.features & (1 << FEATURE_JAZELLE)) || cpu->config.version == ARMV7)
		cpsr |= cpu->pstate.jt & 2 ? CPSR_J : 0;
	if((cpu->config.features & (1 << FEATURE_ENH_DSP)))
		cpsr |= cpu->pstate.q ? CPSR_Q : 0;
	if((cpu->config.version >= ARMV6))
		cpsr |= (cpu->pstate.a ? CPSR_A : 0) | (cpu->pstate.e ? CPSR_E : 0) | (cpu->pstate.ge << CPSR_GE_SHIFT);
	if((cpu->config.features & (1 << FEATURE_THUMB2)))
		cpsr |= ((cpu->pstate.it << CPSR_IT0_SHIFT) & CPSR_IT0_MASK) | ((cpu->pstate.it << CPSR_IT1_SHIFT) & CPSR_IT1_MASK);
	if((cpu->config.version >= ARMV81))
		cpsr |= cpu->pstate.pan ? CPSR_PAN : 0;
	//if((cpu->config.version >= ARMV82))
	//	cpsr |= cpu->pstate.uao ? CPSR_UAO : 0;
	return cpsr;
}

void a32_set_cpsr(arm_state_t * cpu, uint32_t mask, uint32_t cpsr)
{
	if((mask & CPSR_M4) && (cpu->config.features & (1 << FEATURE_ARM26)) && (cpu->config.features & (1 << FEATURE_ARM32))
		&& !a32_is_prog26(cpu)) // impossible to switch to 32-bit mode on a 26-bit configuration
	{
		cpu->pstate.rw = cpsr & CPSR_M4 ? PSTATE_RW_32 : PSTATE_RW_26;
	}

	if((mask & CPSR_MODE_MASK))
		cpu->pstate.mode = (cpu->pstate.mode & ~CPSR_MODE_MASK) | (cpsr & CPSR_MODE_MASK);
	if(cpu->pstate.rw == PSTATE_RW_26)
		cpu->pstate.mode &= 3;

	if((mask & CPSR_F))
		cpu->pstate.f = cpsr & CPSR_F ? 1 : 0;
	if((mask & CPSR_I))
		cpu->pstate.i = cpsr & CPSR_I ? 1 : 0;
	if((mask & CPSR_N))
		cpu->pstate.n = cpsr & CPSR_N ? 1 : 0;
	if((mask & CPSR_C))
		cpu->pstate.c = cpsr & CPSR_C ? 1 : 0;
	if((mask & CPSR_Z))
		cpu->pstate.z = cpsr & CPSR_Z ? 1 : 0;
	if((mask & CPSR_V))
		cpu->pstate.v = cpsr & CPSR_V ? 1 : 0;
	if((mask & (CPSR_T | CPSR_J)))
	{
		if((mask & CPSR_T))
			cpu->pstate.jt = (cpu->pstate.jt & 2) | (cpsr & CPSR_T ? 1 : 0);
		if((mask & CPSR_J))
			cpu->pstate.jt = (cpu->pstate.jt & 1) | (cpsr & CPSR_J ? 2 : 0);
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			if(!(cpu->config.features & ((1 << FEATURE_ARM26) | (1 << FEATURE_ARM32))))
				cpu->pstate.jt = PSTATE_JT_THUMB;
			break;
		case PSTATE_JT_THUMB:
			if(!(cpu->config.features & (1 << FEATURE_THUMB)))
				cpu->pstate.jt = PSTATE_JT_ARM;
			break;
		case PSTATE_JT_JAZELLE:
			if(!(cpu->config.features & (1 << FEATURE_JAZELLE)))
				cpu->pstate.jt = PSTATE_JT_ARM;
			break;
		case PSTATE_JT_THUMBEE:
			if(cpu->config.version != ARMV7)
				cpu->pstate.jt = PSTATE_JT_THUMB;
			break;
		}
	}
	if((mask & CPSR_Q) && (cpu->config.features & (1 << FEATURE_ENH_DSP)))
		cpu->pstate.q = cpsr & CPSR_Q ? 1 : 0;
	if((mask & CPSR_A) && cpu->config.version >= ARMV6)
		cpu->pstate.a = cpsr & CPSR_A ? 1 : 0;
	if((mask & CPSR_E) && cpu->config.version >= ARMV6)
		cpu->pstate.e = cpsr & CPSR_E ? 1 : 0;
	if((mask & CPSR_GE_MASK) && cpu->config.version >= ARMV6)
		cpu->pstate.ge = (cpu->pstate.ge & ((~mask & CPSR_GE_MASK) >> CPSR_GE_SHIFT)) | ((cpsr & mask & CPSR_GE_MASK) >> CPSR_GE_SHIFT);
	if((mask & (CPSR_IT0_MASK | CPSR_IT1_MASK)) && (cpu->config.features & (1 << FEATURE_THUMB2)))
		cpu->pstate.it = (cpu->pstate.it & (((~mask & CPSR_IT0_MASK) >> CPSR_IT0_SHIFT) | ((~mask & CPSR_IT1_MASK) >> CPSR_IT1_SHIFT)))
			| ((cpsr & mask & CPSR_IT0_MASK) >> CPSR_IT0_SHIFT) | ((cpsr & mask & CPSR_IT1_MASK) >> CPSR_IT1_SHIFT);
	if((mask & CPSR_PAN) && (cpu->config.version >= ARMV81))
		cpu->pstate.pan = cpsr & CPSR_PAN ? 1 : 0;
	if((mask & CPSR_UAO) && (cpu->config.version >= ARMV82))
		cpu->pstate.uao = cpsr & CPSR_UAO ? 1 : 0;
}

uint32_t a64_get_cpsr(arm_state_t * cpu)
{
	uint32_t cpsr = cpu->pstate.sp | (cpu->pstate.el << CPSR_EL_SHIFT) | (cpu->pstate.rw & 1 ? CPSR_M4 : 0) | (cpu->pstate.f ? CPSR_F : 0) | (cpu->pstate.i ? CPSR_I : 0) | (cpu->pstate.a ? CPSR_A : 0) | (cpu->pstate.d ? CPSR_D : 0)
		| (cpu->pstate.v ? CPSR_V : 0) | (cpu->pstate.c ? CPSR_C : 0) | (cpu->pstate.z ? CPSR_Z : 0) | (cpu->pstate.n ? CPSR_N : 0);
	if((cpu->config.version >= ARMV81))
		cpsr |= cpu->pstate.pan ? CPSR_PAN : 0;
	if((cpu->config.version >= ARMV82))
		cpsr |= cpu->pstate.uao ? CPSR_UAO : 0;
	return cpsr;
}

void a64_set_cpsr(arm_state_t * cpu, uint32_t cpsr)
{
	if((cpsr & CPSR_M4))
	{
		cpu->pstate.rw = PSTATE_RW_32;
		// TODO: check for invalid value
		cpu->pstate.mode = cpsr & CPSR_MODE_MASK;
		cpu->pstate.jt = ((cpsr & CPSR_T) >> CPSR_T_SHIFT) | (cpsr & CPSR_J) >> CPSR_J_SHIFT;
		cpu->pstate.e = cpsr & CPSR_E ? 1 : 0;
		cpu->pstate.ge = (cpsr & CPSR_GE_MASK) >> CPSR_GE_SHIFT;
		cpu->pstate.it = ((cpsr & CPSR_IT0_MASK) >> CPSR_IT0_SHIFT) | ((cpsr & CPSR_IT1_MASK) >> CPSR_IT1_SHIFT);
		cpu->pstate.q = cpsr & CPSR_Q ? 1 : 0;
	}
	else
	{
		cpu->pstate.rw = PSTATE_RW_64;
		cpu->pstate.el = (cpsr & CPSR_EL_MASK) >> CPSR_EL_SHIFT;
		cpu->pstate.sp = cpsr & CPSR_SP;
		cpu->pstate.d = cpsr & CPSR_D ? 1 : 0;
	}

	cpu->pstate.f = cpsr & CPSR_F ? 1 : 0;
	cpu->pstate.i = cpsr & CPSR_I ? 1 : 0;
	cpu->pstate.a = cpsr & CPSR_A ? 1 : 0;

	cpu->pstate.il = cpsr & CPSR_IL ? 1 : 0;
	cpu->pstate.ss = cpsr & CPSR_SS ? 1 : 0; // TODO

	if(cpu->config.version >= ARMV81)
		cpu->pstate.pan = cpsr & CPSR_PAN ? 1 : 0;
}

bool a32_is_arm26(arm_state_t * cpu)
{
	// assuming it is interpreting AArch32 instructions
	return cpu->pstate.rw == PSTATE_RW_26;
}

static inline bool t32_is_thumbee(arm_state_t * cpu)
{
	// assuming it is interpreting Thumb instructions
	return cpu->pstate.jt == PSTATE_JT_THUMBEE;
}

arm_endianness_t a32_get_instruction_endianness(arm_state_t * cpu)
{
	if((cpu->sctlr_el1 & SCTLR_B))
		return ARM_ENDIAN_SWAPPED; // v5 & v6
	else
		return ARM_ENDIAN_LITTLE;
}

arm_endianness_t a32_get_data_endianness(arm_state_t * cpu)
{
	if((cpu->sctlr_el1 & SCTLR_B))
		return ARM_ENDIAN_SWAPPED; // v5 & v6
	else if(cpu->pstate.e)
		return ARM_ENDIAN_BIG; // v6 & v7
	else
		return ARM_ENDIAN_LITTLE;
}

static inline arm_endianness_t a64_get_data_endianness(arm_state_t * cpu)
{
	switch(cpu->pstate.el)
	{
	case 0:
		return cpu->sctlr_el1 & SCTLR_E0E ? ARM_ENDIAN_BIG : ARM_ENDIAN_LITTLE;
	case 1:
		return cpu->sctlr_el1 & SCTLR_EE ? ARM_ENDIAN_BIG : ARM_ENDIAN_LITTLE;
	case 2:
		return cpu->sctlr_el2 & SCTLR_EE ? ARM_ENDIAN_BIG : ARM_ENDIAN_LITTLE;
	case 3:
		return cpu->sctlr_el3 & SCTLR_EE ? ARM_ENDIAN_BIG : ARM_ENDIAN_LITTLE;
	default:
		assert(false);
	}
}

arm_endianness_t arm_get_instruction_endianness(arm_state_t * cpu)
{
	if(cpu->pstate.rw == PSTATE_RW_64)
		return ARM_ENDIAN_LITTLE;
	else
		return a32_get_instruction_endianness(cpu);
}

static const regnum_t a32_register_for_mode[] =
{
	[MODE_USR * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,      13,      14,     PC,
	[MODE_FIQ * 16] = 0,    1,    2,    3,    4,    5,    6,    7,   R8_FIQ, R9_FIQ, R10_FIQ, R11_FIQ, R12_FIQ, R13_FIQ, R14_FIQ, PC,
	[MODE_IRQ * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_IRQ, R14_IRQ, PC,
	[MODE_SVC * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_SVC, R14_SVC, PC,
	[       4 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[       5 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[MODE_MON * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_MON, R14_MON, PC,
	[MODE_ABT * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_ABT, R14_ABT, PC,
	[       8 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[       9 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[MODE_HYP * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_HYP,  14,     PC,
	[MODE_UND * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_UND, R14_UND, PC,
	[      12 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[      13 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[      14 * 16] = NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,  NONE,   NONE,    NONE,    NONE,    NONE,    NONE,    NONE,
	[MODE_SYS * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,      13,      14,     PC,

	// ARMv1
	[MODE_USR * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,      13,      14,     PC,
	[MODE_FIQ * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,     R10_FIQ, R11_FIQ, R12_FIQ, R13_FIQ, R14_FIQ, PC,
	[MODE_IRQ * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_IRQ, R14_IRQ, PC,
	[MODE_SVC * 16] = 0,    1,    2,    3,    4,    5,    6,    7,    8,      9,      10,      11,      12,     R13_SVC, R14_SVC, PC,
};

#define a32_register_mode(_cpu, _regnum, _mode) ((_cpu)->r[a32_register_for_mode[(_mode) * 16 + (_regnum) + ((_cpu)->config.version == ARMV1 ? 256 : 0)]])

static const uint32_t isa_cpsr_settings[] =
{
	[ISA_AARCH26] = 0,
	[ISA_AARCH32] = CPSR_M4,
	[ISA_THUMB32] = CPSR_M4 | CPSR_T,
	[ISA_JAZELLE] = CPSR_M4          | CPSR_J,
	[ISA_THUMBEE] = CPSR_M4 | CPSR_T | CPSR_J,
	[ISA_AARCH64] = 0,
};

static inline uint32_t get_xpsr_mask(arm_version_t version, uint32_t features, uint16_t supported_isas)
{
	uint32_t mask = 0;
	switch(version)
	{
	case ARMV9:
	case ARMV83:
	case ARMV82:
		mask |= CPSR_UAO;
	case ARMV81:
		mask |= CPSR_PAN;
	case ARMV8:
		mask |= CPSR_SS | CPSR_IL;
	case ARMV7:
	case ARMV6:
		mask |= CPSR_GE_MASK | CPSR_E | CPSR_A;
	case ARMV5:
	case ARMV4:
	case ARMV3:
		mask |= CPSR_MODE_MASK;
	case ARMV2:
	case ARMV1:
		mask |= CPSR_N | CPSR_Z | CPSR_C | CPSR_V | CPSR_I | CPSR_F | CPSR_A26_MODE_MASK;
	}

	if((features & (1 << FEATURE_THUMB2)))
		mask |= CPSR_IT_MASK;

	if((features & (1 << FEATURE_ENH_DSP)))
		mask |= CPSR_Q;

	for(arm_instruction_set_t isa = ISA_START; isa < ISA_END; isa++)
	{
		if((supported_isas & (1 << isa)))
			mask |= isa_cpsr_settings[isa];
	}

	return mask;
}

static inline uint32_t get_xpsr_always_set(arm_version_t version, uint32_t features, uint16_t supported_isas)
{
	uint32_t value = CPSR_M4 | CPSR_T | CPSR_J;

	for(arm_instruction_set_t isa = ISA_START; isa < ISA_END; isa++)
	{
		if((supported_isas & (1 << isa)))
			value &= ~isa_cpsr_settings[isa];
	}

	return value;
}

static inline void set_fpscr_bits(arm_state_t * cpu, uint32_t flags, uint32_t value)
{
	cpu->vfp.fpscr = (cpu->vfp.fpscr & ~flags) | (value & flags);
}

static inline uint32_t get_cpsr_spsr_mask(bool bits24, bool bits16, bool bits8, bool bits0)
{
	uint32_t mask = 0;
	if(bits24)
		mask |= 0xFF000000;
	if(bits16)
		mask |= 0x00FF0000;
	if(bits8)
		mask |= 0x0000FF00;
	if(bits0)
		mask |= 0x000000FF;
	return mask;
}

static const regnum_t a32_spsr_for_mode[] =
{
	[MODE_USR] = NONE,
	[MODE_FIQ] = SPSR_FIQ,
	[MODE_IRQ] = SPSR_IRQ,
	[MODE_SVC] = SPSR_SVC,
	[4] = NONE,
	[5] = NONE,
	[MODE_MON] = SPSR_MON,
	[MODE_ABT] = SPSR_ABT,
	[8] = NONE,
	[9] = NONE,
	[MODE_HYP] = SPSR_MON,
	[MODE_UND] = SPSR_UND,
	[12] = NONE,
	[13] = NONE,
	[14] = NONE,
	[MODE_SYS] = NONE,
};

#define a32_spsr_mode(_cpu, _mode) ((_cpu)->r[a32_spsr_for_mode[(_mode) & 0xF]])
#define a32_spsr(_cpu) (a32_spsr_mode((_cpu), (_cpu)->pstate.mode))
#define a32_spsr_mode_valid(_cpu, _mode) (a32_spsr_for_mode[(_mode) & 0xF] != NONE)
#define a32_spsr_valid(_cpu) (a32_spsr_mode_valid((_cpu), (_cpu)->pstate.mode))

static inline void a32_set_spsr(arm_state_t * cpu, uint32_t mask, uint32_t value)
{
	if(!a32_spsr_valid(cpu))
		return;
	uint64_t * spsr = &a32_spsr(cpu);

	mask &= get_xpsr_mask(cpu->config.version, cpu->config.features, cpu->supported_isas);
	value |= get_xpsr_always_set(cpu->config.version, cpu->config.features, cpu->supported_isas);

	*spsr = (*spsr & ~mask) | (value & mask);
}

#define a32_register(_cpu, _regnum) a32_register_mode((_cpu), (_regnum), (_cpu)->pstate.mode)

static inline uint32_t a32_get_spsr(arm_state_t * cpu)
{
	if(!a32_spsr_valid(cpu))
		return 0;
	return a32_spsr(cpu);
}

static inline uint32_t a26_get_pc(arm_state_t * cpu)
{
	return (cpu->r[PC] & 0x03FFFFFC)
		| (cpu->pstate.mode & 3) | (cpu->pstate.f ? CPSR_A26_F : 0) | (cpu->pstate.i ? CPSR_A26_I : 0)
		| (cpu->pstate.v ? CPSR_V : 0) | (cpu->pstate.c ? CPSR_C : 0) | (cpu->pstate.z ? CPSR_Z : 0) | (cpu->pstate.n ? CPSR_N : 0);
}

static inline void a32_set_cpsr_nzcv(arm_state_t * cpu, uint32_t value)
{
	cpu->pstate.v = value & CPSR_V ? 1 : 0;
	cpu->pstate.c = value & CPSR_C ? 1 : 0;
	cpu->pstate.z = value & CPSR_Z ? 1 : 0;
	cpu->pstate.n = value & CPSR_N ? 1 : 0;
}

static inline void a32_set_pc(arm_state_t * cpu, uint32_t value)
{
	switch(cpu->pstate.jt)
	{
	case PSTATE_JT_ARM:
		// ARM mode
		cpu->r[PC] = value & 0xFFFFFFFC;
		break;
	case PSTATE_JT_THUMB:
	case PSTATE_JT_THUMBEE:
		// Thumb or ThumbEE mode
		cpu->r[PC] = value & 0xFFFFFFFE;
		break;
	case PSTATE_JT_JAZELLE:
		// Jazelle mode
		cpu->r[PC] = value;
		break;
	}
}

static inline void a32_set_pc_mode(arm_state_t * cpu, uint32_t value)
{
	switch(cpu->pstate.jt)
	{
	case PSTATE_JT_ARM:
	case PSTATE_JT_THUMB:
		// ARM or Thumb mode, switch to ARM or Thumb mode
		cpu->pstate.jt = value & 1 ? PSTATE_JT_THUMB : PSTATE_JT_ARM;
		break;
	case PSTATE_JT_JAZELLE:
	case PSTATE_JT_THUMBEE:
		// ThumbEE or Jazelle mode, stay
		break;
	}
	a32_set_pc(cpu, value);
}

uint32_t a32_register_get32(arm_state_t * cpu, int regnum)
{
	regnum &= 0xF;
	uint32_t value = a32_register(cpu, regnum) & 0xFFFFFFFF;
	if(regnum == A32_PC_NUM)
	{
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			value += 4;
			break;
		case PSTATE_JT_THUMB:
		case PSTATE_JT_THUMBEE:
			value += 2;
			break;
		}
	}
	return value;
}

uint32_t a32_register_get32_lhs(arm_state_t * cpu, int regnum)
{
	regnum &= 0xF;
	if(cpu->pstate.rw == PSTATE_RW_26 && regnum == A32_PC_NUM)
	{
		return a26_get_pc(cpu) + 4;
	}
	else
	{
		uint32_t value = a32_register(cpu, regnum) & 0xFFFFFFFF;
		if(regnum == A32_PC_NUM)
		{
			switch(cpu->pstate.jt)
			{
			case PSTATE_JT_ARM:
				value += 4;
				break;
			case PSTATE_JT_THUMB:
			case PSTATE_JT_THUMBEE:
				value += 2;
				break;
			}
		}
		return value;
	}
}

// when using R15 in an MCR instruction
static inline uint32_t a32_get_mcr_offset_for_pc(arm_state_t * cpu)
{
	if(cpu->config.version >= ARMV5)
		return 0;
	else
		return 4; // TODO: implementation defined, this is for ARMv2
}

// when using R15 in an instruction with a shift by shift
static inline uint32_t a32_get_register_shift_offset_for_pc(arm_state_t * cpu, uint32_t opcode)
{
	if((opcode & 0x020000F0) != 0x00000010)
		return 0;
	else if(cpu->config.version >= ARMV5)
		return 0;
	else
		return 4; // TODO: implementation defined, this is for ARMv2
}

// when using R15 with STR/STM
static inline uint32_t a32_get_stored_pc_displacement(arm_state_t * cpu)
{
	if(cpu->config.version >= ARMV7)
		return 0;
	else
		return 4; // TODO: implementation defined, this is for ARMv2
}

uint32_t a32_register_get32_str(arm_state_t * cpu, int regnum)
{
	regnum &= 0xF;
	uint32_t value = a32_register(cpu, regnum) & 0xFFFFFFFF;
	if(regnum == A32_PC_NUM)
	{
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			value += 4;
			break;
		case PSTATE_JT_THUMB:
		case PSTATE_JT_THUMBEE:
			value += 2;
			break;
		}
		value += a32_get_stored_pc_displacement(cpu);
	}
	return value;
}

void a32_register_set32(arm_state_t * cpu, int regnum, uint32_t value)
{
	regnum &= 0xF;
	if(regnum == A32_PC_NUM)
	{
		if(cpu->pstate.rw == PSTATE_RW_26)
		{
			cpu->r[PC] = value & 0x03FFFFFC;
		}
		else
		{
			a32_set_pc(cpu, value);
		}
	}
	else
	{
		a32_register(cpu, regnum) = value;
	}
}

/* this function permits switching ARM/Thumb modes when the assignment target is the PC register */
static inline void a32_register_set32_interworking(arm_state_t * cpu, int regnum, uint32_t value)
{
	regnum &= 0xF;
	if(regnum == A32_PC_NUM)
	{
		if(cpu->pstate.rw == PSTATE_RW_26)
		{
			cpu->r[PC] = value & 0x03FFFFFC;
		}
		else
		{
			a32_set_pc_mode(cpu, value);
		}
	}
	else
	{
		a32_register(cpu, regnum) = value;
	}
}

/* on v5 and later, LDR/LDM instructions assigning to R15 may switch between ARM/Thumb modes according to the least significant bit */
static inline void a32_register_set32_interworking_v5(arm_state_t * cpu, int regnum, uint32_t value)
{
	if(cpu->config.version < ARMV5)
		a32_register_set32(cpu, regnum, value);
	else
		a32_register_set32_interworking(cpu, regnum, value);
}

/* on v7, arithmetic instructions assigning to R15 may switch between ARM/Thumb modes according to the least significant bit if the CPSR bits are not changed */
static inline void a32_register_set32_interworking_v7(arm_state_t * cpu, int regnum, uint32_t value)
{
	if(cpu->config.version < ARMV7)
		a32_register_set32(cpu, regnum, value);
	else
		a32_register_set32_interworking(cpu, regnum, value);
}

uint32_t a64_register_get32(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp)
{
	regnum &= 0x1F;
	if(regnum != A64_SP)
	{
		return cpu->r[regnum] & 0xFFFFFFFF;
	}
	else if(suppress_sp)
	{
		return 0;
	}
	else if(!cpu->pstate.sp)
	{
		return cpu->r[SP_EL0] & 0xFFFFFFFF;
	}
	else
	{
		return cpu->r[SP_EL0 + cpu->pstate.el] & 0xFFFFFFFF;
	}
}

void a64_register_set32(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp, uint32_t value)
{
	regnum &= 0x1F;
	if(regnum != A64_SP)
	{
		cpu->r[regnum] = value;
	}
	else if(suppress_sp)
	{
	}
	else if(!cpu->pstate.sp)
	{
		cpu->r[SP_EL0] = value;
	}
	else
	{
		cpu->r[SP_EL0 + cpu->pstate.el] = value;
	}
}

uint64_t a64_register_get64(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp)
{
	regnum &= 0x1F;
	if(regnum != A64_SP)
	{
		return cpu->r[regnum];
	}
	else if(suppress_sp)
	{
		return 0;
	}
	else if(!cpu->pstate.sp)
	{
		return cpu->r[SP_EL0];
	}
	else
	{
		return cpu->r[SP_EL0 + cpu->pstate.el];
	}
}

void a64_register_set64(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp, uint64_t value)
{
	regnum &= 0x1F;
	if(regnum != A64_SP)
	{
		cpu->r[regnum] = value;
	}
	else if(suppress_sp)
	{
	}
	else if(!cpu->pstate.sp)
	{
		cpu->r[SP_EL0] = value;
	}
	else
	{
		cpu->r[SP_EL0 + cpu->pstate.el] = value;
	}
}

uint32_t arm_get_midr(arm_state_t * cpu)
{
	uint32_t value;

	if(cpu->part_number < ARM_PART_ARM3)
	{
		return 0;
	}
	else if(cpu->part_number < ARM_PART_ARM810)
	{
		value = (uint32_t)ARM_VENDOR_ARM << 24;
		if((cpu->part_number & 0xF000) == 0x7000)
		{
			value |= cpu->part_number;
			if((cpu->config.features & FEATURE_THUMB))
				value |= 0x00800000;
		}
		else
		{
			value |= cpu->part_number & 0x0FFF;
			value |= (uint32_t)ARM_MANUFACTURER_VLSI << 16; // manufacturer
		}
	}
	else
	{
		uint8_t variant;
		value = (uint32_t)cpu->vendor << 24;
		value |= cpu->part_number;
		switch(cpu->config.version)
		{
		case ARMV1:
		case ARMV2:
		case ARMV3:
			// invalid
			variant = 0;
			break;
		case ARMV4:
			if((cpu->config.features & FEATURE_THUMB))
				variant = ARM_ARCH_V4T;
			else
				variant = ARM_ARCH_V4;
			break;
		case ARMV5:
			if((cpu->config.features & FEATURE_JAZELLE))
				variant = ARM_ARCH_V5TEJ;
			else if((cpu->config.features & FEATURE_ENH_DSP))
				variant = ARM_ARCH_V5TE;
			else if((cpu->config.features & FEATURE_THUMB))
				variant = ARM_ARCH_V5T;
			else
				variant = ARM_ARCH_V5;
			break;
		case ARMV6:
			// unsure about other variants
			variant = ARM_ARCH_V6;
			break;
		default:
			variant = ARM_ARCH_CPUID;
			break;
		}
		value |= ((uint32_t)variant << 20) | 0x00F00000;
	}

	return value;
}

uint32_t arm_get_id_pfr0(arm_state_t * cpu)
{
	uint32_t value = 0;

	if((cpu->config.features & (1 << FEATURE_ARM26)) || (cpu->config.features & (1 << FEATURE_ARM32)))
		value |= 0x00000001;

	value |= cpu->config.thumb_implementation << 4;

	if(cpu->config.jazelle_implementation < 0)
		value |= 0 << 8;
	else if(cpu->config.jazelle_implementation <= 2)
		value |= cpu->config.jazelle_implementation << 8;
	else
		value |= 2 << 8;

	if(cpu->config.version == ARMV7)
		value |= 1 << 12; // ThumbEE

	return value;
}

/*
 * There are three byte ordering schemes for ARM processors: LE, BE32 and BE8
 * In LE and BE8, bytes are loaded in the correct order, and then interpreted as either a little-endian or big-endian value
 * In BE32 however, the bytes are read in reverse order: each read at address X return the byte at X^3 actual address
 *
 * To properly emulate this behavior, we will return all aligned 32-bit reads as the sequence of bytes, and parse it as little-endian
 * For shorter reads and unaligned reads however, we need to divide the read into chunks, calculate the actual addresses to be read
 * and then fill a buffer in a way that a little-endian parse will be able to handle
 *
 * (Note that BE32 does not support unaligned reads, so this behavior is not technically necessary)
 */

static inline bool memory_read_bytes16(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->read(cpu, address, buffer, 2, privileged_mode);
	}
	else
	{
		if((address & 3) != 3)
		{
			// return in the reversed order as to the actual read
			return memory->read(cpu, (address ^ 3) - 1, buffer, 2, privileged_mode);
		}
		else
		{
			// replicate reversed order
			return
				memory->read(cpu, address & ~3, &((char *)buffer)[1], 1, privileged_mode)
				&& memory->read(cpu, address + 4, &((char *)buffer)[0], 1, privileged_mode);
		}
	}
}

static inline bool memory_write_bytes16(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, const void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->write(cpu, address, buffer, 2, privileged_mode);
	}
	else
	{
		if((address & 3) != 3)
		{
			// return in the reversed order as to the actual write
			return memory->write(cpu, (address ^ 3) - 1, buffer, 2, privileged_mode);
		}
		else
		{
			// replicate reversed order
			return
				memory->write(cpu, address & ~3, &((char *)buffer)[1], 1, privileged_mode) &&
				memory->write(cpu, address + 4,  &((char *)buffer)[0], 1, privileged_mode);
		}
	}
}

static inline bool memory_read_bytes32(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->read(cpu, address, buffer, 4, privileged_mode);
	}
	else
	{
		if((address & 3) == 0)
		{
			// return in the reversed order as to the actual read
			return memory->read(cpu, address, buffer, 4, privileged_mode);
		}
		else
		{
			// replicate reversed order
			return
				memory->read(cpu, address & ~3,                       &((char *)buffer)[address & 3], 4 - (address & 3), privileged_mode) &&
				memory->read(cpu, address + 8 - ((address & 3) << 1), &((char *)buffer)[0],           address & 3,       privileged_mode);
		}
	}
}

static inline bool memory_write_bytes32(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, const void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->write(cpu, address, buffer, 4, privileged_mode);
	}
	else
	{
		if((address & 3) == 0)
		{
			// return in the reversed order as to the actual write
			return memory->write(cpu, address, buffer, 4, privileged_mode);
		}
		else
		{
			// replicate reversed order
			return
				memory->write(cpu, address & ~3,                       &((char *)buffer)[address & 3], 4 - (address & 3), privileged_mode) &&
				memory->write(cpu, address + 8 - ((address & 3) << 1), &((char *)buffer)[0],           address & 3,       privileged_mode);
		}
	}
}

static inline bool memory_read_bytes64(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->read(cpu, address, buffer, 8, privileged_mode);
	}
	else
	{
		// replicate reversed order
		if((address & 3) == 0)
		{
			return
				memory->read(cpu, address,     &((char *)buffer)[4], 4, privileged_mode) &&
				memory->read(cpu, address + 4, &((char *)buffer)[0], 4, privileged_mode);
		}
		else
		{
			return
				memory->read(cpu, address & ~3,                        &((char *)buffer)[4 + (address & 3)], 4 - (address & 3), privileged_mode) &&
				memory->read(cpu, (address & ~3) + 4,                  &((char *)buffer)[address & 3],       4,                 privileged_mode) &&
				memory->read(cpu, address + 12 - ((address & 3) << 1), &((char *)buffer)[0],                 address & 3,       privileged_mode);
		}
	}
}

static inline bool memory_write_bytes64(const memory_interface_t * memory, arm_state_t * cpu, uint32_t address, const void * buffer, arm_endianness_t endian, bool privileged_mode)
{
	if(endian != ARM_ENDIAN_SWAPPED)
	{
		return memory->write(cpu, address, buffer, 8, privileged_mode);
	}
	else
	{
		// replicate reversed order
		if((address & 3) == 0)
		{
			return
				memory->write(cpu, address,     &((char *)buffer)[4], 4, privileged_mode) &&
				memory->write(cpu, address + 4, &((char *)buffer)[0], 4, privileged_mode);
		}
		else
		{
			return
				memory->write(cpu, address & ~3,                        &((char *)buffer)[4 + (address & 3)], 4 - (address & 3), privileged_mode) &&
				memory->write(cpu, (address & ~3) + 4,                  &((char *)buffer)[address & 3],       4,                 privileged_mode) &&
				memory->write(cpu, address + 12 - ((address & 3) << 1), &((char *)buffer)[0],                 address & 3,       privileged_mode);
		}
	}
}

bool arm_is_privileged_mode(arm_state_t * cpu)
{
	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
	case PSTATE_RW_32:
		return cpu->pstate.mode != 0;
	case PSTATE_RW_64:
		return cpu->pstate.el != 0;
	default:
		assert(false);
	}
}

_Noreturn void arm_prefetch_abort(arm_state_t * cpu);
_Noreturn void arm_data_abort(arm_state_t * cpu);

static bool memory_read8(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint8_t * result, arm_endianness_t endian, bool privileged_mode)
{
	return memory->read(cpu, address ^ (endian == ARM_ENDIAN_SWAPPED ? 3 : 0), result, 1, privileged_mode);
}

uint8_t a64_read8(arm_state_t * cpu, uint64_t address)
{
	uint8_t value;
	if(!memory_read8(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint8_t a64_read8_user_mode(arm_state_t * cpu, uint64_t address)
{
	uint8_t value;
	if(!memory_read8(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

uint8_t a32_read8(arm_state_t * cpu, uint32_t address)
{
	uint8_t value;
	if(!memory_read8(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint8_t a32_read8_user_mode(arm_state_t * cpu, uint32_t address)
{
	uint8_t value;
	if(!memory_read8(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

// convenience function for emulation
uint8_t arm_memory_read8_data(arm_state_t * cpu, uint64_t address)
{
	uint8_t value;
	memory_read8(cpu->memory, NULL, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

bool memory_read16(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint16_t * result, arm_endianness_t endian, bool privileged_mode)
{
	if(!memory_read_bytes16(memory, cpu, address, result, endian, privileged_mode))
		return false;

	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		*result = le16toh(*result);
		break;
	case ARM_ENDIAN_BIG:
		*result = be16toh(*result);
		break;
	}
	return true;
}

uint16_t a64_read16(arm_state_t * cpu, uint64_t address)
{
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint16_t a64_read16_user_mode(arm_state_t * cpu, uint64_t address)
{
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

uint16_t a32_read16(arm_state_t * cpu, uint32_t address)
{
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint16_t a32_read16_user_mode(arm_state_t * cpu, uint32_t address)
{
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

// convenience function for emulation
uint16_t arm_memory_read16_data(arm_state_t * cpu, uint64_t address)
{
	uint16_t value;
	memory_read16(cpu->memory, NULL, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

bool memory_read32(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint32_t * result, arm_endianness_t endian, bool privileged_mode)
{
	if(!memory_read_bytes32(memory, cpu, address, result, endian, privileged_mode))
		return false;

	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		*result = le32toh(*result);
		break;
	case ARM_ENDIAN_BIG:
		*result = be32toh(*result);
		break;
	}
	return true;
}

uint32_t a64_read32(arm_state_t * cpu, uint64_t address)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint32_t a64_read32_user_mode(arm_state_t * cpu, uint64_t address)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

uint32_t a32_read32(arm_state_t * cpu, uint32_t address)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint32_t a32_read32_user_mode(arm_state_t * cpu, uint32_t address)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

// convenience function for emulation
uint32_t arm_memory_read32_data(arm_state_t * cpu, uint64_t address)
{
	uint32_t value;
	memory_read32(cpu->memory, NULL, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

bool memory_read64(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint64_t * result, arm_endianness_t endian, bool privileged_mode)
{
	if(!memory_read_bytes64(memory, cpu, address, result, endian, privileged_mode))
		return false;

	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		*result = le64toh(*result);
		break;
	case ARM_ENDIAN_BIG:
		*result = be64toh(*result);
		break;
	}
	return true;
}

uint64_t a64_read64(arm_state_t * cpu, uint64_t address)
{
	uint64_t value;
	if(!memory_read64(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint64_t a64_read64_user_mode(arm_state_t * cpu, uint64_t address)
{
	uint64_t value;
	if(!memory_read64(cpu->memory, cpu, address, &value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

uint64_t a32_read64(arm_state_t * cpu, uint32_t address)
{
	uint64_t value;
	if(!memory_read64(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
	return value;
}

uint64_t a32_read64_user_mode(arm_state_t * cpu, uint32_t address)
{
	uint64_t value;
	if(!memory_read64(cpu->memory, cpu, address, &value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
	return value;
}

// convenience function for emulation
uint64_t arm_memory_read64_data(arm_state_t * cpu, uint64_t address)
{
	uint64_t value;
	memory_read64(cpu->memory, NULL, address, &value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

bool memory_write8(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint8_t value, arm_endianness_t endian, bool privileged_mode)
{
	return memory->write(cpu, address ^ (endian == ARM_ENDIAN_SWAPPED ? 3 : 0), &value, 1, privileged_mode);
}

void a64_write8(arm_state_t * cpu, uint64_t address, uint8_t value)
{
	if(!memory_write8(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a64_write8_user_mode(arm_state_t * cpu, uint64_t address, uint8_t value)
{
	if(!memory_write8(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

void a32_write8(arm_state_t * cpu, uint32_t address, uint8_t value)
{
	if(!memory_write8(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a32_write8_user_mode(arm_state_t * cpu, uint32_t address, uint8_t value)
{
	if(!memory_write8(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

// direct access for emulation
void arm_memory_write8(const memory_interface_t * memory, uint64_t address, uint8_t value, arm_endianness_t endian)
{
	memory_write8(memory, NULL, address, value, endian, false);
}

// convenience function for emulation
void arm_memory_write8_data(arm_state_t * cpu, uint64_t address, uint8_t value)
{
	memory_write8(cpu->memory, NULL, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
}

bool memory_write16(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint16_t value, arm_endianness_t endian, bool privileged_mode)
{
	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		value = htole16(value);
		break;
	case ARM_ENDIAN_BIG:
		value = htobe16(value);
		break;
	}
	return memory_write_bytes16(memory, cpu, address, &value, endian, privileged_mode);
}

void a64_write16(arm_state_t * cpu, uint64_t address, uint16_t value)
{
	if(!memory_write16(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a64_write16_user_mode(arm_state_t * cpu, uint64_t address, uint16_t value)
{
	if(!memory_write16(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

void a32_write16(arm_state_t * cpu, uint32_t address, uint16_t value)
{
	if(!memory_write16(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a32_write16_user_mode(arm_state_t * cpu, uint32_t address, uint16_t value)
{
	if(!memory_write16(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

// direct access for emulation
void arm_memory_write16(const memory_interface_t * memory, uint64_t address, uint16_t value, arm_endianness_t endian)
{
	memory_write16(memory, NULL, address, value, endian, false);
}

// convenience function for emulation
void arm_memory_write16_data(arm_state_t * cpu, uint64_t address, uint16_t value)
{
	memory_write16(cpu->memory, NULL, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
}

bool memory_write32(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint32_t value, arm_endianness_t endian, bool privileged_mode)
{
	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		value = htole32(value);
		break;
	case ARM_ENDIAN_BIG:
		value = htobe32(value);
		break;
	}
	return memory_write_bytes32(memory, cpu, address, &value, endian, privileged_mode);
}

void a64_write32(arm_state_t * cpu, uint64_t address, uint32_t value)
{
	if(!memory_write32(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a64_write32_user_mode(arm_state_t * cpu, uint64_t address, uint32_t value)
{
	if(!memory_write32(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

void a32_write32(arm_state_t * cpu, uint32_t address, uint32_t value)
{
	if(!memory_write32(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a32_write32_user_mode(arm_state_t * cpu, uint32_t address, uint32_t value)
{
	if(!memory_write32(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

// direct access for emulation
void arm_memory_write32(const memory_interface_t * memory, uint64_t address, uint32_t value, arm_endianness_t endian)
{
	memory_write32(memory, NULL, address, value, endian, false);
}

// direct access for emulation
void arm_memory_write32_data(arm_state_t * cpu, uint64_t address, uint32_t value)
{
	memory_write32(cpu->memory, NULL, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
}

bool memory_write64(const memory_interface_t * memory, arm_state_t * cpu, uint64_t address, uint64_t value, arm_endianness_t endian, bool privileged_mode)
{
	switch(endian)
	{
	case ARM_ENDIAN_LITTLE:
	case ARM_ENDIAN_SWAPPED:
	default:
		value = htole64(value);
		break;
	case ARM_ENDIAN_BIG:
		value = htobe64(value);
		break;
	}
	return memory_write_bytes64(memory, cpu, address, &value, endian, privileged_mode);
}

void a64_write64(arm_state_t * cpu, uint64_t address, uint64_t value)
{
	if(!memory_write64(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a64_write64_user_mode(arm_state_t * cpu, uint64_t address, uint64_t value)
{
	if(!memory_write64(cpu->memory, cpu, address, value, a64_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

void a32_write64(arm_state_t * cpu, uint32_t address, uint64_t value)
{
	if(!memory_write64(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_data_abort(cpu);
}

void a32_write64_user_mode(arm_state_t * cpu, uint32_t address, uint64_t value)
{
	if(!memory_write64(cpu->memory, cpu, address, value, a32_get_data_endianness(cpu), false))
		arm_data_abort(cpu);
}

// direct access for emulation
void arm_memory_write64(const memory_interface_t * memory, uint64_t address, uint64_t value, arm_endianness_t endian)
{
	memory_write64(memory, NULL, address, value, endian, false);
}

// convenience function for emulation
void arm_memory_write64_data(arm_state_t * cpu, uint64_t address, uint64_t value)
{
	memory_write64(cpu->memory, NULL, address, value, a32_get_data_endianness(cpu), arm_is_privileged_mode(cpu));
}

static inline void j32_break(arm_state_t * cpu, uint32_t index);

// ARM26, ARM32
static inline uint32_t a32_fetch32(arm_state_t * cpu)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, cpu->r[PC] & ~3, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_prefetch_abort(cpu);
	cpu->r[PC] += 4;
	if(cpu->pstate.rw == PSTATE_RW_26)
		cpu->r[PC] &= 0x03FFFFFF; // note: this is only needed here, since 26-bit modes can only run in ARM mode
	return value;
}

_Noreturn void arm_unaligned_pc(arm_state_t * cpu);
// ARM64
static inline int32_t a64_fetch32(arm_state_t * cpu)
{
	if((cpu->r[PC] & 3) != 0)
		arm_unaligned_pc(cpu);
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, cpu->r[PC], &value, ARM_ENDIAN_LITTLE, arm_is_privileged_mode(cpu)))
		arm_prefetch_abort(cpu);
	cpu->r[PC] += 4;
	return value;
}

// for Thumb and ThumbEE
static inline uint16_t a32_fetch16(arm_state_t * cpu)
{
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, cpu->r[PC] & ~1, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		arm_prefetch_abort(cpu);
	cpu->r[PC] += 2;
	return value;
}

// for Jazelle
static inline int8_t j32_fetch8(arm_state_t * cpu)
{
	uint64_t pc = cpu->r[PC];
	uint8_t value;
	if(!memory_read8(cpu->memory, cpu, pc, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		j32_break(cpu, J32_EXCEPTION_PREFETCH_ABORT);
	cpu->r[PC] = pc + 1;
	return value;
}

static inline int16_t j32_fetch16(arm_state_t * cpu)
{
	uint64_t pc = cpu->r[PC];
	uint16_t value;
	if(!memory_read16(cpu->memory, cpu, pc, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		j32_break(cpu, J32_EXCEPTION_PREFETCH_ABORT);
	cpu->r[PC] = pc + 2;
	if(a32_get_instruction_endianness(cpu) == ARM_ENDIAN_LITTLE)
		return bswap_16(value);
	else
		return value;
}

// only for Jazelle
static inline int32_t j32_fetch32(arm_state_t * cpu)
{
	uint64_t pc = cpu->r[PC];
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, pc, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		j32_break(cpu, J32_EXCEPTION_PREFETCH_ABORT);
	cpu->r[PC] = pc + 4;
	if(a32_get_instruction_endianness(cpu) == ARM_ENDIAN_LITTLE)
		return bswap_32(value);
	else
		return value;
}

static inline int32_t j32_fetch32_from(arm_state_t * cpu, uint32_t offset)
{
	uint32_t value;
	if(!memory_read32(cpu->memory, cpu, offset, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu)))
		j32_break(cpu, J32_EXCEPTION_PREFETCH_ABORT);
	if(a32_get_instruction_endianness(cpu) == ARM_ENDIAN_LITTLE)
		return bswap_16(value);
	else
		return value;
}

// convenience functions
uint8_t arm_fetch8(arm_state_t * cpu, uint64_t address)
{
	uint8_t value;
	memory_read8(cpu->memory, cpu, address, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

uint16_t arm_fetch16(arm_state_t * cpu, uint64_t address)
{
	uint16_t value;
	memory_read16(cpu->memory, cpu, address, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

uint16_t arm_fetch16be(arm_state_t * cpu, uint64_t address)
{
	uint16_t value;
	memory_read16(cpu->memory, cpu, address, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu));
	if(a32_get_instruction_endianness(cpu) == ARM_ENDIAN_LITTLE)
		return bswap_16(value);
	else
		return value;
}

// may be accessed from either AArch32 or AArch64
uint32_t arm_fetch32(arm_state_t * cpu, uint64_t address)
{
	uint32_t value;
	memory_read32(cpu->memory, cpu, address, &value, arm_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu));
	return value;
}

int32_t arm_fetch32be(arm_state_t * cpu, uint32_t offset)
{
	uint32_t value;
	memory_read32(cpu->memory, cpu, offset, &value, a32_get_instruction_endianness(cpu), arm_is_privileged_mode(cpu));
	if(a32_get_instruction_endianness(cpu) == ARM_ENDIAN_LITTLE)
		return bswap_16(value);
	else
		return value;
}

int8_t arm_fetch_next8(arm_state_t * cpu)
{
	int8_t value = arm_fetch8(cpu, cpu->r[PC]);
	cpu->r[PC] += 1;
	return value;
}

uint16_t arm_fetch_next16(arm_state_t * cpu)
{
	uint16_t value = arm_fetch16(cpu, cpu->r[PC]);
	cpu->r[PC] += 2;
	return value;
}

int16_t arm_fetch_next16be(arm_state_t * cpu)
{
	int16_t value = arm_fetch16be(cpu, cpu->r[PC]);
	cpu->r[PC] += 2;
	return value;
}

uint32_t arm_fetch_next32(arm_state_t * cpu)
{
	uint32_t value = arm_fetch32(cpu, cpu->r[PC]);
	cpu->r[PC] += 4;
	if(cpu->pstate.rw == PSTATE_RW_26)
		cpu->r[PC] &= 0x03FFFFFF; // note: this is only needed here, since 26-bit modes can only run in ARM mode
	return value;
}

int32_t arm_fetch_next32be(arm_state_t * cpu)
{
	int32_t value = arm_fetch32be(cpu, cpu->r[PC]);
	cpu->r[PC] += 4;
	return value;
}

static inline void fetch_align32(arm_state_t * cpu)
{
	cpu->r[PC] = (cpu->r[PC] + 3) & ~3;
}

static inline float32_t a32_register_get32fp(arm_state_t * cpu, uint8_t regnum, uint8_t index)
{
	// TODO
	regnum = (regnum & ~7) | ((regnum + index * ((cpu->vfp.fpscr & FPSCR_STRIDE_MASK) >> FPSCR_STRIDE_SHIFT)) & 7);
	return cpu->VFP_S(regnum);
}
#define a32_register_get32fp_maybe_vector(_cpu, _regnum, _index) (a32_register_get32fp(_cpu, _regnum, (_regnum & 0x18) ? (_index) : 0))

static inline void a32_register_set32fp(arm_state_t * cpu, uint8_t regnum, uint8_t index, float32_t value)
{
	// TODO
	regnum = (regnum & ~7) | ((regnum + index * ((cpu->vfp.fpscr & FPSCR_STRIDE_MASK) >> FPSCR_STRIDE_SHIFT)) & 7);
	cpu->vfp.format_bits &= ~(1 << (regnum >> 1));
	cpu->VFP_S(regnum) = value;
}
#define a32_register_set32fp_maybe_vector(_cpu, _regnum, _index, _value) (a32_register_set32fp(_cpu, _regnum, (_regnum & 0x18) ? (_index) : 0), _value)

static inline float64_t a32_register_get64fp(arm_state_t * cpu, uint8_t regnum, uint8_t index)
{
	// TODO
	regnum = (regnum & ~3) | ((regnum + index * ((cpu->vfp.fpscr & FPSCR_STRIDE_MASK) >> FPSCR_STRIDE_SHIFT)) & 3);
	return cpu->VFP_D(regnum);
}
#define a32_register_get64fp_maybe_vector(_cpu, _regnum, _index) (a32_register_get32fp(_cpu, _regnum, (_regnum & 0x1C) ? (_index) : 0))

static inline void a32_register_set64fp(arm_state_t * cpu, uint8_t regnum, uint8_t index, float64_t value)
{
	// TODO
	regnum = (regnum & ~3) | ((regnum + index * ((cpu->vfp.fpscr & FPSCR_STRIDE_MASK) >> FPSCR_STRIDE_SHIFT)) & 3);
	cpu->vfp.format_bits |= 1 << regnum;
	cpu->VFP_D(regnum) = value;
}
#define a32_register_set64fp_maybe_vector(_cpu, _regnum, _index, _value) (a32_register_set64fp(_cpu, _regnum, (_regnum & 0x1C) ? (_index) : 0), _value)

static inline uint32_t float_as_word(float value)
{
	// TODO
	union
	{
		uint32_t i;
		float f;
	} u;
	u.f = value;
	return u.i;
}

static inline float word_as_float(uint32_t value)
{
	// TODO
	union
	{
		uint32_t i;
		float f;
	} u;
	u.i = value;
	return u.f;
}

static inline uint64_t double_as_dword(double value)
{
	// TODO
	union
	{
		uint64_t i;
		double f;
	} u;
	u.f = value;
	return u.i;
}

static inline double dword_as_double(uint64_t value)
{
	// TODO
	union
	{
		uint64_t i;
		double f;
	} u;
	u.i = value;
	return u.f;
}

static inline void ldouble_as_words(long double value, uint64_t * fraction, uint32_t * exponent)
{
	// TODO
	union
	{
		struct
		{
			uint64_t l;
			uint32_t h;
		};
		long double f;
	} u;
	u.f = value;
	*fraction = u.l;
	*exponent = u.h;
}

static inline long double words_as_ldouble(uint64_t low_word, uint32_t high_word)
{
	// TODO
	union
	{
		struct
		{
			uint64_t l;
			uint32_t h;
		};
		long double f;
	} u;
	u.l = low_word;
	u.h = high_word;
	return u.f;
}

static inline uint64_t swap_words(uint64_t value)
{
	return (value << 32) | (value >> 32);
}

// VFP access

static inline uint32_t a32_register_get64fp_low(arm_state_t * cpu, uint8_t regnum)
{
	// TODO
	return double_as_dword(cpu->VFP_D(regnum * 2));
}

static inline void a32_register_set64fp_low(arm_state_t * cpu, uint8_t regnum, uint32_t value)
{
	// TODO
	cpu->vfp.format_bits |= 1 << regnum;
	cpu->VFP_D(regnum * 2) = dword_as_double((double_as_dword(cpu->VFP_D(regnum * 2)) & 0xFFFFFFFF00000000) | value);
}

static inline uint32_t a32_register_get64fp_high(arm_state_t * cpu, uint8_t regnum)
{
	// TODO
	return double_as_dword(cpu->VFP_D(regnum * 2 + 1)) >> 16;
}

static inline void a32_register_set64fp_high(arm_state_t * cpu, uint8_t regnum, uint32_t value)
{
	// TODO
	cpu->vfp.format_bits |= 1 << regnum;
	cpu->VFP_D(regnum * 2) = dword_as_double((double_as_dword(cpu->VFP_D(regnum * 2)) & 0x00000000FFFFFFFF) | ((uint64_t)value << 32));
}

static inline uint64_t a32_register_get64fp_both(arm_state_t * cpu, uint8_t regnum)
{
	// TODO
	return double_as_dword(cpu->VFP_W(regnum * 2));
}

static inline void a32_register_set64fp_both(arm_state_t * cpu, uint8_t regnum, uint64_t value)
{
	// TODO
	cpu->VFP_W(regnum * 2) = dword_as_double(value);
}

static inline void a32_write32fp(arm_state_t * cpu, uint32_t address, float32_t value)
{
	a32_write32(cpu, address, float_as_word(value));
}

static inline float32_t a32_read32fp(arm_state_t * cpu, uint32_t address)
{
	return word_as_float(a32_read32(cpu, address));
}

static inline void fpa_write32fp(arm_state_t * cpu, uint32_t address, float32_t value)
{
	a32_write32fp(cpu, address, value);
}

static inline float32_t fpa_read32fp(arm_state_t * cpu, uint32_t address)
{
	return a32_read32fp(cpu, address);
}

static inline void a32_write64fp(arm_state_t * cpu, uint32_t address, float64_t value)
{
	a32_write64(cpu, address, double_as_dword(value));
}

static inline float64_t a32_read64fp(arm_state_t * cpu, uint32_t address)
{
	return dword_as_double(a32_read64(cpu, address));
}

static inline void fpa_write64fp(arm_state_t * cpu, uint32_t address, float64_t value)
{
	a32_write64(cpu, address, swap_words(double_as_dword(value)));
}

static inline float64_t fpa_read64fp(arm_state_t * cpu, uint32_t address)
{
	return dword_as_double(swap_words(a32_read64(cpu, address)));
}

static inline void fpa_write80fp(arm_state_t * cpu, uint32_t address, float80_t value)
{
	uint64_t low_word;
	uint32_t high_word;
	ldouble_as_words(value, &low_word, &high_word);
	a32_write32(cpu, address,     high_word);
	a32_write64(cpu, address + 4, low_word);
}

static inline float80_t fpa_read80fp(arm_state_t * cpu, uint32_t address)
{
	uint32_t high_word = a32_read32(cpu, address);
	uint64_t low_word  = a32_read32(cpu, address + 4);
	return words_as_ldouble(low_word, high_word);
}

// TODO: N=false, imms=-1 or -2 are invalid
// for IMM=true, N=false, (imms + 1) & imms should not be 0
// for IMM=true, N=true, imm should not be -1
uint64_t a64_get_bitmask64(uint32_t opcode)
{
	bool N = opcode & 0x00400000;
	uint8_t imms = (opcode & 0x0000FC00) >> 10;
	uint8_t immr = (opcode & 0x003F0000) >> 16;
	uint64_t mask;

	if(N)
	{
		// len == 6
		imms &= 0x3F;
		immr &= 0x3F;

		mask = imms == 63 ? (uint64_t)-1 : ((uint64_t)1 << (imms + 1)) - 1;
	}
	else if((imms & 0x20) == 0)
	{
		// len == 5
		imms &= 0x1F;
		immr &= 0x1F;

		mask = (0x0000000100000001 << (imms + 1)) - 0x0000000100000001;
	}
	else if((imms & 0x10) == 0)
	{
		// len == 4
		imms &= 0x0F;
		immr &= 0x0F;

		mask = (0x0001000100010001 << (imms + 1)) - 0x0001000100010001;
	}
	else if((imms & 0x08) == 0)
	{
		// len == 3
		imms &= 0x07;
		immr &= 0x07;

		mask = (0x0101010101010101 << (imms + 1)) - 0x0101010101010101;
	}
	else if((imms & 0x04) == 0)
	{
		// len == 2
		imms &= 0x03;
		immr &= 0x03;

		mask = (0x1111111111111111 << (imms + 1)) - 0x1111111111111111;
	}
	else
	{
		// len == 1
		imms &= 0x01;
		immr &= 0x01;

		mask = (0x5555555555555555 << (imms + 1)) - 0x5555555555555555;
	}
	return rotate_right64(mask, immr);
}

uint32_t a64_get_bitmask32(uint32_t opcode)
{
	uint8_t imms = (opcode & 0x0000FC00) >> 10;
	uint8_t immr = (opcode & 0x003F0000) >> 16;
	uint32_t mask;

	if((imms & 0x20) == 0)
	{
		// len == 5
		imms &= 0x1F;
		immr &= 0x1F;

		mask = (1 << (imms + 1)) - 1;
	}
	else if((imms & 0x10) == 0)
	{
		// len == 4
		imms &= 0x0F;
		immr &= 0x0F;

		mask = (0x00010001 << (imms + 1)) - 0x00010001;
	}
	else if((imms & 0x08) == 0)
	{
		// len == 3
		imms &= 0x07;
		immr &= 0x07;

		mask = (0x01010101 << (imms + 1)) - 0x01010101;
	}
	else if((imms & 0x04) == 0)
	{
		// len == 2
		imms &= 0x03;
		immr &= 0x03;

		mask = (0x11111111 << (imms + 1)) - 0x11111111;
	}
	else
	{
		// len == 1
		imms &= 0x01;
		immr &= 0x01;

		mask = (0x55555555 << (imms + 1)) - 0x55555555;
	}
	return rotate_right32(mask, immr);
}

uint64_t a64_bfm64(uint64_t op1, uint64_t op2, uint8_t immr, uint8_t imms)
{
	uint64_t wmask, tmask;
	uint8_t diff = imms - immr;

	// len == 6
	imms &= 0x3F;
	immr &= 0x3F;
	diff &= 0x3F;

	wmask = imms == 63 ? (uint64_t)-1 : ((uint64_t)1 << (imms + 1)) - 1;
	tmask = diff == 63 ? (uint64_t)-1 : ((uint64_t)1 << (diff + 1)) - 1;

	op2 &= wmask;

	wmask = rotate_right64(wmask, immr);
	op2 = rotate_right64(op2, immr);

	wmask &= tmask;
	op2 &= tmask;

	return (op1 & ~wmask) | op2;
}

uint32_t a64_bfm32(uint32_t op1, uint32_t op2, uint8_t imms, uint8_t immr)
{
	uint32_t wmask, tmask;
	uint8_t diff = imms - immr;

	// len == 5
	imms &= 0x1F;
	immr &= 0x1F;
	diff &= 0x1F;

	wmask = (1 << (imms + 1)) - 1;
	tmask = (1 << (diff + 1)) - 1;

	op2 &= wmask;

	wmask = rotate_right32(wmask, immr);
	op2 = rotate_right32(op2, immr);

	wmask &= tmask;
	op2 &= tmask;

	return (op1 & ~wmask) | op2;
}

uint64_t a64_sbfm64(uint64_t op, uint8_t immr, uint8_t imms)
{
	uint64_t wmask, tmask;
	uint8_t diff = imms - immr;
	bool sign = (int64_t)op < 0;

	// len == 6
	imms &= 0x3F;
	immr &= 0x3F;
	diff &= 0x3F;

	wmask = imms == 63 ? (uint64_t)-1 : ((uint64_t)1 << (imms + 1)) - 1;
	tmask = diff == 63 ? (uint64_t)-1 : ((uint64_t)1 << (diff + 1)) - 1;

	op &= wmask;

	wmask = rotate_right64(wmask, immr);
	op = rotate_right64(op, immr);

	return (sign ? ~tmask : 0) | (op & tmask);
}

uint32_t a64_sbfm32(uint32_t op, uint8_t immr, uint8_t imms)
{
	uint32_t wmask, tmask;
	uint8_t diff = imms - immr;
	bool sign = (int32_t)op < 0;

	// len == 5
	imms &= 0x1F;
	immr &= 0x1F;
	diff &= 0x1F;

	wmask = (1 << (imms + 1)) - 1;
	tmask = (1 << (diff + 1)) - 1;

	op &= wmask;

	wmask = rotate_right32(wmask, immr);
	op = rotate_right32(op, immr);

	return (sign ? ~tmask : 0) | (op & tmask);
}

uint64_t a64_ubfm64(uint64_t op, uint8_t immr, uint8_t imms)
{
	uint64_t wmask, tmask;
	uint8_t diff = imms - immr;

	// len == 6
	imms &= 0x3F;
	immr &= 0x3F;
	diff &= 0x3F;

	wmask = imms == 63 ? (uint64_t)-1 : ((uint64_t)1 << (imms + 1)) - 1;
	tmask = diff == 63 ? (uint64_t)-1 : ((uint64_t)1 << (diff + 1)) - 1;

	op &= wmask;

	wmask = rotate_right64(wmask, immr);
	op = rotate_right64(op, immr);

	return op & tmask;
}

uint32_t a64_ubfm32(uint32_t op, uint8_t immr, uint8_t imms)
{
	uint32_t wmask, tmask;
	uint8_t diff = imms - immr;

	// len == 5
	imms &= 0x1F;
	immr &= 0x1F;
	diff &= 0x1F;

	wmask = (1 << (imms + 1)) - 1;
	tmask = (1 << (diff + 1)) - 1;

	op &= wmask;

	wmask = rotate_right32(wmask, immr);
	op = rotate_right32(op, immr);

	return op & tmask;
}

/* Execution */

static inline bool a32_check_condition(arm_state_t * cpu, int code)
{
	switch(code & 0xF)
	{
	case 0x0:
		/* eq */
		return cpu->pstate.z != 0;
	case 0x1:
		/* ne */
		return cpu->pstate.z == 0;
	case 0x2:
		/* cs */
		return cpu->pstate.c != 0;
	case 0x3:
		/* cc */
		return cpu->pstate.c == 0;
	case 0x4:
		/* mi */
		return cpu->pstate.n != 0;
	case 0x5:
		/* pl */
		return cpu->pstate.n == 0;
	case 0x6:
		/* vs */
		return cpu->pstate.v != 0;
	case 0x7:
		/* vc */
		return cpu->pstate.v == 0;
	case 0x8:
		/* hi */
		return cpu->pstate.c != 0 && cpu->pstate.z == 0;
	case 0x9:
		/* ls */
		return cpu->pstate.c == 0 || cpu->pstate.z != 0;
	case 0xA:
		/* ge */
		return (cpu->pstate.n == 0) == (cpu->pstate.v == 0);
	case 0xB:
		/* lt */
		return (cpu->pstate.n == 0) != (cpu->pstate.v == 0);
	case 0xC:
		/* gt */
		return ((cpu->pstate.n == 0) == (cpu->pstate.v == 0)) && cpu->pstate.z != 0;
	case 0xD:
		/* le */
		return ((cpu->pstate.n == 0) != (cpu->pstate.v == 0)) || cpu->pstate.z == 0;
	case 0xE:
		/* al */
		return true;
	case 0xF:
		/* nv */
		return false;
	default:
		assert(false);
	}
}

// same except for 0xF
static inline bool a64_check_condition(arm_state_t * cpu, int code)
{
	if((code & 0xF) == 0xF)
		return true;
	else
		return a32_check_condition(cpu, code);
}

static inline uint8_t t32_get_it_state(arm_state_t * cpu)
{
	return cpu->pstate.it;
}

static inline void t32_set_it_state(arm_state_t * cpu, uint8_t itstate)
{
	cpu->pstate.it = itstate;
}

static inline bool t32_in_it_block(arm_state_t * cpu)
{
	uint8_t itstate = t32_get_it_state(cpu);
	return (itstate & 0xF) != 0;
}

static inline bool t32_last_in_it_block(arm_state_t * cpu)
{
	uint8_t itstate = t32_get_it_state(cpu);
	return (itstate & 0xF) == 0x8;
}

static inline void t32_advance_it(arm_state_t * cpu)
{
	if(!t32_in_it_block(cpu))
		return;

	uint8_t itstate = t32_get_it_state(cpu);

	if((itstate & 0x07) == 0)
		itstate = 0;
	else
		itstate = (itstate & 0xE0) | ((itstate << 1) & 0x1F);

	t32_set_it_state(cpu, itstate);
}

static inline bool t32_check_condition(arm_state_t * cpu)
{
	if(!t32_in_it_block(cpu))
		return true;

	uint8_t itstate = t32_get_it_state(cpu);

	bool condition = a32_check_condition(cpu, (itstate & 0xE0) >> 4);

	if((itstate & 0x10))
		condition = !condition;

	return condition;
}

static inline _Noreturn void arm_break_emulation(arm_state_t * cpu, arm_emu_result_t result)
{
	t32_advance_it(cpu);
	cpu->result = result;
	longjmp(cpu->exc, 1);
}

// TODO: there is more to check
static inline bool a64_el_uses_aarch64(arm_state_t * cpu, int selected_el)
{
	if(selected_el >= cpu->lowest_64bit_only_el)
		return true;  // A64
	if(selected_el == 3)
		return false; // A32
	// check EL2
	if(cpu->el3_supported && (cpu->scr_el3 & SCR_EL3_RW) == 0)
		return false; // A32
	if(selected_el == 2)
		return true;  // A64
	// check EL1
	if(cpu->el2_supported && (cpu->hcr_el2 & HCR_EL2_RW) == 0 && (cpu->hcr_el2 & (HCR_EL2_E2H|HCR_EL2_TGE)) != (HCR_EL2_E2H|HCR_EL2_TGE))
		return false; // A32
	if(selected_el == 1)
		return true; // A64
	return cpu->pstate.rw == PSTATE_RW_64;
}

static inline _Noreturn void a64_exception(arm_state_t * cpu, uint32_t address, int mode)
{
	t32_advance_it(cpu);

	int current_el = cpu->pstate.el;
	int target_el = (mode & CPSR_EL_MASK) >> CPSR_EL_SHIFT;
	if(target_el > current_el)
	{
		int check_el = target_el - 1;
#if 0
		if check_el = 2 and there is no EL in this security state
			check_el = 1
		elif check_el = 1 and PSTATE.EL = 0 and IsInHost()
			check_el = 0
#endif

		if(a64_el_uses_aarch64(cpu, check_el))
			address += 0x600;
		else
			address += 0x400;
	}
	else if(cpu->pstate.sp)
	{
		address += 0x200;
	}

	switch(target_el)
	{
	case 1:
		address += cpu->vbar_el1 & ~0x7FF;
		break;
	case 2:
		address += cpu->vbar_el2 & ~0x7FF;
		break;
	case 3:
		address += cpu->vbar_el3 & ~0x7FF;
		break;
	}

	cpu->r[ELR_EL1 - 1 + mode] = cpu->r[PC];
	cpu->r[SPSR_EL1 - 1 + mode] = a64_get_cpsr(cpu);

	cpu->r[PC] = address;

	cpu->pstate.rw = PSTATE_RW_64;
	cpu->pstate.jt = PSTATE_JT_ARM;
	cpu->pstate.mode = mode;
	cpu->pstate.sp = 1;

	cpu->pstate.d = 1;
	cpu->pstate.a = 1;
	cpu->pstate.i = 1;
	cpu->pstate.f = 1;

	cpu->pstate.it = 0;
	cpu->pstate.ss = 0;
	cpu->pstate.il = 0;

	if(cpu->config.version >= ARMV81)
	{
		cpu->pstate.pan = cpu->sctlr_el1 & SCTLR_SPAN ? 1 : 0; /* TODO: PAN only if supported */
	}

	longjmp(cpu->exc, 1);
}

static inline _Noreturn void a32_exception(arm_state_t * cpu, uint32_t address, int mode)
{
	t32_advance_it(cpu);

	if(!a32_is_prog26(cpu))
	{
		if(mode == MODE_HYP)
			cpu->r[ELR_HYP] = cpu->r[PC];
		else
			a32_register_mode(cpu, 14, mode) = cpu->r[PC];
		a32_spsr_mode(cpu, mode) = a32_get_cpsr(cpu);
	}
	else
	{
		a32_register_mode(cpu, 14, mode) = a26_get_pc(cpu);
	}

	cpu->r[PC] = address;
	// TODO: access vbar_*
	if((cpu->sctlr_el1 & SCTLR_V)) // v5
		cpu->r[PC] += 0xFFFF0000;

	cpu->pstate.i = 1;
	cpu->pstate.mode = mode;
	cpu->pstate.rw = a32_is_prog26(cpu) ? PSTATE_RW_26 : PSTATE_RW_32;
	if(address == A32_VECTOR_RESET || address == A32_VECTOR_FIQ)
	{
		cpu->pstate.f = 1;
	}
	cpu->pstate.jt = cpu->config.version >= ARMV7 && cpu->sctlr_el1 & SCTLR_TE ? PSTATE_JT_THUMB : PSTATE_JT_ARM;
	cpu->pstate.e = cpu->config.version >= ARMV6 && cpu->sctlr_el1 & SCTLR_EE ? 1 : 0;
	if(cpu->config.version >= ARMV6 && (address != A32_VECTOR_UNDEFINED || address != A32_VECTOR_SWI))
	{
		cpu->pstate.a = 1;
	}
	cpu->pstate.it = 0;
	if(cpu->config.version >= ARMV8)
	{
		cpu->pstate.ss = 1;
		cpu->pstate.il = 1;
	}
	if(cpu->config.version >= ARMV81)
	{
		cpu->pstate.pan = cpu->sctlr_el1 & SCTLR_SPAN ? 1 : 0; /* TODO: PAN only if supported */
	}

	longjmp(cpu->exc, 1);
}

_Noreturn void arm_reset(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_RESET);
	}
	else
	{
		if(cpu->el3_supported)
		{
			if(cpu->lowest_64bit_only_el <= 3)
				a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, 3);
			else
				a32_exception(cpu, A32_VECTOR_RESET, MODE_SVC);
		}
		else if(cpu->el2_supported)
		{
			if(cpu->lowest_64bit_only_el <= 2)
				a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, 2);
			else
				a32_exception(cpu, A32_VECTOR_RESET, MODE_HYP);
		}
		else
		{
			if(cpu->lowest_64bit_only_el <= 1)
				a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, 1);
			else
				a32_exception(cpu, A32_VECTOR_RESET, MODE_SVC);
		}
	}
}

_Noreturn void arm_undefined(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_SVC);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		switch(arm_get_current_instruction_set(cpu))
		{
		case ISA_AARCH26:
		case ISA_AARCH32:
			cpu->r[PC] += 4;
			break;
		case ISA_THUMB32:
		case ISA_THUMBEE:
			cpu->r[PC] += 2;
			break;
		case ISA_JAZELLE:
			// Jazelle can not issue an undefined instruction exception
			break;
		case ISA_AARCH64:
			// invalid
			break;
		}

		a32_exception(cpu, A32_VECTOR_UNDEFINED, !a32_is_prog26(cpu) ? MODE_UND : MODE_SVC);
	}
}

// called swc before v7/UAL assembly
_Noreturn void arm_svc(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_SVC);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		a32_exception(cpu, A32_VECTOR_SWI, MODE_SVC);
	}
}

_Noreturn void arm_hvc(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_HVC);
	}
	else if(a64_el_uses_aarch64(cpu, 2))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(2, cpu->pstate.el));
	}
	else
	{
		a32_exception(cpu, A32_VECTOR_SWI, MODE_HYP);
	}
}

_Noreturn void arm_smc(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_SMC);
	}
	else if(a64_el_uses_aarch64(cpu, 3))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, 3);
	}
	else
	{
		a32_exception(cpu, A32_VECTOR_SWI, MODE_MON);
	}
}

_Noreturn void arm_prefetch_abort(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_PREFETCH_ABORT);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_PREFETCH_ABORT, !a32_is_prog26(cpu) ? MODE_ABT : MODE_SVC);
	}
}

_Noreturn void arm_data_abort(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_DATA_ABORT);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		cpu->r[PC] += 8;

		a32_exception(cpu, A32_VECTOR_DATA_ABORT, !a32_is_prog26(cpu) ? MODE_ABT : MODE_SVC);
	}
}

// only ARMv3, in 26-bit mode
_Noreturn void a32_address_exception(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_ADDRESS26);
	}
	else
	{
		cpu->r[PC] += 8;

		a32_exception(cpu, A32_VECTOR_ADDRESS, MODE_SVC);
	}
}

// called externally
_Noreturn void arm_interrupt(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_IRQ);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_IRQ, MAX(1, cpu->pstate.el));
	}
	else
	{
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_IRQ, MODE_IRQ);
	}
}

// called externally
_Noreturn void arm_fast_interrupt(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_FIQ);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_FIQ, MAX(1, cpu->pstate.el));
	}
	else
	{
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_FIQ, MODE_FIQ);
	}
}

// called externally
_Noreturn void arm_serror(arm_state_t * cpu)
{
	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_SERROR);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SERROR, MAX(1, cpu->pstate.el));
	}
	else
	{
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_DATA_ABORT, MODE_ABT);
	}
}

_Noreturn void arm_breakpoint(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_BREAKPOINT);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		// prefetch abort, not defined on 26-bit processors
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_PREFETCH_ABORT, MODE_ABT);
	}
}

_Noreturn void arm_unaligned(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_UNALIGNED);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		// data abort
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		// data abort
		cpu->r[PC] += 8;

		a32_exception(cpu, A32_VECTOR_DATA_ABORT, !a32_is_prog26(cpu) ? MODE_ABT : MODE_SVC);
	}
}

_Noreturn void arm_unaligned_pc(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_UNALIGNED_PC);
	}
	else if(a64_el_uses_aarch64(cpu, 1))
	{
		cpu->r[PC] = cpu->old_pc;
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
	else
	{
		// prefetch abort, not defined on 26-bit processors
		cpu->r[PC] += 4;

		a32_exception(cpu, A32_VECTOR_PREFETCH_ABORT, MODE_ABT);
	}
}

_Noreturn void a64_unaligned_sp(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_UNALIGNED_SP);
	}
	else
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
}

_Noreturn void arm_software_step(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->old_pc;

	if(cpu->capture_breaks)
	{
		arm_break_emulation(cpu, ARM_EMU_SOFTWARE_STEP);
	}
	else
	{
		a64_exception(cpu, A64_VECTOR_SYNCHRONOUS, MAX(1, cpu->pstate.el));
	}
}

void a32_eret(arm_state_t * cpu)
{
	if(cpu->pstate.mode == MODE_HYP)
		cpu->r[PC] = cpu->r[ELR_HYP];
	else
		cpu->r[PC] = a32_register(cpu, 14);
	a32_set_cpsr(cpu, ~(uint32_t)0, a32_get_spsr(cpu));
}

void a64_eret(arm_state_t * cpu)
{
	cpu->r[PC] = cpu->r[ELR_EL1 - 1 + cpu->pstate.mode];
	a64_set_cpsr(cpu, cpu->r[SPSR_EL1 - 1 + cpu->pstate.mode]);
}

uint32_t a32_lsl32(arm_state_t * cpu, uint32_t value, uint32_t amount, bool store_carry)
{
	if(amount == 0)
		return value;
	if(store_carry)
		cpu->pstate.c = amount <= 32 ? (value >> (32 - amount)) & 1 : 0;
	return amount < 32 ? value << amount : 0;
}

uint32_t a32_lsr32(arm_state_t * cpu, uint32_t value, uint32_t amount, bool store_carry)
{
	if(amount == 0)
		return value;
	if(store_carry)
		cpu->pstate.c = amount <= 32 ? (value >> (amount - 1)) & 1 : 0;
	return amount < 32 ? value >> amount : 0;
}

uint32_t a32_asr32(arm_state_t * cpu, uint32_t value, uint32_t amount, bool store_carry)
{
	if(amount == 0)
		return value;
	if(store_carry)
		cpu->pstate.c = (amount <= 32 ? value >> (amount - 1) : value >> 31) & 1;
	return amount < 32 ? (int32_t)value >> amount : (int32_t)value >> 31;
}

uint32_t a32_ror32(arm_state_t * cpu, uint32_t value, uint32_t amount, bool store_carry)
{
	if(amount == 0)
		return value;
	amount &= 0x1F;
	if(store_carry)
		cpu->pstate.c = (value >> (amount - 1)) & 1;
	return rotate_right32(value, amount);
}

uint32_t a32_rrx32(arm_state_t * cpu, uint32_t value, bool store_carry)
{
	int carry = cpu->pstate.c;
	if(store_carry)
		cpu->pstate.c = value & 1;
	return (value >> 1) | (carry << 31);
}

uint32_t a32_get_immediate_operand(uint32_t opcode)
{
	uint32_t immediate = opcode & 0xFF;
	uint32_t amount = ((opcode >> 8) & 0xF) * 2;
	immediate = rotate_right32(immediate, amount);
	return immediate;
}

uint32_t a32_get_register_operand(arm_state_t * cpu, uint32_t opcode, bool store_carry)
{
	uint32_t value = a32_register_get32(cpu, opcode & 0xF);
	if((opcode & 0x00000FF0) == 0)
	{
		return value;
	}
	else if((opcode & 0x00000FF0) == 0x00000060)
	{
		/* rrx */
		int carry = cpu->pstate.c;
		if(store_carry)
			cpu->pstate.c = value & 1;
		return (value >> 1) | (carry << 31);
	}
	else
	{
 		uint32_t amount;
		if((opcode & 0x00000010))
		{
			if((opcode & 0xF) == A32_PC_NUM)
				value += 4;

			amount = a32_register_get32(cpu, (opcode >> 8) & 0xF) & 0xFF;
			if(amount == 0)
				return value;
		}
		else
		{
			/* 32 is encoded as 0, except for lsl */
			amount = (((opcode >> 7) - 1) & 0x1F) + 1;
		}
		switch((opcode >> 5) & 0x3)
		{
		case 0b00: /* lsl */
			if(store_carry)
				cpu->pstate.c = amount <= 32 ? (value >> (32 - amount)) & 1 : 0;
			return amount < 32 ? value << amount : 0;
		case 0b01: /* lsr */
			if(store_carry)
				cpu->pstate.c = amount <= 32 ? (value >> (amount - 1)) & 1 : 0;
			return amount < 32 ? value >> amount : 0;
		case 0b10: /* asr */
			if(store_carry)
				cpu->pstate.c = (amount <= 32 ? value >> (amount - 1) : value >> 31) & 1;
			return amount < 32 ? (int32_t)value >> amount : (int32_t)value >> 31;
		case 0b11: /* ror */
			amount &= 0x1F;
			if(store_carry)
				cpu->pstate.c = (value >> (amount - 1)) & 1;
			return rotate_right32(value, amount);
		default:
			assert(false);
		}
	}
}

uint32_t a32_get_operand(arm_state_t * cpu, uint32_t opcode, bool store_carry)
{
	if((opcode & 0x02000000))
	{
		return a32_get_immediate_operand(opcode);
	}
	else
	{
		return a32_get_register_operand(cpu, opcode, store_carry);
	}
}

uint32_t a32_get_address_operand(arm_state_t * cpu, uint32_t opcode)
{
	uint32_t operand;

	if((opcode & 0x02000000))
	{
		operand = a32_get_register_operand(cpu, opcode, false);
	}
	else
	{
		operand = opcode & 0xFFF;
	}

	if((opcode & 0x00800000))
	{
		return operand;
	}
	else
	{
		return -operand;
	}
}

uint32_t t32_get_immediate_operand(uint16_t opcode1, uint16_t opcode2)
{
	uint32_t imm = (opcode2 & 0xFF);
	uint8_t shift = ((opcode2 >> 12) & 0x7) | (((opcode1 >> 10) & 1) << 3);
	switch(shift)
	{
	case 0:
		break;
	case 1:
		imm = (imm << 16) | imm;
		break;
	case 2:
		imm = (imm << 24) | (imm << 8);
		break;
	case 3:
		imm = (imm << 24) | (imm << 16) | (imm << 8) | imm;
		break;
	default:
		shift = (shift << 1) | (imm >> 7);
		imm = (0x80 | (imm & 0x7F)) << (32 - shift);
		break;
	}
	return imm;
}

uint32_t t32_get_register_operand(arm_state_t * cpu, uint32_t opcode1, uint32_t opcode2, bool store_carry)
{
	uint32_t value = a32_register_get32(cpu, opcode2 & 0xF);
	if((opcode2 & 0x70F0) == 0)
	{
		return value;
	}
	else if((opcode2 & 0x70F0) == 0x0030)
	{
		/* rrx */
		int carry = cpu->pstate.c;
		if(store_carry)
			cpu->pstate.c = value & 1;
		return (value >> 1) | (carry << 31);
	}
	else
	{
 		uint32_t amount = (((opcode2 & 0x7000) >> 10) | ((opcode2 & 0x00C0) >> 6));
		/* 32 is encoded as 0, except for lsl */
		amount = ((amount - 1) & 0x1F) + 1;

		switch((opcode2 >> 4) & 0x3)
		{
		case 0b00: /* lsl */
			if(store_carry)
				cpu->pstate.c = amount <= 32 ? (value >> (32 - amount)) & 1 : 0;
			return amount < 32 ? value << amount : 0;
		case 0b01: /* lsr */
			if(store_carry)
				cpu->pstate.c = amount <= 32 ? (value >> (amount - 1)) & 1 : 0;
			return amount < 32 ? value >> amount : 0;
		case 0b10: /* asr */
			if(store_carry)
				cpu->pstate.c = (amount <= 32 ? value >> (amount - 1) : value >> 31) & 1;
			return amount < 32 ? (int32_t)value >> amount : (int32_t)value >> 31;
		case 0b11: /* ror */
			amount &= 0x1F;
			if(store_carry)
				cpu->pstate.c = (value >> (amount - 1)) & 1;
			return rotate_right32(value, amount);
		default:
			assert(false);
		}
	}
}

static inline uint64_t arm_get_simd_operand(arm_state_t * cpu, bool op, uint8_t cmode, uint64_t imm)
{
	switch(cmode & 0xF)
	{
	case 0x0:
	case 0x1:
		return (imm << 32) | imm;
	case 0x2:
	case 0x3:
		return (imm << 40) | (imm << 8);
	case 0x4:
	case 0x5:
		return (imm << 48) | (imm << 16);
	case 0x6:
	case 0x7:
		return (imm << 56) | (imm << 24);
	case 0x8:
	case 0x9:
		return (imm << 48) | (imm << 32) | (imm << 16) | imm;
	case 0xA:
	case 0xB:
		return (imm << 56) | (imm << 40) | (imm << 24) | (imm << 8);
	case 0xC:
		return 0x000000FF000000FF | (imm << 40) | (imm << 8);
	case 0xD:
		return 0x0000FFFF0000FFFF | (imm << 48) | (imm << 16);
	case 0xE:
		if(!op)
			return 0x0101010101010101 * imm;
		else
			return
				(imm & 0x80 ? 0xFF00000000000000 : 0)
				| (imm & 0x40 ? 0x00FF000000000000 : 0)
				| (imm & 0x20 ? 0x0000FF0000000000 : 0)
				| (imm & 0x10 ? 0x000000FF00000000 : 0)
				| (imm & 0x08 ? 0x00000000FF000000 : 0)
				| (imm & 0x04 ? 0x0000000000FF0000 : 0)
				| (imm & 0x02 ? 0x000000000000FF00 : 0)
				| (imm & 0x01 ? 0x00000000000000FF : 0);
	case 0xF:
		if(!op)
			return
				(imm & 0x80 ? 0x8000000080000000 : 0)
				| (imm & 0x40 ? 0x3E0000003E000000 : 0x4000000040000000)
				| ((imm & 0x3F) << 35) | ((imm & 0x3F) << 19);
		else if(cpu != NULL)
			arm_undefined(cpu);
		else
			return 0;
	}
	assert(false);
}

uint64_t a32_get_simd_operand(arm_state_t * cpu, uint32_t opcode)
{
	bool op = opcode & 0x00000020;
	uint8_t cmode = (opcode >> 8) & 0xF;
	uint64_t imm = ((opcode >> (24 - 7)) & 0x80) | ((opcode >> (16 - 4)) & 0x70) | (opcode & 0x0F);
	return arm_get_simd_operand(cpu, op, cmode, imm);
}

uint64_t t32_get_simd_operand(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2)
{
	bool op = opcode2 & 0x0020;
	uint8_t cmode = (opcode2 >> 8) & 0xF;
	uint64_t imm = ((opcode1 >> (12 - 7)) & 0x80) | ((opcode1 << 4) & 0x70) | (opcode2 & 0x0F);
	return arm_get_simd_operand(cpu, op, cmode, imm);
}

uint64_t a64_get_simd_operand(arm_state_t * cpu, uint32_t opcode)
{
	bool op = opcode & 0x20000000;
	uint8_t cmode = (opcode >> 12) & 0xF;
	uint64_t imm = ((opcode >> (16 - 5)) & 0xE0) | ((opcode >> 5) & 0x1F);
	return arm_get_simd_operand(cpu, op, cmode, imm);
}

uint8_t arm_get_simd_vector_index_size(uint8_t imm)
{
	if((imm & 1))
		return 8;
	else if((imm & 2))
		return 16;
	else
		return 32;
}

uint8_t arm_get_simd_vector_index(uint8_t imm)
{
	imm &= 0xF;
	if((imm & 1))
		return imm >> 1;
	else if((imm & 2))
		return imm >> 2;
	else
		return imm >> 3;
}

uint8_t arm_get_simd_shift_element_size(uint8_t imm)
{
	if((imm & 0x40))
		return 64;
	else if((imm & 0x20))
		return 32;
	else if((imm & 0x10))
		return 16;
	else
		return 8;
}

uint8_t arm_get_simd_shift_amount_neg(uint8_t imm)
{
	if((imm & 0x40))
		return 128 - imm;
	else if((imm & 0x20))
		return 64 - imm;
	else if((imm & 0x10))
		return 32 - imm;
	else
		return 16 - imm;
}

uint8_t arm_get_simd_shift_amount(uint8_t imm)
{
	if((imm & 0x40))
		return imm - 64;
	else if((imm & 0x20))
		return imm - 32;
	else if((imm & 0x10))
		return imm - 16;
	else
		return imm - 8;
}

uint32_t a64_get_shifted_operand32(uint32_t value, int shift_type, uint8_t amount)
{
	switch(shift_type)
	{
	case 0b00: /* lsl */
		return amount < 32 ? value << amount : 0;
	case 0b01: /* lsr */
		return amount < 32 ? value >> amount : 0;
	case 0b10: /* asr */
		return amount < 32 ? (int32_t)value >> amount : (int32_t)value >> 31;
	case 0b11: /* ror */
		amount &= 0x1F;
		return rotate_right32(value, amount);
	default:
		assert(false);
	}
}

uint64_t a64_get_shifted_operand64(uint64_t value, int shift_type, uint8_t amount)
{
	switch(shift_type)
	{
	case 0b00: /* lsl */
		return amount < 64 ? value << amount : 0;
	case 0b01: /* lsr */
		return amount < 64 ? value >> amount : 0;
	case 0b10: /* asr */
		return amount < 64 ? (int64_t)value >> amount : (int64_t)value >> 63;
	case 0b11: /* ror */
		return rotate_right64(value, amount);
	default:
		assert(false);
	}
}

/* Instructions */

static inline void a32_test_nz(arm_state_t * cpu, uint32_t res)
{
	cpu->pstate.z = res == 0;
	cpu->pstate.n = (res >> 31) & 1;
}

static inline void a32_test64_nz(arm_state_t * cpu, uint64_t res)
{
	cpu->pstate.z = res == 0;
	cpu->pstate.n = (res >> 63) & 1;
}

static inline void a64_test32_nz(arm_state_t * cpu, uint32_t res)
{
	cpu->pstate.z = res == 0;
	cpu->pstate.n = (res >> 31) & 1;
}

static inline void a64_test64_nz(arm_state_t * cpu, uint64_t res)
{
	cpu->pstate.z = res == 0;
	cpu->pstate.n = (res >> 63) & 1;
}

static inline void a32_test_nzvc(arm_state_t * cpu, uint32_t res, uint32_t op1, uint32_t op2)
{
	cpu->pstate.c = (((op1 & op2) | (op1 & ~res) | (op2 & ~res)) >> 31) & 1;
	cpu->pstate.v = (((op1 & op2 & ~res) | (~op1 & ~op2 & res)) >> 31) & 1;
	a32_test_nz(cpu, res);
}

static inline void a64_test32_nzvc(arm_state_t * cpu, uint32_t res, uint32_t op1, uint32_t op2)
{
	cpu->pstate.c = (((op1 & op2) | (op1 & ~res) | (op2 & ~res)) >> 31) & 1;
	cpu->pstate.c = (((op1 & op2 & ~res) | (~op1 & ~op2 & res)) >> 31) & 1;
	a64_test32_nz(cpu, res);
}

static inline void a64_test64_nzvc(arm_state_t * cpu, uint64_t res, uint64_t op1, uint64_t op2)
{
	cpu->pstate.c = (((op1 & op2) | (op1 & ~res) | (op2 & ~res)) >> 63) & 1;
	cpu->pstate.v = (((op1 & op2 & ~res) | (~op1 & ~op2 & res)) >> 63) & 1;
	a64_test64_nz(cpu, res);
}

/* for 26-bit mode, copy the flags from value, otherwise from the SPR, this is used by certain instructions that modify R15 and set the flags */
static inline void a32_or_a26_copy_flags(arm_state_t * cpu, uint32_t value)
{
	if(a32_is_arm26(cpu))
	{
		a32_set_cpsr_nzcv(cpu, value);
		if(cpu->pstate.mode != MODE_USR)
		{
			cpu->pstate.i = value & CPSR_A26_I ? 1 : 0;
			cpu->pstate.f = value & CPSR_A26_F ? 1 : 0;
		}
	}
	else
	{
		if(!a32_spsr_valid(cpu))
			return; // unpredictable, this is v3 behavior

		a32_set_cpsr(cpu, ~(uint32_t)0, a32_get_spsr(cpu));
	}
}

static inline void a32_or_a26_test_nz(arm_state_t * cpu, uint32_t res, bool destination_is_pc)
{
	if(!destination_is_pc)
	{
		a32_test_nz(cpu, res);
	}
	else
	{
		a32_or_a26_copy_flags(cpu, res);
	}
}

static inline void a32_or_a26_test_nzvc(arm_state_t * cpu, uint32_t res, uint32_t op1, uint32_t op2, bool destination_is_pc)
{
	if(!destination_is_pc)
	{
		a32_test_nzvc(cpu, res, op1, op2);
	}
	else
	{
		a32_or_a26_copy_flags(cpu, res);
	}
}

static inline uint32_t a32_avoid_interworking(arm_state_t * cpu, uint32_t value)
{
	// modifies the least significant bit, so that an interworking assignment will not change modes
	// this is permissible since arithmetic instructions already set the CPSR flags, so the least significant bit is no longer required
	switch(cpu->pstate.jt)
	{
	case PSTATE_JT_ARM:
		return value & ~1;
	case PSTATE_JT_THUMB:
	case PSTATE_JT_THUMBEE:
		return value | 1;
	default:
		assert(false);
	}
}

uint32_t a32_and32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 & op2;
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, res, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

uint32_t a64_and32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 & op2;
	if(set_flags)
	{
		a64_test32_nz(cpu, res);
	}
	return res;
}

uint64_t a64_and64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 & op2;
	if(set_flags)
	{
		a64_test64_nz(cpu, res);
	}
	return res;
}

uint32_t a32_bic32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 & ~op2;
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, res, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

uint32_t a64_bic32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 & ~op2;
	if(set_flags)
	{
		a64_test32_nz(cpu, res);
	}
	return res;
}

uint64_t a64_bic64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 & ~op2;
	if(set_flags)
	{
		a64_test64_nz(cpu, res);
	}
	return res;
}

uint32_t a32_eor32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 ^ op2;
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, res, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

uint32_t a32_orr32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 | op2;
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, res, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

static inline uint32_t a32_add32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 + op2;
	if(set_flags)
	{
		a32_or_a26_test_nzvc(cpu, res, op1, op2, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

static inline uint32_t a64_add32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 + op2;
	if(set_flags)
	{
		a64_test32_nzvc(cpu, res, op1, op2);
	}
	return res;
}

static inline uint64_t a64_add64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 + op2;
	if(set_flags)
	{
		a64_test64_nzvc(cpu, res, op1, op2);
	}
	return res;
}

static inline uint32_t a32_adc32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 + op2 + cpu->pstate.c;
	if(set_flags)
	{
		a32_or_a26_test_nzvc(cpu, res, op1, op2, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

static inline uint32_t a64_adc32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 + op2 + cpu->pstate.c;
	if(set_flags)
	{
		a64_test32_nzvc(cpu, res, op1, op2);
	}
	return res;
}

static inline uint64_t a64_adc64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 + op2 + cpu->pstate.c;
	if(set_flags)
	{
		a64_test64_nzvc(cpu, res, op1, op2);
	}
	return res;
}

static inline uint32_t a32_sub32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 - op2;
	if(set_flags)
	{
		a32_or_a26_test_nzvc(cpu, res, op1, ~op2, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

static inline uint32_t a64_sub32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 - op2;
	if(set_flags)
	{
		a64_test32_nzvc(cpu, res, op1, ~op2);
	}
	return res;
}

static inline uint64_t a64_sub64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 - op2;
	if(set_flags)
	{
		a64_test64_nzvc(cpu, res, op1, ~op2);
	}
	return res;
}

#define a32_rsb32(cpu, op1, op2, set_flags, destination_is_pc) a32_sub32(cpu, op2, op1, set_flags, destination_is_pc)

static inline uint32_t a32_sbc32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags, bool destination_is_pc)
{
	uint32_t res = op1 - op2 + cpu->pstate.c - 1;
	if(set_flags)
	{
		a32_or_a26_test_nzvc(cpu, res, op1, ~op2, destination_is_pc);
	}

	if(destination_is_pc && set_flags)
		return a32_avoid_interworking(cpu, res); // instruction set is controlled by the CPSR
	else
		return res;
}

static inline uint32_t a64_sbc32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool set_flags)
{
	uint32_t res = op1 - op2 + cpu->pstate.c - 1;
	if(set_flags)
	{
		a64_test32_nzvc(cpu, res, op1, ~op2);
	}
	return res;
}

static inline uint64_t a64_sbc64(arm_state_t * cpu, uint64_t op1, uint64_t op2, bool set_flags)
{
	uint64_t res = op1 - op2 + cpu->pstate.c - 1;
	if(set_flags)
	{
		a64_test64_nzvc(cpu, res, op1, ~op2);
	}
	return res;
}

#define a32_rsc32(cpu, op1, op2, set_flags, destination_is_pc) a32_sbc32(cpu, op2, op1, set_flags, destination_is_pc)

#define a32_tst32(cpu, op1, op2, destination_is_pc) ((void)a32_and32(cpu, op1, op2, true, destination_is_pc))

#define a32_teq32(cpu, op1, op2, destination_is_pc) ((void)a32_eor32(cpu, op1, op2, true, destination_is_pc))

#define a32_cmp32(cpu, op1, op2, destination_is_pc) ((void)a32_sub32(cpu, op1, op2, true, destination_is_pc))

#define a32_cmn32(cpu, op1, op2, destination_is_pc) ((void)a32_add32(cpu, op1, op2, true, destination_is_pc))

static inline uint32_t a32_mov32(arm_state_t * cpu, uint32_t op, bool_copy_cpsr_bits_t set_flags, bool destination_is_pc)
{
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, op, destination_is_pc);
	}
	return op;
}

static inline uint32_t a32_mvn32(arm_state_t * cpu, uint32_t op, bool_copy_cpsr_bits_t set_flags, bool destination_is_pc)
{
	if(set_flags)
	{
		a32_or_a26_test_nz(cpu, ~op, destination_is_pc);
	}
	return ~op;
}

static inline uint32_t a32_orn32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool_copy_cpsr_bits_t set_flags)
{
	uint32_t res = op1 | ~op2;
	if(set_flags)
	{
		a32_test_nz(cpu, res);
	}
	return res;
}

static inline uint32_t a32_mul32(arm_state_t * cpu, uint32_t op1, uint32_t op2, bool_copy_cpsr_bits_t set_flags)
{
	uint32_t res = op1 * op2;
	if(set_flags)
	{
		a32_test_nz(cpu, res);
	}
	return res;
}

static inline uint32_t a32_mla32(arm_state_t * cpu, uint32_t op1, uint32_t op2, uint32_t op3, bool_copy_cpsr_bits_t set_flags)
{
	uint32_t res = op1 * op2 + op3;
	if(set_flags)
	{
		a32_test_nz(cpu, res);
	}
	return res;
}

static inline uint32_t a32_mls32(uint32_t op1, uint32_t op2, uint32_t op3)
{
	return op3 - op1 * op2;
}

static inline void a32_smlal32(arm_state_t * cpu, regnum_t low_register, regnum_t high_register, int32_t op1, int32_t op2, bool_copy_cpsr_bits_t set_flags, bool add_register)
{
	uint64_t res = (int64_t)op1 * (int64_t)op2;
	if(add_register)
	{
		res += a32_register_get32(cpu, low_register) | ((uint64_t)a32_register_get32(cpu, high_register) << 32);
	}
	if(set_flags)
	{
		a32_test64_nz(cpu, res);
	}
	a32_register_set32(cpu, low_register, res);
	a32_register_set32(cpu, high_register, res >> 32);
}

static inline void a32_umlal32(arm_state_t * cpu, regnum_t low_register, regnum_t high_register, uint32_t op1, uint32_t op2, bool_copy_cpsr_bits_t set_flags, bool add_register)
{
	uint64_t res = (uint64_t)op1 * (uint64_t)op2;
	if(add_register)
	{
		res += a32_register_get32(cpu, low_register) | ((uint64_t)a32_register_get32(cpu, high_register) << 32);
	}
	if(set_flags)
	{
		a32_test64_nz(cpu, res);
	}
	a32_register_set32(cpu, low_register, res);
	a32_register_set32(cpu, high_register, res >> 32);
}

static inline uint32_t clz32(uint32_t op)
{
	uint32_t position;
	for(position = 0; position < 32; position ++)
	{
		if((op & (1 << (31 - position))) != 0)
			return position;
	}
	return position;
}

static inline uint64_t clz64(uint64_t op)
{
	uint64_t position;
	for(position = 0; position < 64; position ++)
	{
		if((op & (1 << (64 - position))) != 0)
			return position;
	}
	return position;
}

static inline uint32_t cls32(uint32_t op)
{
	uint32_t position;
	for(position = 0; position < 32; position ++)
	{
		if((op & (1 << (31 - position))) == 0)
			return position;
	}
	return position;
}

static inline uint64_t cls64(uint64_t op)
{
	uint64_t position;
	for(position = 0; position < 64; position ++)
	{
		if((op & (1 << (64 - position))) == 0)
			return position;
	}
	return position;
}

static inline uint32_t rbit32(uint32_t value)
{
	uint32_t result = 0;
	for(int i = 0; i < 32; i ++)
	{
		if((value & (1 << i)))
			result |= 1 << (31 - i);
	}
	return result;
}

static inline uint64_t rbit64(uint64_t value)
{
	uint64_t result = 0;
	for(int i = 0; i < 64; i ++)
	{
		if((value & (1 << i)))
			result |= 1 << (63 - i);
	}
	return result;
}

static inline uint32_t bswap_32_16(uint32_t value)
{
	return bswap_16(value) | ((uint32_t)bswap_16(value >> 16) << 16);
}

static inline uint64_t bswap_64_16(uint64_t value)
{
	return bswap_16(value) | ((uint64_t)bswap_16(value >> 16) << 16) | ((uint64_t)bswap_16(value >> 32) << 32) | ((uint64_t)bswap_16(value >> 48) << 48);
}

static inline uint64_t bswap_64_32(uint64_t value)
{
	return bswap_32(value) | ((uint64_t)bswap_32(value >> 32) << 32);
}

static inline uint32_t a32_bfc32(arm_state_t * cpu, uint32_t value, uint8_t lsb, uint8_t msb)
{
	return value & (((uint32_t)-2 << msb) | ((1 << lsb) - 1));
}

static inline uint32_t a32_bfi32(arm_state_t * cpu, uint32_t base, uint32_t pattern, uint8_t lsb, uint8_t msb)
{
	return a32_bfc32(cpu, base, lsb, msb) | ((pattern << lsb) & ((2 << msb) - 1));
}

static inline uint32_t sxtb32(uint32_t value, uint8_t rotate)
{
	return sign_extend(8, rotate_right32(value, rotate));
}

static inline uint32_t uxtb32(uint32_t value, uint8_t rotate)
{
	return rotate_right32(value, rotate) & 0xFF;
}

static inline uint32_t sxth32(uint32_t value, uint8_t rotate)
{
	return sign_extend(16, rotate_right32(value, rotate));
}

static inline uint32_t uxth32(uint32_t value, uint8_t rotate)
{
	return rotate_right32(value, rotate) & 0xFFFF;
}

static inline uint64_t umulh64(uint64_t op1, uint64_t op2)
{
	// TODO: what if __int128 is not defined
	unsigned __int128 result = op1;
	result *= op2;
	return result >> 64;
}

static inline int64_t smulh64(int64_t op1, int64_t op2)
{
	// TODO: what if __int128 is not defined
	__int128 result = op1;
	result *= op2;
	return result >> 64;
}

static inline int8_t qadd8(int8_t op1, int8_t op2)
{
	if(op2 >= 0 && op1 > 0x7F - op2)
	{
		return 0x7F;
	}
	else if(op2 < 0 && op1 < -0x80 - op2)
	{
		return -0x80;
	}
	else
	{
		return op1 + op2;
	}
}

static inline uint8_t uqadd8(uint8_t op1, uint8_t op2)
{
	if(op1 > 0xFFU - op2)
	{
		return 0xFFU;
	}
	else
	{
		return op1 + op2;
	}
}

static inline int16_t qadd16(int16_t op1, int16_t op2)
{
	if(op2 >= 0 && op1 > 0x7FFF - op2)
	{
		return 0x7FFF;
	}
	else if(op2 < 0 && op1 < -0x8000 - op2)
	{
		return -0x8000;
	}
	else
	{
		return op1 + op2;
	}
}

static inline uint16_t uqadd16(uint16_t op1, uint16_t op2)
{
	if(op1 > 0xFFFFU - op2)
	{
		return 0xFFFFU;
	}
	else
	{
		return op1 + op2;
	}
}

static inline uint32_t a32_qadd32(arm_state_t * cpu, int32_t op1, int32_t op2)
{
	if(op2 >= 0 && op1 > 0x7FFFFFFF - op2)
	{
		cpu->pstate.q = 1;
		return 0x7FFFFFFF;
	}
	else if(op2 < 0 && op1 < -0x80000000 - op2)
	{
		cpu->pstate.q = 1;
		return -0x80000000;
	}
	else
	{
		return op1 + op2;
	}
}

static inline uint32_t a32_qdadd32(arm_state_t * cpu, int32_t op1, int32_t op2)
{
	return a32_qadd32(cpu, op1, a32_qadd32(cpu, op2, op2));
}

static inline int8_t qsub8(int8_t op1, int8_t op2)
{
	if(op2 >= 0 && op1 < -0x80 + op2)
	{
		return -0x80;
	}
	else if(op2 < 0 && op1 > 0x7F + op2)
	{
		return 0x7F;
	}
	else
	{
		return op1 - op2;
	}
}

static inline uint8_t uqsub8(uint8_t op1, uint8_t op2)
{
	if(op1 < op2)
	{
		return 0;
	}
	else if(op1 > 0xFFU + op2)
	{
		return 0xFFU;
	}
	else
	{
		return op1 - op2;
	}
}

static inline int16_t qsub16(int16_t op1, int16_t op2)
{
	if(op2 >= 0 && op1 < -0x8000 + op2)
	{
		return -0x8000;
	}
	else if(op2 < 0 && op1 > 0x7FFF + op2)
	{
		return 0x7FFF;
	}
	else
	{
		return op1 - op2;
	}
}

static inline uint16_t uqsub16(uint16_t op1, uint16_t op2)
{
	if(op1 < op2)
	{
		return 0;
	}
	else if(op1 > 0xFFFFU + op2)
	{
		return 0xFFFFU;
	}
	else
	{
		return op1 - op2;
	}
}

static inline uint32_t a32_qsub32(arm_state_t * cpu, int32_t op1, int32_t op2)
{
	if(op2 >= 0 && op1 < -0x80000000 + op2)
	{
		cpu->pstate.q = 1;
		return -0x80000000;
	}
	else if(op2 < 0 && op1 > 0x7FFFFFFF + op2)
	{
		cpu->pstate.q = 1;
		return 0x7FFFFFFF;
	}
	else
	{
		return op1 - op2;
	}
}

static inline uint32_t a32_qdsub32(arm_state_t * cpu, int32_t op1, int32_t op2)
{
	return a32_qsub32(cpu, op1, a32_qadd32(cpu, op2, op2));
}

static inline int32_t a32_smla32(arm_state_t * cpu, int16_t op1, int16_t op2, int32_t op3)
{
	return a32_qadd32(cpu, (int32_t)op1 * (int32_t)op2, op3);
}

static inline int32_t a32_smul32(arm_state_t * cpu, int16_t op1, int16_t op2)
{
	return (int32_t)op1 * (int32_t)op2;
}

static inline void a32_smlal64(arm_state_t * cpu, regnum_t lo, regnum_t hi, int16_t op1, int16_t op2)
{
	int64_t result = (int32_t)op1 * (int32_t)op2;
	result += a32_register_get32(cpu, lo);
	a32_register_set32(cpu, lo, result);
	result = (result >> 32) + a32_register_get32(cpu, hi);
	a32_register_set32(cpu, hi, result);
}

static inline int32_t a32_smlaw32(arm_state_t * cpu, int32_t op1, int16_t op2, int32_t op3)
{
	int32_t result = ((int64_t)op1 * (int64_t)op2) >> 32;
	return a32_qadd32(cpu, result, op3);
}

static inline int8_t a32_sadd8(arm_state_t * cpu, int8_t op1, int8_t op2, int ge_shift)
{
	int16_t result = (int16_t)op1 + (int16_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 1 << ge_shift;
	else
		cpu->pstate.ge &= ~(1 << ge_shift);
	return result;
}

static inline uint8_t a32_uadd8(arm_state_t * cpu, uint8_t op1, uint8_t op2, int ge_shift)
{
	uint16_t result = (uint16_t)op1 + (uint16_t)op2;
	if(result >= 0x100)
		cpu->pstate.ge |= 1 << ge_shift;
	else
		cpu->pstate.ge &= ~(1 << ge_shift);
	return result;
}

static inline int16_t a32_sadd16(arm_state_t * cpu, int16_t op1, int16_t op2, int ge_shift)
{
	int32_t result = (int32_t)op1 + (int32_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 3 << ge_shift;
	else
		cpu->pstate.ge &= ~(3 << ge_shift);
	return result;
}

static inline uint16_t a32_uadd16(arm_state_t * cpu, uint16_t op1, uint16_t op2, int ge_shift)
{
	uint32_t result = (uint32_t)op1 + (uint32_t)op2;
	if(result >= 0x10000)
		cpu->pstate.ge |= 3 << ge_shift;
	else
		cpu->pstate.ge &= ~(3 << ge_shift);
	return result;
}

static inline int8_t a32_ssub8(arm_state_t * cpu, int8_t op1, int8_t op2, int ge_shift)
{
	int16_t result = (int16_t)op1 - (int16_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 1 << ge_shift;
	else
		cpu->pstate.ge &= ~(1 << ge_shift);
	return result;
}

static inline uint8_t a32_usub8(arm_state_t * cpu, uint8_t op1, uint8_t op2, int ge_shift)
{
	int16_t result = (int16_t)op1 - (int16_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 1 << ge_shift;
	else
		cpu->pstate.ge &= ~(1 << ge_shift);
	return result;
}

static inline int16_t a32_ssub16(arm_state_t * cpu, int16_t op1, int16_t op2, int ge_shift)
{
	int32_t result = (int32_t)op1 - (int32_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 3 << ge_shift;
	else
		cpu->pstate.ge &= ~(3 << ge_shift);
	return result;
}

static inline uint16_t a32_usub16(arm_state_t * cpu, uint16_t op1, uint16_t op2, int ge_shift)
{
	int32_t result = (int32_t)op1 - (int32_t)op2;
	if(result >= 0)
		cpu->pstate.ge |= 3 << ge_shift;
	else
		cpu->pstate.ge &= ~(3 << ge_shift);
	return result;
}

static inline int8_t a32_shadd8(int8_t op1, int8_t op2)
{
	int16_t result = (int16_t)op1 + (int16_t)op2;
	return result >> 1;
}

static inline uint8_t a32_uhadd8(uint8_t op1, uint8_t op2)
{
	uint16_t result = (uint16_t)op1 + (uint16_t)op2;
	return result >> 1;
}

static inline int16_t a32_shadd16(int16_t op1, int16_t op2)
{
	int32_t result = (int32_t)op1 + (int32_t)op2;
	return result >> 1;
}

static inline uint16_t a32_uhadd16(uint16_t op1, uint16_t op2)
{
	uint32_t result = (uint32_t)op1 + (uint32_t)op2;
	return result >> 1;
}

static inline int8_t a32_shsub8(int8_t op1, int8_t op2)
{
	int16_t result = (int16_t)op1 - (int16_t)op2;
	return result >> 1;
}

static inline uint8_t a32_uhsub8(uint8_t op1, uint8_t op2)
{
	uint16_t result = (uint16_t)op1 - (uint16_t)op2;
	return result >> 1;
}

static inline int16_t a32_shsub16(int16_t op1, int16_t op2)
{
	int32_t result = (int32_t)op1 - (int32_t)op2;
	return result >> 1;
}

static inline uint16_t a32_uhsub16(uint16_t op1, uint16_t op2)
{
	uint32_t result = (uint32_t)op1 - (uint32_t)op2;
	return result >> 1;
}

static inline int32_t a32_smulw32(arm_state_t * cpu, int32_t op1, int16_t op2)
{
	return ((int64_t)op1 * (int64_t)op2) >> 32;
}

static inline void e32_break_index_check(arm_state_t * cpu)
{
	if(!cpu->capture_breaks)
	{
		a32_register_set32(cpu, A32_LR, cpu->r[PC] | 1);
		cpu->pstate.it = 0;
		cpu->r[PC] = cpu->teehbr - 8;
	}
	else
	{
		cpu->result = ARM_EMU_THUMBEE_OUT_OF_BOUNDS;
	}
	longjmp(cpu->exc, 1);
}

static inline void e32_check_nullptr(arm_state_t * cpu, uint32_t address)
{
	if(cpu->config.version == ARMV7 && cpu->pstate.jt == PSTATE_JT_THUMBEE)
	{
		if(address == 0)
		{
			if(!cpu->capture_breaks)
			{
				a32_register_set32(cpu, A32_LR, cpu->r[PC] | 1);
				cpu->pstate.it = 0;
				cpu->r[PC] = cpu->teehbr - 4;
			}
			else
			{
				cpu->result = ARM_EMU_THUMBEE_NULLPTR;
			}
			longjmp(cpu->exc, 1);
		}
	}
}

static inline void a26_check_address(arm_state_t * cpu, uint32_t address)
{
	if(a32_is_data26(cpu))
	{
		if((address & 0xFC000000) != 0)
		{
			a32_address_exception(cpu);
		}
	}
}

static inline uint32_t a32_ldrb(arm_state_t * cpu, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	a26_check_address(cpu, address);

	uint32_t result =
		usermode
			? a32_read8_user_mode(cpu, address)
			: a32_read8(cpu, address);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}

	return result;
}

static inline uint32_t a32_ldrsb(arm_state_t * cpu, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	a26_check_address(cpu, address);
	uint32_t result =
		usermode
			? a32_read8_user_mode(cpu, address)
			: a32_read8(cpu, address);
	result = sign_extend(8, result);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}

	return result;
}

static inline void a32_strb(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	a26_check_address(cpu, address);

	if(usermode)
		a32_write8_user_mode(cpu, address, value);
	else
		a32_write8(cpu, address, value);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline uint32_t a32_ldrh(arm_state_t * cpu, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address);
	uint32_t result =
		usermode
			? a32_read16_user_mode(cpu, address)
			: a32_read16(cpu, address);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}

	return result;
}

static inline uint32_t a32_ldrsh(arm_state_t * cpu, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address);
	uint32_t result =
		usermode
			? a32_read16_user_mode(cpu, address)
			: a32_read16(cpu, address);
	result = sign_extend(16, result);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}

	return result;
}

static inline void a32_strh(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address);
	if(usermode)
		a32_write16_user_mode(cpu, address, value);
	else
		a32_write16(cpu, address, value);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline uint32_t a32_ldr(arm_state_t * cpu, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	a26_check_address(cpu, address);

	uint32_t result;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);

		result =
			usermode
				? a32_read32_user_mode(cpu, address)
				: a32_read32(cpu, address);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		result =
			usermode
				? a32_read32_user_mode(cpu, address & ~3)
				: a32_read32(cpu, address & ~3);

		result = rotate_right32(result, (address & 3) * 8);
	}
	else
	{
		result =
			usermode
				? a32_read32_user_mode(cpu, address)
				: a32_read32(cpu, address);
	}

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}

	return result;
}

static inline void a32_str(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback, bool_usermode_t usermode)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	a26_check_address(cpu, address);
	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);

		if(usermode)
			a32_write32_user_mode(cpu, address, value);
		else
			a32_write32(cpu, address, value);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		if(usermode)
			a32_write32_user_mode(cpu, address & ~3, value);
		else
			a32_write32(cpu, address & ~3, value);
	}
	else
	{
		if(usermode)
			a32_write32_user_mode(cpu, address, value);
		else
			a32_write32(cpu, address, value);
	}

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline void a32_ldrd(arm_state_t * cpu, regnum_t operand1, regnum_t operand2, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	uint32_t actual_address;

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	actual_address = address;

	if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		if((cpu->sctlr_el1 & SCTLR_A) != 0)
		{
			if((address & 7) != 0)
				arm_unaligned(cpu);
		}
		else
		{
			actual_address &= ~7;
		}
	}
	else
	{
		// note: the difference between word-alignment and double-word alignment here is intentional
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	a32_register_set32(cpu, operand1, a32_read32(cpu, actual_address));
	a32_register_set32(cpu, operand2, a32_read32(cpu, actual_address + 4));

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline void a32_strd(arm_state_t * cpu, regnum_t operand1, regnum_t operand2, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	uint32_t actual_address;

	if(preindexed)
	{
		address += offset;
	}

	actual_address = address;

	if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		if((cpu->sctlr_el1 & SCTLR_A) != 0)
		{
			if((address & 7) != 0)
				arm_unaligned(cpu);
		}
		else
		{
			actual_address &= ~7;
		}
	}
	else
	{
		// note: the difference between word-alignment and double-word alignment here is intentional
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	a32_write32(cpu, actual_address, a32_register_get32(cpu, operand1));
	a32_write32(cpu, actual_address + 4, a32_register_get32(cpu, operand2));

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline void a32_ldm(arm_state_t * cpu, uint16_t register_list, regnum_t stack_register, bool upward, bool change_before, bool writeback, bool include_cpsr /* also user mode */)
{
	unsigned stacksize = count_bits16(register_list) << 2;
	uint32_t address = a32_register_get32(cpu, stack_register);
	uint32_t update;
	uint32_t final_address;

	if(upward)
	{
		update = 4;
		final_address = address + stacksize;
	}
	else
	{
		update = -4;
		address -= stacksize - 4;
		final_address = address - 4;
	}

	if(change_before)
	{
		address += update;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	bool user_mode = !(register_list & 0x8000) && include_cpsr && !writeback;
	a26_check_address(cpu, address); // only the first address needs checking
	for(unsigned register_number = 0; register_number < 15; register_number++)
	{
		if((register_list & (1 << register_number)) != 0)
		{
			if(user_mode)
				cpu->r[register_number] = a32_read32(cpu, address);
			else
				a32_register_set32_interworking_v5(cpu, register_number, a32_read32(cpu, address));
			address += 4;
		}
	}

	if((register_list & 0x8000))
	{
		// R15 included
		uint32_t word = a32_read32(cpu, address);
		cpu->r[PC] = word & ~1;
		if((cpu->config.features & (1 << FEATURE_THUMB)))
			cpu->pstate.jt = word & 1 ? PSTATE_JT_THUMB : PSTATE_JT_ARM;
		if(include_cpsr)
		{
			a32_or_a26_copy_flags(cpu, word);
		}
	}

	if(writeback && !(register_list & (1 << stack_register)))
	{
		// for write-back, if Rn is not in the list, update it
		a32_register_set32(cpu, stack_register, final_address);
	}
}

static inline void a32_stm(arm_state_t * cpu, uint16_t register_list, regnum_t stack_register, bool upward, bool change_before, bool writeback, bool user_mode)
{
	unsigned stacksize = count_bits16(register_list) << 2;
	uint32_t address = a32_register_get32(cpu, stack_register);
	uint32_t update;
	uint32_t final_address;

	if(upward)
	{
		update = 4;
		final_address = address + stacksize;
	}
	else
	{
		update = -4;
		address -= stacksize - 4;
		final_address = address - 4;
	}

	if(change_before)
	{
		address += update;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	uint16_t test_mask = (1 << (stack_register + 1)) - 1; // masks all registers including or lower than Rn
	uint16_t test_value = 1 << stack_register; // Rn is included, but all lower numbered registers are not
	bool stack_register_is_lowest = (register_list & test_mask) == test_value;

	if(!stack_register_is_lowest && writeback)
	{
		a32_register_set32(cpu, stack_register, final_address);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	for(unsigned register_number = 0; register_number < 16; register_number++)
	{
		if((register_list & (1 << register_number)) != 0)
		{
			uint32_t value;
			if(user_mode)
				value = cpu->r[register_number];
			else
				value = a32_register_get32_lhs(cpu, register_number);
			if(register_number == A32_PC_NUM)
			{
				value += a32_get_stored_pc_displacement(cpu);
			}

			a32_write32(cpu, address, value);
			address += 4;
		}
	}

	if(stack_register_is_lowest && writeback)
	{
		a32_register_set32(cpu, stack_register, final_address);
	}
}

static inline void a32_rfe(arm_state_t * cpu, regnum_t stack_register, bool upward, bool change_before, bool writeback)
{
	unsigned stacksize = 8;
	uint32_t address = a32_register_get32(cpu, stack_register);
	uint32_t update;
	uint32_t final_address;

	if(upward)
	{
		update = 4;
		final_address = address + stacksize;
	}
	else
	{
		update = -4;
		address -= stacksize - 4;
		final_address = address - 4;
	}

	if(change_before)
	{
		address += update;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	cpu->r[PC] = a32_read32(cpu, address);
	a32_set_cpsr(cpu, ~(uint32_t)0,
		a32_read32(cpu, address + 4));

	if(writeback)
	{
		// for write-back, if Rn is not in the list, update it
		a32_register_set32(cpu, stack_register, final_address);
	}
}

static inline void a32_srs(arm_state_t * cpu, int mode, bool upward, bool change_before, bool writeback)
{
	unsigned stacksize = 8;
	uint32_t address = a32_register_mode(cpu, A32_SP, mode);
	uint32_t update;
	uint32_t final_address;

	if(upward)
	{
		update = 4;
		final_address = address + stacksize;
	}
	else
	{
		update = -4;
		address -= stacksize - 4;
		final_address = address - 4;
	}

	if(change_before)
	{
		address += update;
	}

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	a32_write32(cpu, address, a32_register_get32(cpu, A32_LR));
	a32_write32(cpu, address + 4, a32_get_spsr(cpu));

	if(writeback)
	{
		a32_register_mode(cpu, A32_SP, mode) = final_address;
	}
}

static inline void a32_mark_exclusive(arm_state_t * cpu, uint64_t base, size_t size)
{
	// TODO: set exclusive_procid
	cpu->exclusive_start = base;
	cpu->exclusive_end = base + size - 1;
}

static inline bool a32_check_exclusive(arm_state_t * cpu, uint64_t base, size_t size)
{
	// TODO: check exclusive_procid
	return cpu->exclusive_start < base + size && base <= cpu->exclusive_end;
}

static inline void a32_clear_exclusive(arm_state_t * cpu)
{
	// TODO: clear exclusive_procid
	cpu->exclusive_start = (uint32_t)-1;
	cpu->exclusive_end = 0;
}

static inline uint32_t a32_ldrexb(arm_state_t * cpu, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	a26_check_address(cpu, address);
	a32_mark_exclusive(cpu, address, 1);
	return a32_read8(cpu, address);
}

static inline uint32_t a32_ldrexh(arm_state_t * cpu, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~1;
	}
	else
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address);
	a32_mark_exclusive(cpu, address, 2);
	return a32_read16(cpu, address);
}

static inline uint32_t a32_ldrex(arm_state_t * cpu, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address);
	a32_mark_exclusive(cpu, address, 4);	
	return a32_read8(cpu, address);
}

static inline void a32_ldrexd(arm_state_t * cpu, regnum_t operand1, regnum_t operand2, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 7) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~7;
	}
	else
	{
		if((address & 7) != 0)
			arm_unaligned(cpu);
	}

	a26_check_address(cpu, address); // only the first address needs checking
	a32_check_exclusive(cpu, address, 8);
	a32_register_set32(cpu, operand1, a32_read32(cpu, address));
	a32_register_set32(cpu, operand2, a32_read32(cpu, address + 4));
}

static inline uint32_t a32_strexb(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if(a32_check_exclusive(cpu, address, 1))
	{
		a26_check_address(cpu, address);
		a32_write8(cpu, address, value);
		return 0;
	}
	else
	{
		return 1;
	}
}

static inline uint32_t a32_strexh(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~1;
	}
	else
	{
		if((address & 1) != 0)
			arm_unaligned(cpu);
	}

	if(a32_check_exclusive(cpu, address, 2))
	{
		a26_check_address(cpu, address);
		a32_write16(cpu, address, value);
		return 0;
	}
	else
	{
		return 1;
	}
}

static inline uint32_t a32_strex(arm_state_t * cpu, uint32_t value, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	if(a32_check_exclusive(cpu, address, 4))
	{
		a26_check_address(cpu, address);
		a32_write32(cpu, address, value);
		return 0;
	}
	else
	{
		return 1;
	}
}

static inline uint32_t a32_strexd(arm_state_t * cpu, regnum_t operand1, regnum_t operand2, regnum_t base, uint32_t offset)
{
	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	address += offset;

	if((cpu->sctlr_el1 & SCTLR_A) != 0)
	{
		if((address & 7) != 0)
			arm_unaligned(cpu);
	}
	else if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~7;
	}
	else
	{
		if((address & 7) != 0)
			arm_unaligned(cpu);
	}

	if(a32_check_exclusive(cpu, address, 4))
	{
		a26_check_address(cpu, address); // only the first address needs checking
		a32_write32(cpu, address, a32_register_get32(cpu, operand1));
		a32_write32(cpu, address + 4, a32_register_get32(cpu, operand2));
		return 0;
	}
	else
	{
		return 1;
	}
}

#define OP32 false
#define OP64 true
#define MEM8 1
#define MEM16 2
#define MEM32 4
#define MEM64 8
#define SIGNED true
#define UNSIGNED false

static inline void a64_ldr(arm_state_t * cpu, bool operand64, size_t bytes, bool is_signed, regnum_t operand, regnum_t base, uint64_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint64_t address = base != PC ? a64_register_get64(cpu, base, false) : 0;

	if(base == A64_SP && (address & 0xF) != 0)
	{
		a64_unaligned_sp(cpu);
	}

	if(preindexed)
	{
		address += offset;
	}

	uint64_t value;
	switch(bytes)
	{
	case 1:
		value = a64_read8(cpu, address);
		if(is_signed)
			value = sign_extend(8, value);
		break;
	case 2:
		value = a64_read16(cpu, address);
		if(is_signed)
			value = sign_extend(16, value);
		break;
	case 4:
		value = a64_read32(cpu, address);
		if(is_signed)
			value = sign_extend(32, value);
		break;
	case 8:
		value = a64_read64(cpu, address);
		break;
	}

	if(!operand64)
		a64_register_set32(cpu, operand, SUPPRESS_SP, value);
	else
		a64_register_set64(cpu, operand, SUPPRESS_SP, value);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a64_register_set64(cpu, base, false, address);
	}
}

static inline void a64_str(arm_state_t * cpu, bool operand64, size_t bytes, regnum_t operand, regnum_t base, uint64_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint64_t address = base != PC ? a64_register_get64(cpu, base, false) : 0;

	if(base == A64_SP && (address & 0xF) != 0)
	{
		a64_unaligned_sp(cpu);
	}

	if(preindexed)
	{
		address += offset;
	}

	uint64_t value;

	if(!operand64)
		value = a64_register_get32(cpu, operand, SUPPRESS_SP);
	else
		value = a64_register_get64(cpu, operand, SUPPRESS_SP);

	switch(bytes)
	{
	case 1:
		a64_write8(cpu, address, value);
		break;
	case 2:
		a64_write16(cpu, address, value);
		break;
	case 4:
		a64_write32(cpu, address, value);
		break;
	case 8:
		a64_write64(cpu, address, value);
		break;
	}

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a64_register_set64(cpu, base, false, address);
	}
}

static inline void a64_ldp(arm_state_t * cpu, bool operand64, size_t bytes, bool is_signed, regnum_t operand1, regnum_t operand2, regnum_t base, uint64_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint64_t address = base != PC ? a64_register_get64(cpu, base, false) : 0;

	if(base == A64_SP && (address & 0xF) != 0)
	{
		a64_unaligned_sp(cpu);
	}

	if(preindexed)
	{
		address += offset;
	}

	uint64_t value1;
	uint64_t value2;
	switch(bytes)
	{
	case 4:
		value1 = a64_read32(cpu, address);
		value2 = a64_read32(cpu, address + 4);
		if(is_signed)
		{
			value1 = sign_extend(32, value1);
			value2 = sign_extend(32, value2);
		}
		break;
	case 8:
		value1 = a64_read64(cpu, address);
		value2 = a64_read64(cpu, address + 8);
		break;
	}

	if(!operand64)
	{
		a64_register_set32(cpu, operand1, SUPPRESS_SP, value1);
		a64_register_set32(cpu, operand2, SUPPRESS_SP, value2);
	}
	else
	{
		a64_register_set64(cpu, operand1, SUPPRESS_SP, value1);
		a64_register_set64(cpu, operand2, SUPPRESS_SP, value2);
	}

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a64_register_set64(cpu, base, false, address);
	}
}

static inline void a64_stp(arm_state_t * cpu, bool operand64, size_t bytes, regnum_t operand1, regnum_t operand2, regnum_t base, uint64_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint64_t address = base != PC ? a64_register_get64(cpu, base, false) : 0;

	if(base == A64_SP && (address & 0xF) != 0)
	{
		a64_unaligned_sp(cpu);
	}

	if(preindexed)
	{
		address += offset;
	}

	uint64_t value1;
	uint64_t value2;
	if(!operand64)
	{
		value1 = a64_register_get32(cpu, operand1, SUPPRESS_SP);
		value2 = a64_register_get32(cpu, operand2, SUPPRESS_SP);
	}
	else
	{
		value1 = a64_register_get64(cpu, operand1, SUPPRESS_SP);
		value2 = a64_register_get64(cpu, operand2, SUPPRESS_SP);
	}

	switch(bytes)
	{
	case 4:
		a64_write32(cpu, address,     value1);
		a64_write32(cpu, address + 4, value2);
		break;
	case 8:
		a64_write64(cpu, address,     value1);
		a64_write64(cpu, address + 8, value2);
		break;
	}

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a64_register_set64(cpu, base, false, address);
	}
}

static inline uint64_t a64_extend(uint64_t value, uint8_t operation, uint8_t amount)
{
	switch(operation)
	{
	case 0b000:
		value &= 0x000000FF;
		break;
	case 0b001:
		value &= 0x0000FFFF;
		break;
	case 0b010:
		value &= 0xFFFFFFFF;
		break;
	case 0b011:
		break;
	case 0b100:
		value = sign_extend64(8, value);
		break;
	case 0b101:
		value = sign_extend64(16, value);
		break;
	case 0b110:
		value = sign_extend64(32, value);
		break;
	case 0b111:
		break;
	}

	return value << amount;
}

const float80_t fpa_operands[] = { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 0.5, 10.0 };

static inline float80_t a32_fetch_fpa_operand(arm_state_t * cpu, uint32_t opcode)
{
	if((opcode & 0x00000008))
	{
		return fpa_operands[opcode & 7];
	}
	else
	{
		return cpu->fpa.f[opcode & 7];
	}
}

static const regnum_t a32_banked_register_numbers[32] =
{
	8, 9, 10, 11, 12, 13, 14, -1,
	R8_FIQ, R9_FIQ, R10_FIQ, R11_FIQ, R12_FIQ, R13_FIQ, R14_FIQ, -1, // +24
	R14_IRQ, R13_IRQ, R14_SVC, R13_SVC, R14_ABT, R13_ABT, R14_UND, R13_UND, // +16
	R14_MON, R13_MON, R14_MON, R13_MON, ELR_HYP, R13_HYP, ELR_HYP, R13_HYP,
};

static const regnum_t a32_banked_spsr_numbers[] =
{
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, SPSR_FIQ, -1,
	SPSR_IRQ, -1, SPSR_SVC, -1, SPSR_ABT, -1, SPSR_UND, -1,
	-1, -1, -1, -1, SPSR_MON, -1, SPSR_HYP, -1,
};

static inline void a32_perform_cdp(arm_state_t * cpu, uint32_t opcode)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_cdp == NULL)
		arm_undefined(cpu);

	cpu->coproc[coproc].perform_cdp(cpu, opcode);
}

static inline void t32_perform_cdp(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2)
{
	a32_perform_cdp(cpu, ((uint32_t)opcode1 << 16) | opcode2);
}

static inline void a32_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_ldc_stc == NULL)
		arm_undefined(cpu);

	uint32_t address = a32_register_get32(cpu, base);

	if(base == A32_PC_NUM)
	{
		address &= ~3;
	}

	e32_check_nullptr(cpu, address);

	if(preindexed)
	{
		address += offset;
	}

	if(cpu->config.version <= ARMV6 && !(cpu->sctlr_el1 & SCTLR_U))
	{
		address &= ~3;
	}
	else
	{
		if((address & 3) != 0)
			arm_unaligned(cpu);
	}

	cpu->coproc[coproc].perform_ldc_stc(cpu, opcode, address);

	if(writeback)
	{
		if(!preindexed)
		{
			address += offset;
		}

		a32_register_set32(cpu, base, address);
	}
}

static inline void t32_perform_ldc_stc(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2, regnum_t base, uint32_t offset, bool_indexing_type_t preindexed, bool_writeback_t writeback)
{
	a32_perform_ldc_stc(cpu, ((uint32_t)opcode1 << 16) | opcode2, base, offset, preindexed, writeback);
}

static void a32_perform_mcr(arm_state_t * cpu, uint32_t opcode, regnum_t reg)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_mcr == NULL)
		arm_undefined(cpu);

	uint32_t value = a32_register_get32(cpu, reg);
	if(reg == A32_PC_NUM)
	{
		value += a32_get_mcr_offset_for_pc(cpu);
	}

	cpu->coproc[coproc].perform_mcr(cpu, opcode, a32_register_get32(cpu, reg));
}

static void t32_perform_mcr(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2, regnum_t reg)
{
	a32_perform_mcr(cpu, ((uint32_t)opcode1 << 16) | opcode2, reg);
}

static void a32_perform_mcrr(arm_state_t * cpu, uint32_t opcode, regnum_t reg1, regnum_t reg2)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_mcrr == NULL)
		arm_undefined(cpu);

	cpu->coproc[coproc].perform_mcrr(cpu, opcode, a32_register_get32(cpu, reg1), a32_register_get32(cpu, reg2));
}

static void t32_perform_mcrr(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2, regnum_t reg1, regnum_t reg2)
{
	a32_perform_mcrr(cpu, ((uint32_t)opcode1 << 16) | opcode2, reg1, reg2);
}

static void a32_perform_mrc(arm_state_t * cpu, uint32_t opcode, regnum_t reg)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_mrc == NULL)
		arm_undefined(cpu);

	uint32_t result = cpu->coproc[coproc].perform_mrc(cpu, opcode);
	if(reg != A32_PC_NUM)
	{
		a32_register_set32(cpu, reg, result);
	}
	else
	{
		a32_set_cpsr_nzcv(cpu, result);
	}
}

static void t32_perform_mrc(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2, regnum_t reg)
{
	a32_perform_mrc(cpu, ((uint32_t)opcode1 << 16) | opcode2, reg);
}

static void a32_perform_mrrc(arm_state_t * cpu, uint32_t opcode, regnum_t reg1, regnum_t reg2)
{
	uint8_t coproc = (opcode >> 8) & 0xF;

	if(cpu->coproc[coproc].perform_mrrc == NULL)
		arm_undefined(cpu);

	uint32_pair_t result = cpu->coproc[coproc].perform_mrrc(cpu, opcode);
	a32_register_set32(cpu, reg1, result.l);
	a32_register_set32(cpu, reg2, result.h);
}

static void t32_perform_mrrc(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2, regnum_t reg1, regnum_t reg2)
{
	a32_perform_mrrc(cpu, ((uint32_t)opcode1 << 16) | opcode2, reg1, reg2);
}

void a64_perform_sys(arm_state_t * cpu, uint32_t opcode, uint64_t value)
{
	// TODO
}

uint64_t a64_perform_sysl(arm_state_t * cpu, uint32_t opcode)
{
	// TODO
	return 0;
}

uint64_t a64_perform_mrs(arm_state_t * cpu, uint32_t opcode)
{
	uint8_t op0 = (opcode >> 19) & 3;
	uint8_t opc1 = (opcode >> 16) & 7;
	uint8_t cr1 = (opcode >> 12) & 0xF;
	uint8_t cr2 = (opcode >> 8) & 0xF;
	uint8_t opc2 = (opcode >> 5) & 7;

	// TODO
	switch(op0)
	{
	case 3:
		switch(cr1)
		{
		case 0:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
					case 4:
					case 7:
						return arm_get_midr(cpu);
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						return arm_get_id_pfr0(cpu);
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 1:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->sctlr_el1;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 4:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->sctlr_el2;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 6:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->sctlr_el3;
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						return cpu->scr_el3;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 4:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->sctlr_el2;
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						return cpu->hcr_el2;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 12:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->vbar_el1;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 4:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->vbar_el2;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 6:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						return cpu->vbar_el3;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		default:
			arm_undefined(cpu);
		}
	default:
		arm_undefined(cpu);
	}
}

void a64_perform_msr(arm_state_t * cpu, uint32_t opcode, uint64_t value)
{
	uint8_t op0 = (opcode >> 19) & 3;
	uint8_t opc1 = (opcode >> 16) & 7;
	uint8_t cr1 = (opcode >> 12) & 0xF;
	uint8_t cr2 = (opcode >> 8) & 0xF;
	uint8_t opc2 = (opcode >> 5) & 7;

	// TODO
	switch(op0)
	{
	case 3:
		switch(cr1)
		{
		case 0:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
					case 4:
					case 7:
						// read only
						//return arm_get_midr(cpu);
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						// read only
						//return arm_get_id_pfr0(cpu);
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 1:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->sctlr_el1 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 4:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->sctlr_el2 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 6:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->sctlr_el3 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						cpu->scr_el3 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 4:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->sctlr_el2 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				case 1:
					switch(opc2)
					{
					case 0:
						cpu->hcr_el2 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		case 12:
			switch(opc1)
			{
			case 0:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->vbar_el1 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 4:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->vbar_el2 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			case 6:
				switch(cr2)
				{
				case 0:
					switch(opc2)
					{
					case 0:
						cpu->vbar_el3 = value;
						break;
					default:
						arm_undefined(cpu);
					}
				default:
					arm_undefined(cpu);
				}
			default:
				arm_undefined(cpu);
			}
		default:
			arm_undefined(cpu);
		}
	default:
		arm_undefined(cpu);
	}
}

/* Floating point instructions */

static inline void a32_cmp32fp(arm_state_t * cpu, float32_t op1, float32_t op2)
{
	if(isnan(op1) || isnan(op2))
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_C | FPSCR_V);
	}
	else if(op1 < op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_N);
	}
	else if(op1 == op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_Z | FPSCR_C);
	}
	else if(op1 > op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_C);
	}
}

static inline void a32_cmp64fp(arm_state_t * cpu, float64_t op1, float64_t op2)
{
	if(isnan(op1) || isnan(op2))
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_C | FPSCR_V);
	}
	else if(op1 < op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_N);
	}
	else if(op1 == op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_Z | FPSCR_C);
	}
	else if(op1 > op2)
	{
		set_fpscr_bits(cpu, FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V, FPSCR_C);
	}
}

/* Coprocessors */

void cp1_perform_cdp(arm_state_t * cpu, uint32_t opcode);
void cp1_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
void cp1_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
uint32_t cp1_perform_mrc(arm_state_t * cpu, uint32_t opcode);

//void cp2_perform_cdp(arm_state_t * cpu, uint32_t opcode);
void cp2_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
//void cp2_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
//uint32_t cp2_perform_mrc(arm_state_t * cpu, uint32_t opcode);

void cp10_perform_cdp(arm_state_t * cpu, uint32_t opcode);
void cp10_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
void cp10_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
uint32_t cp10_perform_mrc(arm_state_t * cpu, uint32_t opcode);

void cp11_perform_cdp(arm_state_t * cpu, uint32_t opcode);
void cp11_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
void cp11_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
uint32_t cp11_perform_mrc(arm_state_t * cpu, uint32_t opcode);
void cp11_perform_mcrr(arm_state_t * cpu, uint32_t opcode, uint32_t value1, uint32_t value2);
uint32_pair_t cp11_perform_mrrc(arm_state_t * cpu, uint32_t opcode);

//void cp14_perform_cdp(arm_state_t * cpu, uint32_t opcode);
//void cp14_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
void cp14_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
uint32_t cp14_perform_mrc(arm_state_t * cpu, uint32_t opcode);

//void cp15_perform_cdp(arm_state_t * cpu, uint32_t opcode);
//void cp15_perform_ldc_stc(arm_state_t * cpu, uint32_t opcode, uint32_t address);
void cp15_perform_mcr(arm_state_t * cpu, uint32_t opcode, uint32_t value);
uint32_t cp15_perform_mrc(arm_state_t * cpu, uint32_t opcode);

/* Initialization */

void arm_emu_init(arm_state_t * cpu, arm_configuration_t config, uint16_t supported_isas, const memory_interface_t * memory_interface)
{
	memset(cpu, 0, sizeof(arm_state_t));
	cpu->memory = memory_interface;
	cpu->config = config;
	cpu->supported_isas = supported_isas;

	if((cpu->supported_isas & (1 << ISA_AARCH32)))
	{
		if(!(cpu->config.features & (1 << FEATURE_ARM26)) && !(cpu->config.features & (1 << FEATURE_ARM32)))
		{
			if(cpu->config.version < ARMV3)
				cpu->config.features |= 1 << FEATURE_ARM26;
			else
				cpu->config.features |= 1 << FEATURE_ARM32;
		}
	}

	if((cpu->supported_isas & (1 << ISA_AARCH64)))
	{
		cpu->config.features |= 1 << FEATURE_ARM64;
	}

	// make sure the Thumb implementation is coherent with the constraints
	if((cpu->config.features & (1 << FEATURE_THUMB2)) && cpu->config.thumb_implementation < ARM_THUMB_2)
		cpu->config.thumb_implementation = ARM_THUMB_2;
	else if(((cpu->config.features & (1 << FEATURE_THUMB)) || (cpu->supported_isas & (1 << ISA_THUMB32))) && cpu->config.thumb_implementation < ARM_THUMB_1)
		cpu->config.thumb_implementation = ARM_THUMB_1;

	// update constraints to reflect Thumb implementation
	if(cpu->config.thumb_implementation >= ARM_THUMB_1)
	{
		cpu->config.features |= 1 << FEATURE_THUMB;
		cpu->supported_isas |= 1 << ISA_THUMB32;
	}

	if(cpu->config.thumb_implementation >= ARM_THUMB_2)
		cpu->config.features |= 1 << FEATURE_THUMB2;

	// select a Jazelle implementation that is coherent with the constraints
	switch(cpu->config.jazelle_implementation)
	{
	case ARM_JAVA_DEFAULT:
		// default is Jazelle
		cpu->config.jazelle_implementation = ARM_JAVA_JAZELLE;
		break;
	case ARM_JAVA_NONE:
		// if Jazelle is supported, we need at least ARM_JAVA_JAZELLE
		// otherwise if the feature is supported, we need at least ARM_JAVA_TRIVIAL
		if((cpu->supported_isas & (1 << ISA_JAZELLE)))
			cpu->config.jazelle_implementation = ARM_JAVA_JAZELLE;
		else if((cpu->config.features & (1 << FEATURE_JAZELLE)))
			cpu->config.jazelle_implementation = ARM_JAVA_TRIVIAL;
		break;
	case ARM_JAVA_TRIVIAL:
		// if Jazelle is supported, we need at least ARM_JAVA_JAZELLE
		if((cpu->supported_isas & (1 << ISA_JAZELLE)))
			cpu->config.jazelle_implementation = ARM_JAVA_JAZELLE;
		break;
	case ARM_JAVA_JAZELLE:
		// Jazelle as implemented on ARM chips
		break;
	case ARM_JAVA_JVM:
	case ARM_JAVA_PICOJAVA:
		// these are not real implementations, use all extensions
		cpu->config.jazelle_implementation = ARM_JAVA_EXTENSION;
		break;
	case ARM_JAVA_EXTENSION:
		// Jazelle with some extensions to make it more usable for general purpose programming
		break;
	}

	// update constraints to reflect Jazelle implementation
	if(cpu->config.jazelle_implementation >= ARM_JAVA_TRIVIAL)
		cpu->config.features |= 1 << FEATURE_JAZELLE;
	if(cpu->config.jazelle_implementation >= ARM_JAVA_JAZELLE)
		cpu->supported_isas |= 1 << ISA_JAZELLE;

	cpu->el2_supported = config.features & (1 << FEATURE_VIRTUALIZATION);
	cpu->el3_supported = config.features & (1 << FEATURE_SECURITY);

	if((config.features & (1 << FEATURE_ARM64)))
		cpu->lowest_64bit_only_el = 3;
	else
		cpu->lowest_64bit_only_el = 4;

	if((cpu->config.features & (1 << FEATURE_FPA)) != 0)
	{
		cpu->coproc[1].perform_cdp = cp1_perform_cdp;
		cpu->coproc[1].perform_ldc_stc = cp1_perform_ldc_stc;
		cpu->coproc[1].perform_mcr = cp1_perform_mcr;
		cpu->coproc[1].perform_mrc = cp1_perform_mrc;

//		cpu->coproc[2].perform_cdp = cp2_perform_cdp;
		cpu->coproc[2].perform_ldc_stc = cp2_perform_ldc_stc;
//		cpu->coproc[2].perform_mcr = cp2_perform_mcr;
//		cpu->coproc[2].perform_mrc = cp2_perform_mrc;
	}
	if(arm_support_vfp_registers(cpu->config))
	{
		cpu->coproc[10].perform_cdp = cp10_perform_cdp;
		cpu->coproc[10].perform_ldc_stc = cp10_perform_ldc_stc;
		cpu->coproc[10].perform_mcr = cp10_perform_mcr;
//		cpu->coproc[10].perform_mcrr = cp10_perform_mcrr;
		cpu->coproc[10].perform_mrc = cp10_perform_mrc;
//		cpu->coproc[10].perform_mrrc = cp10_perform_mrrc;

		cpu->coproc[11].perform_cdp = cp11_perform_cdp;
		cpu->coproc[11].perform_ldc_stc = cp11_perform_ldc_stc;
		cpu->coproc[11].perform_mcr = cp11_perform_mcr;
		cpu->coproc[11].perform_mcrr = cp11_perform_mcrr;
		cpu->coproc[11].perform_mrc = cp11_perform_mrc;
		cpu->coproc[11].perform_mrrc = cp11_perform_mrrc;
	}

//	cpu->coproc[14].perform_cdp = cp14_perform_cdp;
//	cpu->coproc[14].perform_ldc_stc = cp14_perform_ldc_stc;
	cpu->coproc[14].perform_mcr = cp14_perform_mcr;
//	cpu->coproc[14].perform_mcrr = cp14_perform_mcrr;
	cpu->coproc[14].perform_mrc = cp14_perform_mrc;
//	cpu->coproc[14].perform_mrrc = cp14_perform_mrrc;

//	cpu->coproc[15].perform_cdp = cp15_perform_cdp;
//	cpu->coproc[15].perform_ldc_stc = cp15_perform_ldc_stc;
	cpu->coproc[15].perform_mcr = cp15_perform_mcr;
//	cpu->coproc[15].perform_mcrr = cp15_perform_mcrr;
	cpu->coproc[15].perform_mrc = cp15_perform_mrc;
//	cpu->coproc[15].perform_mrrc = cp15_perform_mrrc;

	cpu->sctlr_el1 = 0;
	if(!(cpu->config.features & FEATURE_ARM26))
	{
		cpu->sctlr_el1 |= SCTLR_P | SCTLR_D;
	}
}

#include "jazelle.c"

void a32_step(arm_state_t * cpu);
void a64_step(arm_state_t * cpu);
void t32_step(arm_state_t * cpu);
void j32_step(arm_state_t * cpu);

void step(arm_state_t * cpu)
{
	cpu->result = ARM_EMU_OK;
	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
		/* ARM26 */
		a32_step(cpu);
		break;
	case PSTATE_RW_32:
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			/* ARM32 */
			a32_step(cpu);
			break;
		case PSTATE_JT_THUMB:
		case PSTATE_JT_THUMBEE:
			/* Thumb or ThumbEE */
			t32_step(cpu);
			break;
		case PSTATE_JT_JAZELLE:
			/* Jazelle */
			j32_step(cpu);
			break;
		}
		break;
	case PSTATE_RW_64:
		/* ARM64 */
		a64_step(cpu);
		break;
	}
}

#include "step.gen.c"

