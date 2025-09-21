#ifndef EMU_H
#define EMU_H

/* Definitions for CPU emulation */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include "arm.h"

/* Conventions
 *
 * Functions, types and variables relating to the ARM architecture begin with the `arm_` prefix
 * The `a32_` and `a64_` prefixes distinguish between the AArch32 (32-bit) and AArch64 (64-bit) execution environments
 * When the 26-bit mode requires separate operations, the `a26_` prefix is used
 * Depending on the current instruction set, the `t32_` prefix marks Thumb/Thumb-2/ThumbEE objects and `j32_` marks Jazelle objects
 * ThumbEE specific objects are prefixed with `e32_`
 * When the distinction is necessary, `t16_` refers to objects relating to 16-bit wide Thumb instructions, and `t32_` to 32-bit wide Thumb instructions
 *
 * In summary:
 * - `arm_` - All ARM related definitions
 * - `a64_` - AArch64 specific
 * - `a32_` - AArch32 specific, also used for 26-bit mode and ARM instruction set
 * - `a26_` - Specifically 26-bit mode
 * - `t32_` - Related to Thumb/Thumb-2/ThumbEE, also specifically 32-bit wide Thumb instructions
 * - `e32_` - Related specifically to ThumbEE
 * - `t16_` - Related specifically to 16-bit wide Thumb instructions
 * - `j32_` - Jazelle instruction set related
 */

typedef enum arm_part_number_t
{
	// no MIDR
	ARM_PART_ARM1    = 0x0100,
	ARM_PART_ARM2    = 0x0200,
	ARM_PART_ARM250  = 0x0250,

	// pre-ARM8
	ARM_PART_ARM3    = 0x0300,

	ARM_PART_ARM600  = 0x0600,
	ARM_PART_ARM610  = 0x0610,
	ARM_PART_ARM620  = 0x0620,

	ARM_PART_ARM710  = 0x7100,
	ARM_PART_ARM720  = 0x7200,

	// ARM8 and later (https://github.com/bp0/armids/blob/master/arm.ids)
	ARM_PART_ARM810  = 0x8100,

	ARM_PART_ARM920  = 0x9200,
	ARM_PART_ARM922  = 0x9220,
	ARM_PART_ARM926  = 0x9260,
	ARM_PART_ARM940  = 0x9400,
	ARM_PART_ARM946  = 0x9460,
	ARM_PART_ARM966  = 0x9660,
	ARM_PART_ARM968  = 0x9680,
	ARM_PART_ARM996  = 0x9960,

	ARM_PART_ARM1020 = 0xA200,
	ARM_PART_ARM1022 = 0xA220,
	ARM_PART_ARM1026 = 0xA260,

	ARM_PART_ARM11MPCORE = 0xB020,
	ARM_PART_ARM1136 = 0xB360,
	ARM_PART_ARM1156 = 0xB560,
	ARM_PART_ARM1176 = 0xB760,

	ARM_PART_CORTEX_A5 = 0xC050,
	ARM_PART_CORTEX_A7 = 0xC070,
	ARM_PART_CORTEX_A8 = 0xC080,
	ARM_PART_CORTEX_A9 = 0xC090,
	ARM_PART_CORTEX_A12 = 0xC0D0,
	ARM_PART_CORTEX_A15 = 0xC0F0,
	ARM_PART_CORTEX_A17 = 0xC0E0,

	ARM_PART_CORTEX_R4 = 0xC140,
	ARM_PART_CORTEX_R5 = 0xC150,
	ARM_PART_CORTEX_R7 = 0xC170,
	ARM_PART_CORTEX_R8 = 0xC180,

	ARM_PART_CORTEX_M0 = 0xC200,
	ARM_PART_CORTEX_M1 = 0xC210,
	ARM_PART_CORTEX_M3 = 0xC230,
	ARM_PART_CORTEX_M4 = 0xC240,
	ARM_PART_CORTEX_M7 = 0xC270,
	ARM_PART_CORTEX_M0PLUS = 0xC600,

	ARM_PART_CORTEX_A32 = 0xD010,
	ARM_PART_CORTEX_A53 = 0xD030,

	ARM_PART_CORTEX_R52 = 0xD130,

	ARM_PART_CORTEX_M33 = 0xD210,
} arm_part_number_t;

typedef enum arm_vendor_t
{
	ARM_VENDOR_ARM = 'A',
	ARM_VENDOR_BROADCOMM = 'B',
	ARM_VENDOR_CAVIUM = 'C',
	ARM_VENDOR_DEC = 'D',
	ARM_VENDOR_INFINEON = 'I',
	ARM_VENDOR_MOTOROLA = 'M',
	ARM_VENDOR_NVIDIA = 'N',
	ARM_VENDOR_AMCC = 'P',
	ARM_VENDOR_QUALCOMM = 'Q',
	ARM_VENDOR_MARVELL = 'V',
	ARM_VENDOR_INTEL = 'i',
} arm_vendor_t;

enum
{
	// manufacturer for pre-ARM6 only
	ARM_MANUFACTURER_VLSI = 'V',

	ARM_ARCH_V4 = 0x1,
	ARM_ARCH_V4T = 0x2,
	ARM_ARCH_V5 = 0x3,
	ARM_ARCH_V5T = 0x4,
	ARM_ARCH_V5TE = 0x5,
	ARM_ARCH_V5TEJ = 0x6,
	ARM_ARCH_V6 = 0x7,
	ARM_ARCH_CPUID = 0xF,
};

// some useful constants
enum
{
	CPSR_M_MASK = 0x0000001F, /* (v3+), for (v1+) lower two bits (privileged) */
	CPSR_MODE_MASK = 0x0000000F,
	CPSR_A26_MODE_MASK = 0x00000003,
	CPSR_M4 = 0x00000010,
	CPSR_T = 0x00000020, /* [AArch32 only] (v4T+) Thumb or ThumbEE */
	CPSR_T_SHIFT = 5,
	CPSR_F = 0x00000040, /* (v1+) FIQ disable (privileged) */
	CPSR_I = 0x00000080, /* (v1+) IRQ disable (privileged) */
	CPSR_A = 0x00000100, /* (v6+) imprecise data abort disable (privileged) */
	CPSR_E = 0x00000200, /* [AArch32 only] (v6+) big endian (USR) */
	CPSR_D = 0x00000200, /* [AArch64 only] (v8+) breakpoint exception disable */
	CPSR_IT_MASK = 0x0600FC00, /* [AArch32 only] (v6T2+) if-then state bits */
	CPSR_IT0_MASK = 0x06000000,
	CPSR_IT0_SHIFT = 25,
	CPSR_IT1_MASK = 0x0000FC00,
	CPSR_IT1_SHIFT = 10 - 2,
	CPSR_GE_MASK = 0x000F0000, /* [AArch32 only] (v6+) greater-than-or-equal-to bits (USR) */
	CPSR_GE_SHIFT = 16,
	CPSR_GE0 = 0x00010000,
	CPSR_GE1 = 0x00020000,
	CPSR_GE2 = 0x00040000,
	CPSR_GE3 = 0x00080000,
	CPSR_IL = 0x00100000, /* (v8+) illegal execution */
	CPSR_SS = 0x00200000, /* (v8+) software step */
	CPSR_PAN = 0x00400000, /* (v8.1+) */
	CPSR_UAO = 0x00800000, /* [AArch64 only] */
	CPSR_J = 0x01000000, /* [AArch32 only] (v5TEJ+) Java state bit */
	CPSR_J_SHIFT = 24 - 1, /* to put it into the pstate.jt field */
	CPSR_Q = 0x08000000, /* [AArch32 only] (v5TE+/v5TExP+) sticky overflow bit (USR) */
	CPSR_V = 0x10000000, /* (v1+) overflow (USR) */
	CPSR_C = 0x20000000, /* (v1+) carry (USR) */
	CPSR_Z = 0x40000000, /* (v1+) zero (USR) */
	CPSR_N = 0x80000000, /* (v1+) negative/less than (USR) */

	CPSR_SP = 0x00000001, /* [AArch64 only] */
	CPSR_EL_MASK = 0x0000000C, /* [AArch64 only] */
	CPSR_EL_SHIFT = 2,

	CPSR_A26_F = 0x04000000, /* (v1+) FIQ disable (privileged) */
	CPSR_A26_I = 0x08000000, /* (v1+) IRQ disable (privileged) */

	/* modes, stored in the bottom 4 bits */
	MODE_USR = 0, /* (v1+) user */
	MODE_FIQ = 1, /* (v1+) fast interrupt */
	MODE_IRQ = 2, /* (v1+) interrupt */
	MODE_SVC = 3, /* (v1+) supervisor */
	MODE_MON = 6, /* (v6z, v7+, security extensions) monitor */
	MODE_ABT = 7, /* (v3+) instruction abort */
	MODE_HYP = 10, /* (v7ve+, virtualization extensions) hypervisor */
	MODE_UND = 11, /* (v3+) undefined instruction */
	MODE_SYS = 15, /* (v4+) system */

	MODE_ARM26 = 0,
	MODE_ARM32 = 16,
	MODE_ARM64 = 0,

	MODE_EL0 = 0 << CPSR_EL_SHIFT,
	MODE_EL1 = 1 << CPSR_EL_SHIFT,
	MODE_EL2 = 2 << CPSR_EL_SHIFT,
	MODE_EL3 = 3 << CPSR_EL_SHIFT,

	/* VFP flags */
	FPSCR_LEN_MASK = 0x00070000,
	FPSCR_LEN_SHIFT = 16,
	FPSCR_STRIDE_MASK = 0x00300000,
	FPSCR_STRIDE_SHIFT = 20,

	FPSCR_V = 0x10000000,
	FPSCR_C = 0x20000000,
	FPSCR_Z = 0x40000000,
	FPSCR_N = 0x80000000,

	/* Jazelle flags */

	// p14, 7, c2
	JOSCR_FLAT_ARRAY = 0x20000000,
	JOSCR_DISABLE_ARRAY_INSTRUCTIONS = 0x80000000,

	// p14, 7, c3 (Array Object Layout Register)
	JAOLR_ELEMENT_OFF_MASK = 0x00000F00,
	JAOLR_ELEMENT_OFF_SHIFT = 8 - 2,
	JAOLR_LENGTH_OFF_MASK = 0x0000F000,
	JAOLR_LENGTH_OFF_SHIFT = 12 - 2,
	JAOLR_LENGTH_SUB = 0x00010000,
	JAOLR_LENSHIFT_MASK = 0x000E0000,
	JAOLR_LENSHIFT_SHIFT = 17,

	// p15, 0, c1: SCTLR and SCTLR_EL1
	SCTLR_M  = 1 << 0, // v3 (610)
	SCTLR_A  = 1 << 1, // v3 (610)
	SCTLR_C  = 1 << 2, // v3 (610)
	SCTLR_W  = 1 << 3, // v3 (610)
	SCTLR_P  = 1 << 4, // v3 (610)
	SCTLR_D  = 1 << 5, // v3 (610)
	SCTLR_L  = 1 << 6, // v3 (610, fixed 1 on 710)
	SCTLR_B  = 1 << 7, // v3 (610)
	SCTLR_S  = 1 << 8, // v3 (610)
	SCTLR_R  = 1 << 9, // v3 (710)
	SCTLR_F  = 1 << 10, // v5
	SCTLR_Z  = 1 << 11, // v5
	SCTLR_I  = 1 << 12, // v5
	SCTLR_V  = 1 << 13, // v5
	SCTLR_RR  = 1 << 14, // v5
	SCTLR_L4  = 1 << 15, // v5
	SCTLR_U  = 1 << 22, // v6
	SCTLR_SPAN = 1 << 23, // v8.1
	SCTLR_E0E = 1 << 24, // v8
	SCTLR_EE = 1 << 25, // v6
	SCTLR_TE = 1 << 30, // v7

	// SCR and SCR_EL3
	SCR_EL3_NS = 1 << 0,
	SCR_EL3_RW = 1 << 10,

	// HCR/HCR2 and HCR_EL2
	HCR_EL2_RW = (uint64_t)1 << 31,
	HCR_EL2_E2H = (uint64_t)1 << 34,
	HCR_EL2_TGE = (uint64_t)1 << 27,
};

typedef enum arm_emu_result_t
{
	ARM_EMU_OK,

	ARM_EMU_RESET,
	ARM_EMU_SVC,
	ARM_EMU_UNDEFINED,
	ARM_EMU_PREFETCH_ABORT,
	ARM_EMU_DATA_ABORT,
	ARM_EMU_ADDRESS26,
	ARM_EMU_IRQ,
	ARM_EMU_FIQ,
	ARM_EMU_BREAKPOINT,
	ARM_EMU_UNALIGNED,
	ARM_EMU_UNALIGNED_PC,
	ARM_EMU_UNALIGNED_SP,
	ARM_EMU_SERROR,
	ARM_EMU_SMC,
	ARM_EMU_HVC,
	ARM_EMU_SOFTWARE_STEP,

	ARM_EMU_JAZELLE_UNDEFINED,
	ARM_EMU_JAZELLE_NULLPTR,
	ARM_EMU_JAZELLE_OUT_OF_BOUNDS,
	ARM_EMU_JAZELLE_DISABLED,
	ARM_EMU_JAZELLE_INVALID,
	ARM_EMU_JAZELLE_PREFETCH_ABORT,

	ARM_EMU_THUMBEE_OUT_OF_BOUNDS,
	ARM_EMU_THUMBEE_NULLPTR,
} arm_emu_result_t;

/* represents an ARM coprocessor interface */
typedef struct arm_coprocessor_t
{
	void (* perform_cdp)(arm_state_t * cpu, uint32_t opcode);

	void (* perform_ldc_stc)(arm_state_t * cpu, uint32_t opcode, uint32_t address);

	void (* perform_mcr)(arm_state_t * cpu, uint32_t opcode, uint32_t value);
	void (* perform_mcrr)(arm_state_t * cpu, uint32_t opcode, uint32_t value1, uint32_t value2);
	uint32_t (* perform_mrc)(arm_state_t * cpu, uint32_t opcode);
	uint32_pair_t (* perform_mrrc)(arm_state_t * cpu, uint32_t opcode);
} arm_coprocessor_t;

typedef struct memory_interface_t
{
	bool (* read)(arm_state_t *, uint64_t, void *, size_t, bool);
	bool (* write)(arm_state_t *, uint64_t, const void *, size_t, bool);
} memory_interface_t;

/* PSTATE RW field, bit 0 is stored in xPSR bit 4
 * Encodes the possible execution state by specifying the register width (or in 26-bit mode, the address width)
 * In ARMv8, only 32-bit and 64-bit are available, but here we extend the structure for 26-bit mode as well
 */
enum
{
	PSTATE_RW_26 = 0, // This is an extension to support old ARM CPUs (v1-v3)
	PSTATE_RW_32 = 1,
	PSTATE_RW_64 = 2,
};

/* PSTATE JT field, stored in the J and T fields of the xPSR in 32-bit mode
 * These two bits specify the current instruction set and execution environment in 32-bit mode
 */
enum
{
	PSTATE_JT_ARM = 0, // original ARM instruction set
	PSTATE_JT_THUMB = 1, // compressed Thumb instruction set
	PSTATE_JT_JAZELLE = 2, // native Java bytecode execution
	PSTATE_JT_THUMBEE = 3, // variant of Thumb mode intended for JIT execution for memory managed programming languages
};

typedef struct arm_pstate_t
{
	uint32_t rw : 2; // register/address width (v3+), 0: ARM26, 1: ARM32, 2: ARM64
	uint32_t mode : 4; // [AArch32] processor mode in AArch32 (privileged) (v1: 2-bit, v3+: 4-bit)
	uint32_t f : 1; // FIQ disable (privileged) (v1+)
	uint32_t i : 1; // IRQ disable (privileged) (v1+)
	uint32_t jt : 2; // [AArch32] instruction set (v4T+, v5TEJ+), 0: ARM, 1: Thumb, 2: Jazelle, 3: ThumbEE
	uint32_t q : 1; // [AArch32] sticky overflow/cumulative saturation (v5TE+/v5TExP+)
	uint32_t a : 1; // imprecise data abort disable (privileged) (v6+)
	uint32_t ge : 4; // [AArch32] greater-than-or-equal-to-bits (v6+)
	uint32_t e : 1; // [AArch32] big endian (v6+)
	uint32_t it : 8; // [AArch32] if-then state bits (Thumb/ThumbEE mode only) (v6T2+)
	uint32_t sp : 1; // [AArch64] use SP of error level instead of SP_EL0 (v8+)
	uint32_t el : 2; // [AArch64] error level (v8+)
	uint32_t d : 1; // [AArch64] breakpoint exception disable (v8+)
	uint32_t il : 1; // [AArch64] illegal execution (v8+)
	uint32_t ss : 1; // [AArch64] software step (v8+)
	uint32_t pan : 1; // privileged access never (v8.1+)
	uint32_t uao : 1; // user access override (v8.2+)
	uint32_t v : 1; // overflow (v1+)
	uint32_t c : 1; // carry (v1+)
	uint32_t z : 1; // zero (v1+)
	uint32_t n : 1; // negative/less than (v1+)
} arm_pstate_t;

struct arm_state_t
{
	arm_configuration_t config;
	uint16_t supported_isas;
	arm_part_number_t part_number;
	arm_vendor_t vendor;

	/* 4: fully 32-bit */
	uint8_t lowest_64bit_only_el;
	bool el2_supported;
	bool el3_supported;

	// breaks are returned to the monitor instead of handled by the emulator
	bool capture_breaks;
	arm_emu_result_t result;

	// coprocessor interfaces
	arm_coprocessor_t coproc[16];

	// modified at runtime
	uint64_t r[REG_COUNT];
	uint64_t old_pc;

	arm_pstate_t pstate;

	// coprocessor/system registers
	union
	{
		// old floating point
		struct
		{
			float80_t f[8];
			uint32_t fpsr;
			uint32_t fpcr;
		} fpa;
		// new floating point
		struct
		{
			bool stmx_standard_format2;
			uint32_t format_bits;
			union
			{
				float32_t s[32];
				float64_t d[32]; // VFPv2, VFPv3/4-D16: 16
				uint64_t w[32];
			};
			uint32_t fpsid;
			uint32_t fpscr;
			uint32_t fpexc;
		} vfp;
	};

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define VFP_S(i) vfp.s[(i)]
# define VFP_D(i) vfp.d[(i)]
# define VFP_W(i) vfp.w[(i)]
#elif __BYTE_ORDER == __BIG_ENDIAN
# define VFP_S(i) vfp.s[(i) ^ 1]
# define VFP_D(i) vfp.d[(i)]
# define VFP_W(i) vfp.w[(i)]
#else
# error Unknown byte order
#endif

	uint32_t exclusive_procid; // TODO
	uint64_t exclusive_start;
	uint64_t exclusive_end;

	// p32|64, #, c#, c#, #

	// p14, 6, c1, c0, 0
	uint32_t teehbr;

	// Jazelle
	// p14, 7, c0, c0, 0
	uint32_t jidr;
	// p14, 7, c1, c0, 0
	uint32_t joscr;
	// p14, 7, c2, c0, 0
	uint32_t jmcr;
	// p14, 7, c3, c0, 0
	uint32_t jaolr; // Jazelle array object layout register

	// p15|3, 0, c1, c0, 0
	// AArch64 name, AArch32 name is sctlr
	uint32_t sctlr_el1;

	// p15|3, 4, c1, c0, 0
	// AArch64 name, AArch32 name is hsctlr
	uint32_t sctlr_el2;

	// | p3, 6, c1, c0, 0
	uint32_t sctlr_el3;

	// p15, 0, c1, c1, 0 | p3, 6, c1, c1, 0
	// AArch64 name, AArch32 name is scr
	// note: these are not mandated to be the same
	uint32_t scr_el3;

	// p15|3, 4, c1, c1, 0 (4 for hcr2 in AArch32)
	// AArch64 name, AArch32 names are hcr2:hcr
	uint64_t hcr_el2;

	// p15|3, 0(?5), c12, c0, 0
	// AArch64 name, AArch32 name is vbar
	uint64_t vbar_el1;
	// p15|3, 4(?0), c12, c0, 0
	// AArch64 name, AArch32 name is hvbar
	uint64_t vbar_el2;
	// | p3, 6, c12, c0, 0
	// AArch64 name
	uint64_t vbar_el3;

	const memory_interface_t * memory;

	jmp_buf exc;
};

void arm_emu_init(arm_state_t * cpu, arm_configuration_t config, uint16_t supported_isas, const memory_interface_t * memory_interface);
void step(arm_state_t * cpu);

void arm_set_isa(arm_state_t * cpu, arm_instruction_set_t isa);
arm_instruction_set_t arm_get_current_instruction_set(arm_state_t * cpu);

bool is_supported_isa(arm_state_t * cpu, arm_instruction_set_t isa);
bool a32_is_arm26(arm_state_t * cpu);

uint32_t a32_get_cpsr(arm_state_t * cpu);
void a32_set_cpsr(arm_state_t * cpu, uint32_t mask, uint32_t cpsr);

uint32_t a32_register_get32(arm_state_t * cpu, int regnum);
uint32_t a32_register_get32_lhs(arm_state_t * cpu, int regnum);\
void a32_register_set32(arm_state_t * cpu, int regnum, uint32_t value);
uint32_t a64_register_get32(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp);
void a64_register_set32(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp, uint32_t value);
uint64_t a64_register_get64(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp);
void a64_register_set64(arm_state_t * cpu, int regnum, bool_suppress_sp_t suppress_sp, uint64_t value);

arm_endianness_t a32_get_instruction_endianness(arm_state_t * cpu);
arm_endianness_t a32_get_data_endianness(arm_state_t * cpu);

uint8_t arm_memory_read8_data(arm_state_t * cpu, uint64_t address);
uint16_t arm_memory_read16_data(arm_state_t * cpu, uint64_t address);
uint32_t arm_memory_read32_data(arm_state_t * cpu, uint64_t address);
uint64_t arm_memory_read64_data(arm_state_t * cpu, uint64_t address);

void arm_memory_write8_data(arm_state_t * cpu, uint64_t address, uint8_t value);
void arm_memory_write16_data(arm_state_t * cpu, uint64_t address, uint16_t value);
void arm_memory_write32_data(arm_state_t * cpu, uint64_t address, uint32_t value);
void arm_memory_write64_data(arm_state_t * cpu, uint64_t address, uint64_t value);

uint8_t arm_fetch8(arm_state_t * cpu, uint64_t address);
uint16_t arm_fetch16(arm_state_t * cpu, uint64_t address);
uint32_t arm_fetch32(arm_state_t * cpu, uint64_t address);
uint16_t arm_fetch16be(arm_state_t * cpu, uint64_t address);

//uint8_t  arm_memory_read8(const memory_interface_t * memory, uint64_t address, arm_endianness_t endian);
//uint16_t arm_memory_read16(const memory_interface_t * memory, uint64_t address, arm_endianness_t endian);
//uint32_t arm_memory_read32(const memory_interface_t * memory, uint64_t address, arm_endianness_t endian);
//uint64_t arm_memory_read64(const memory_interface_t * memory, uint64_t address, arm_endianness_t endian);

void arm_memory_write8(const memory_interface_t * memory, uint64_t address, uint8_t value, arm_endianness_t endian);
void arm_memory_write16(const memory_interface_t * memory, uint64_t address, uint16_t value, arm_endianness_t endian);
void arm_memory_write32(const memory_interface_t * memory, uint64_t address, uint32_t value, arm_endianness_t endian);
void arm_memory_write64(const memory_interface_t * memory, uint64_t address, uint64_t value, arm_endianness_t endian);

#endif // EMU_H
