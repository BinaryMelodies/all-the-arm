#ifndef _JAZELLE_H
#define _JAZELLE_H

/* Conventions and definitions for Jazelle emulation */

#include "arm.h"

enum
{
	J32_LOC0 = 4, /* local 0 */
	J32_SHT = 5, /* software handler table */
	J32_TOS = 6, /* top of stack */
	J32_LOC = 7, /* locals */
	J32_CP = 8, /* constant pool */
};

enum
{
	J32_EXCEPTION_NULLPTR = 0x100,
	J32_EXCEPTION_OUT_OF_BOUNDS = 0x101,
	J32_EXCEPTION_JAZELLE_DISABLED = 0x102, // JE = 0
	J32_EXCEPTION_JAZELLE_INVALID = 0x103, // CV = 0
	J32_EXCEPTION_PREFETCH_ABORT = 0x104,
};

void j32_push_word(arm_state_t * cpu, uint32_t value);
uint32_t j32_pop_word(arm_state_t * cpu);
uint32_t j32_peek_word(arm_state_t * cpu, size_t index);
void j32_push_dword(arm_state_t * cpu, uint64_t value);
uint64_t j32_pop_dword(arm_state_t * cpu);
void j32_push_float(arm_state_t * cpu, float value);
uint32_t j32_pop_float(arm_state_t * cpu);
void j32_push_double(arm_state_t * cpu, double value);
double j32_pop_double(arm_state_t * cpu);

void j32_spill_fast_stack(arm_state_t * cpu);
void j32_update_locals(arm_state_t * cpu);

#endif // _JAZELLE_H
