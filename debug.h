#ifndef _DEBUG_H
#define _DEBUG_H

/* Functionality for displaying debugging information */

#include <stdio.h>
#include "arm.h"
#include "emu.h"

#define ANSI_BOLD "\33[1m"
#define ANSI_RESET "\33[m"

typedef struct arm_debug_state_t
{
	uint64_t r[33]; // 31 registers plus SP, PC
	uint32_t cpsr;

	arm_pstate_t pstate;

	// old floating point
	float80_t f[8];

	// new floating point
	uint32_t format_bits;
	union
	{
		float32_t s[32];
		float64_t d[32]; // VFPv2, VFPv3/4-D16: 16
		uint64_t w[32];
	};

	// top 4 elements of the Jazelle stack (possibly cached in registers)
	uint32_t j32_stack[4];
	// top of stack pointer (if all cached registers got flushed)
	uint32_t j32_stack_pointer;

	// if lowest > highest, then no memory has changed
	uint64_t memory_changed_lowest;
	uint64_t memory_changed_highest;
} arm_debug_state_t;

void arm_get_debug_state(arm_debug_state_t * debug_state, arm_state_t * cpu);

// old_state is optional, if non-NULL then the debugger highlights the changes
void debug(FILE * file, arm_state_t * cpu, arm_debug_state_t * old_state);

#endif // _DEBUG_H
