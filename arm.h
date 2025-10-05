#ifndef ARM_H
#define ARM_H

/* Global include for the ARM emulator and disassembler */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef float float32_t;
typedef double float64_t;
typedef long double float80_t;

typedef struct
{
	uint32_t l, h;
} uint32_pair_t;

#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#define MAX(a, b) ((a) >= (b) ? (a) : (b))

/* Architecture version, 26-bit: ARMV1/ARMV2, 32-bit: ARMV3 to ARMV7, 64-bit: ARMV8 or above (unless only 32-bit mode is supported) */
typedef enum arm_version_t
{
	ARMV1 = 1, // ARMv1 (1985)
	ARMV2, // ARMv2 (1986), product names: ARM2aS, ARM250, ARM3
	ARMV3, // ARMv3 (1993), product names: ARM6, ARM7
	ARMV4, // ARMv4 (1994), product names: ARM8, ARM7T, SecurCore
	ARMV5, // ARMv5 (1999), product names: ARM9E, ARM10E, ARM7EJ, ARM9E, ARM10E
	ARMV6, // ARMv6 (2002), product names: ARM11, SecurCore, Cortex-M
	ARMV7, // ARMv7 (2005), product names: SecurCore, Cortex-M, Cortex-R, Cortex-A
	ARMV8, // ARMv8 (2011), product names: Cortex-M, Cortex-R, Cortex-A, Cortex-X, Neoverse
	ARMV81, // ARMv8.1
	ARMV82, // ARMv8.2
	ARMV83, // ARMv8.3
	ARMV9, // ARMv9 (2021), product names: Cortex-A, Cortex-X, Neoverse
} arm_version_t;

#define ARM_UNKNOWN ((arm_version_t)0)

/* Floating point coprocessors */
typedef enum arm_fp_version_t
{
	ARM_VFPV1 = 1, // Vector Floating Point, for ARM10 (v5TE)
	ARM_VFPV2, // for v5TE and later
	ARM_VFPV3, // also SIMDv1, for v7 and later
	ARM_VFPV4, // also SIMDv2, for v7 and later
	ARM_VFPV5, // for 7VE-M and v8-R
	ARM_V8FP,  // ARMv8 floating point
} arm_fp_version_t;

/*
 * Instruction set emulation
 *
 * (Note that the choice of naming them AArch26/AArch32/AArch64 is a misnomer because those refer to execution states, not instruction sets)
 */
typedef enum arm_instruction_set_t
{
	ISA_AARCH26 = 1, // original instruction set, introduced with ARMv1, removed in ARMv4
	ISA_AARCH32, // 32-bit extension, identical instruction format, introduced with ARMv3
	ISA_THUMB32, // compressed Thumb and Thumb-2 instruction sets, introduced in ARMv4 and ARMv6, respectively
	ISA_JAZELLE, // Java bytecode, introduced in ARMv5
	ISA_THUMBEE, // ThumbEE, variant of Thumb, introduced in ARMv7
	ISA_AARCH64, // 64-bit extension, introduced with ARMv8
} arm_instruction_set_t;

#define ISA_UNKNOWN ((arm_instruction_set_t)0)
#define ISA_START ((arm_instruction_set_t)1)
#define ISA_END ((arm_instruction_set_t)(ISA_AARCH64 + 1))

typedef enum arm_syntax_t
{
	SYNTAX_UNKNOWN,
	SYNTAX_DIVIDED,
	SYNTAX_UNIFIED,
} arm_syntax_t;

/* Java bytecode support */
typedef enum arm_java_implementation_t
{
	ARM_JAVA_DEFAULT = -1,
	ARM_JAVA_NONE = 0, // for parsing: no instructions are recognized
	ARM_JAVA_TRIVIAL = 1, // for parsing: for emulation: all instructions are trapped
	ARM_JAVA_JAZELLE = 2, // for emulation: instructions implemented by v5TEJ
	ARM_JAVA_JVM, // for parsing: all JVM instructions
	ARM_JAVA_PICOJAVA, // for parsing: all JVM instructions and picoJava instructions
	// this is a custom extension of Jazelle never implemented in real hardware
	ARM_JAVA_EXTENSION, // for parsing: all JVM/picoJava instructions and some custom extensions, for emulation
} arm_java_implementation_t;

/* Thumb support */
typedef enum arm_thumb_implementation_t : uint8_t
{
	ARM_NO_THUMB = 0,
	ARM_THUMB_1 = 1,
	ARM_THUMB_2 = 3,
} arm_thumb_implementation_t;

typedef enum arm_endianness_t
{
	ARM_ENDIAN_LITTLE = 0, // LE, words start with the least significant byte
	ARM_ENDIAN_BIG = 1, // BE8, words start with the most significant byte
	ARM_ENDIAN_SWAPPED = 2, // BE32, byte/word access in memory is flipped
} arm_endianness_t;

enum
{
	ARM_PROFILE_C = 0, // "Classic", pre-profile setting, treated the same as A-profile
	ARM_PROFILE_A = 1,
	ARM_PROFILE_R = 2,
	ARM_PROFILE_M = 3,
	FEATURE_PROFILE_MASK = 3,
};

typedef enum arm_feature_t
{
	_FEATURE_PROFILE_TOP = 1,
	FEATURE_SWP, // 2a, 3+
	FEATURE_ARM26, // 1, 2, 3 but not 3G
	FEATURE_ARM32, // 3+
	FEATURE_MULL, // 3M, 4+ but not 4xM, 4TxM, 5xM, 5TxM
	FEATURE_THUMB, // 4T(xM), 5T(xM), 5TExP, 5TE, 5TEJ, 6+ (1994)
	FEATURE_ENH_DSP, // 5TE(xP), 5TEJ, 6+ (1999)
	FEATURE_DSP_PAIR, // 5TE but not 5TExP, 6+
	FEATURE_JAZELLE, // 5TEJ, 6+ (2001) BXJ instruction
	FEATURE_MULTIPROC, // 6K, 7+
	FEATURE_THUMB2, // 6T2, 7+ (2003)
	FEATURE_SECURITY, // 6Z, 6KZ, 7 + Security Extensions
	FEATURE_VIRTUALIZATION, // 7VE, 8+ (optional)
	FEATURE_ARM64, // 8-A
	FEATURE_M_BASE, // 8-M Baseline (TODO)
	FEATURE_M_MAIN, // 8-M Mainline (TODO)
	FEATURE_CRYPTOGRAPHY,

	FEATURE_FPA, // (1989) Floating Point Accelerator, WE32206 compatible floating point, product names: AKA20 (for ARM2 (v2)), FPA10 (for ARM3/ARM600 (v3)), FPA11 (for ARM700 (v3))
	FEATURE_VFP, // Virtual Floating Point, 32-bit floating point support
	FEATURE_DREG, // VFP flag, D registers (32-bit)
	FEATURE_32_DREG, // VFP flag, 32 D registers (instead of 16)
	FEATURE_FP16, // 16-bit floating point support
	FEATURE_SIMD, // Advanced SIMD (Neon) support
	FEATURE_MVE, // M-Profile Vector Extension (Helium) support (TODO)
} arm_feature_t;

/* Common set of of information for emulation and disassembly */
typedef struct arm_configuration_t
{
	arm_version_t version;
	arm_fp_version_t fp_version;
	uint32_t features;
	arm_thumb_implementation_t thumb_implementation;
	arm_java_implementation_t jazelle_implementation;
} arm_configuration_t;

static inline bool arm_support_vfp_registers(arm_configuration_t config)
{
	return (config.features & (1 << FEATURE_VFP)) != 0 || (config.features & (1 << FEATURE_SIMD)) != 0 || (config.features & (1 << FEATURE_MVE)) != 0;
}

typedef struct arm_state_t arm_state_t;
typedef struct arm_parser_state_t arm_parser_state_t;
typedef struct memory_interface_t memory_interface_t;

/* Accessible registers */
typedef enum regnum_t
{
	NONE = -1,

	/* registers 0-14 map to user mode AArch32 registers and 0-30 map to AArch64 registers */

	/* AArch32-AArch64 correspondence */
	R13_HYP = 15,
	R14_IRQ,
	R13_IRQ,
	R14_SVC,
	R13_SVC,
	R14_ABT,
	R13_ABT,
	R14_UND,
	R13_UND,
	R8_FIQ,
	R9_FIQ,
	R10_FIQ,
	R11_FIQ,
	R12_FIQ,
	R13_FIQ,
	R14_FIQ,

	A64_LR = 30,

	/* these should come sequentially for easier access via code */
	SP_EL0, // conveniently 31
	SP_EL1,
	SP_EL2,
	SP_EL3,

	/* additional registers */
	PC,
	R14_MON,
	R13_MON,
	ELR_EL1,
	ELR_EL2,
	ELR_EL3,

	/* SPSR registers */
	SPSR_EL1,
	SPSR_EL2,
	SPSR_EL3,
	SPSR_ABT,
	SPSR_UND,
	SPSR_IRQ,
	SPSR_FIQ,

	REG_COUNT,

	/* synonyms, between AArch32 and AArch64 registers */

	ELR_HYP = ELR_EL2,
	SPSR_SVC = SPSR_EL1,
	SPSR_HYP = SPSR_EL2,
	SPSR_MON = SPSR_EL3,

	/* numerical shorthands */

	A32_SP = 13,
	A32_LR = 14,
	A32_PC_NUM = 15, // accessed from r15 in assembly, but actual position in arm_state_t is r[PC], not r[15]
	A64_SP = 31, // for some instruction it can be accessed as x31
} regnum_t;

// some useful constants
enum
{
	COND_ALWAYS = 14,
};

// convenience functions

static inline int32_t sign_extend(size_t bits, int32_t value)
{
	return (value << (32 - bits)) >> (32 - bits);
}

static inline int64_t sign_extend64(size_t bits, int64_t value)
{
	return (value << (64 - bits)) >> (64 - bits);
}

static inline uint32_t rotate_right32(uint32_t value, size_t bits)
{
	bits &= 0x1F;
	if(bits == 0)
		return value;
	else
		return (value >> bits) | (value << (32 - bits));
}

static inline uint64_t rotate_right64(uint64_t value, size_t bits)
{
	bits &= 0x3F;
	if(bits == 0)
		return value;
	else
		return (value >> bits) | (value << (64 - bits));
}

static inline uint16_t count_bits16(uint16_t value)
{
	size_t count = 0;
	while(value != 0)
	{
		if((value & 1))
			count ++;
		value >>= 1;
	}
	return count;
}

// common execution/disassembly functions

typedef enum bool_suppress_sp_t : bool
{
	PERMIT_SP = false,
	SUPPRESS_SP = true,
} bool_suppress_sp_t;

typedef enum bool_copy_cpsr_bits_t : bool
{
	IGNORE_RESULT = false,
	COPY_CPSR_BITS = true,
} bool_copy_cpsr_bits_t;

typedef enum bool_indexing_type_t : bool
{
	POSTINDEXED = false,
	PREINDEXED = true,
	NOINDEX = true,
} bool_indexing_type_t;

typedef enum bool_writeback_t : bool
{
	NOWRITEBACK = false,
	WRITEBACK = true,
} bool_writeback_t;

typedef enum bool_usermode_t : bool
{
	USE_DEFAULT_MODE = false,
	USE_USER_MODE = true,
} bool_usermode_t;

/* Parsing instruction immediates */

extern uint32_t a32_get_immediate_operand(uint32_t opcode);
extern uint32_t t32_get_immediate_operand(uint16_t opcode1, uint16_t opcode2);
extern uint32_t a64_get_bitmask32(uint32_t opcode);
extern uint64_t a64_get_bitmask64(uint32_t opcode);
extern uint64_t a32_get_simd_operand(arm_state_t * cpu, uint32_t opcode);
extern uint64_t t32_get_simd_operand(arm_state_t * cpu, uint16_t opcode1, uint16_t opcode2);
extern uint64_t a64_get_simd_operand(arm_state_t * cpu, uint32_t opcode);

extern uint8_t arm_get_simd_vector_index_size(uint8_t imm);
extern uint8_t arm_get_simd_vector_index(uint8_t imm);
extern uint8_t arm_get_simd_shift_element_size(uint8_t imm);
extern uint8_t arm_get_simd_shift_amount(uint8_t imm);
extern uint8_t arm_get_simd_shift_amount_neg(uint8_t imm);

extern   int8_t arm_fetch_next8(arm_state_t * cpu);
extern uint16_t arm_fetch_next16(arm_state_t * cpu);
extern  int16_t arm_fetch_next16be(arm_state_t * cpu);
extern uint32_t arm_fetch_next32(arm_state_t * cpu);
extern  int32_t arm_fetch_next32be(arm_state_t * cpu);

/* FPA specific constants */
extern const float80_t fpa_operands[];

#endif // ARM_H
