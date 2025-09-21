
/* Displays current CPU state, checks state change between execution steps */

#include <string.h>
#include "debug.h"
#include "jazelle.h"

typedef struct arm_debug_change_t
{
	bool r[33]; // 31 registers plus SP, PC
	bool cpsr;

	struct
	{
		bool rw;
		bool mode;
		bool f;
		bool i;
		bool j, t;
		bool q;
		bool a;
		bool ge;
		bool e;
		bool it;
		bool sp;
		bool el;
		bool d;
		bool il;
		bool ss;
		bool pan;
		bool uao;
		bool v;
		bool c;
		bool z;
		bool n;
	} pstate;

	// old floating point
	bool f[8];

	// new floating point
	bool d[32];

	// top 4 elements of the Jazelle stack (possibly cached in registers)
	bool j32_stack[4];

	uint64_t memory_changed_lowest;
	uint64_t memory_changed_highest;
} arm_debug_change_t;

#if J32_EMULATE_INTERNALS
extern uint32_t j32_get_fast_stack_size(arm_state_t * cpu);
extern uint32_t j32_get_fast_stack_element(arm_state_t * cpu, uint32_t offset);
extern uint32_t j32_get_fast_stack_top(arm_state_t * cpu);
#endif

static inline uint32_t j32_get_stack_value(arm_state_t * cpu, uint8_t index)
{
#if J32_EMULATE_INTERNALS
	uint32_t size = j32_get_fast_stack_size(cpu);
	if(index < size)
	{
		return a32_register_get32(cpu, j32_get_fast_stack_element(cpu, index));
	}
	else
	{
		index -= size;
	}
#endif
	return arm_memory_read32_data(cpu, a32_register_get32(cpu, J32_TOS) - 4 * (1 + index));
}

void arm_get_debug_state(arm_debug_state_t * debug_state, arm_state_t * cpu)
{
	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
	case PSTATE_RW_32:
		/* ARM26/ARM32 */
		for(int i = 0; i < 15; i++)
			debug_state->r[i] = a32_register_get32(cpu, i);
		debug_state->r[32] = a32_register_get32_lhs(cpu, A32_PC_NUM);
		debug_state->cpsr = a32_get_cpsr(cpu);
		break;
	case PSTATE_RW_64:
		/* ARM64 */
		for(int i = 0; i < 32; i++)
			debug_state->r[i] = a64_register_get64(cpu, i, PERMIT_SP);
		debug_state->r[32] = cpu->r[PC];
		break;
	}

	debug_state->pstate = cpu->pstate;

	if((cpu->config.features & (1 << FEATURE_FPA)) != 0)
	{
		for(int i = 0; i < 16; i++)
			debug_state->f[i] = cpu->fpa.f[i];
	}
	if(arm_support_vfp_registers(cpu->config))
	{
		debug_state->format_bits = cpu->vfp.format_bits;
		for(int i = 0; i < 32; i++)
			debug_state->w[i] = cpu->vfp.w[i];
	}

	if(cpu->pstate.jt == PSTATE_JT_JAZELLE)
	{
		for(int i = 0; i < 4; i++)
			debug_state->j32_stack[i] = j32_get_stack_value(cpu, i);
		debug_state->j32_stack_pointer = a32_register_get32(cpu, J32_TOS);
#if J32_EMULATE_INTERNALS
		debug_state->j32_stack_pointer += 4 * j32_get_fast_stack_size(cpu);
#endif
	}
}

static inline void arm_compare_debug_state(arm_debug_change_t * change, arm_debug_state_t * old_state, arm_debug_state_t * new_state, arm_configuration_t config)
{
	memset(change, 0, sizeof(arm_debug_change_t));

	for(int i = 0; i < 15; i++)
		change->r[i] = old_state->r[i] != new_state->r[i];

	if(old_state->pstate.rw == PSTATE_RW_64 && new_state->pstate.rw == PSTATE_RW_64)
	{
		for(int i = 16; i < 32; i++)
			change->r[i] = old_state->r[i] != new_state->r[i];
	}

	change->r[32] = old_state->r[32] != new_state->r[32];

	change->cpsr = old_state->cpsr != new_state->cpsr || old_state->pstate.rw == PSTATE_RW_64;

	// enough to check if the field is used in the target mode
	change->pstate.rw = old_state->pstate.rw != new_state->pstate.rw;
	change->pstate.mode = old_state->pstate.mode != new_state->pstate.mode || old_state->pstate.rw == PSTATE_RW_64;
	change->pstate.f = old_state->pstate.f != new_state->pstate.f;
	change->pstate.i = old_state->pstate.i != new_state->pstate.i;
	change->pstate.j = (old_state->pstate.jt & PSTATE_JT_JAZELLE) != (new_state->pstate.jt & PSTATE_JT_JAZELLE);
	change->pstate.t = (old_state->pstate.jt & PSTATE_JT_THUMB) != (new_state->pstate.jt & PSTATE_JT_THUMB);
	change->pstate.a = old_state->pstate.a != new_state->pstate.a;
	change->pstate.sp = old_state->pstate.sp != new_state->pstate.sp || old_state->pstate.rw != PSTATE_RW_64;
	change->pstate.el = old_state->pstate.el != new_state->pstate.el || old_state->pstate.rw != PSTATE_RW_64;

	change->pstate.il = old_state->pstate.il != new_state->pstate.il;
	change->pstate.ss = old_state->pstate.ss != new_state->pstate.ss;
	change->pstate.pan = old_state->pstate.pan != new_state->pstate.pan;
	change->pstate.uao = old_state->pstate.uao != new_state->pstate.uao;
	change->pstate.v = old_state->pstate.v != new_state->pstate.v;
	change->pstate.c = old_state->pstate.c != new_state->pstate.c;
	change->pstate.z = old_state->pstate.z != new_state->pstate.z;
	change->pstate.n = old_state->pstate.n != new_state->pstate.n;

	if(new_state->pstate.rw != PSTATE_RW_64)
	{
		change->pstate.q = old_state->pstate.q != new_state->pstate.q;
		change->pstate.ge = old_state->pstate.ge != new_state->pstate.ge;
		change->pstate.e = old_state->pstate.e != new_state->pstate.e;
		change->pstate.it = old_state->pstate.it != new_state->pstate.it;
	}
	else
	{
		change->pstate.d = old_state->pstate.d != new_state->pstate.d;
	}

	if((config.features & (1 << FEATURE_FPA)) != 0)
	{
		for(int i = 0; i < 16; i++)
			change->f[i] = old_state->f[i] != new_state->f[i];
	}
	if(arm_support_vfp_registers(config))
	{
		for(int i = 0; i < 32; i++)
		{
			change->d[i] = (((old_state->format_bits ^ new_state->format_bits) >> i) & 1) || old_state->w[i] != new_state->w[i];
		}
	}

	if(old_state->pstate.jt == PSTATE_JT_JAZELLE && new_state->pstate.jt == PSTATE_JT_JAZELLE)
	{
		int32_t delta = ((int32_t)new_state->j32_stack_pointer - (int32_t)old_state->j32_stack_pointer) / 4;
		int32_t i;

		for(i = 0; i < 4; i++)
		{
			if(i - delta < 0)
				change->j32_stack[i] = true;
			else if(i - delta < 4)
				change->j32_stack[i] = old_state->j32_stack[i - delta] != new_state->j32_stack[i];
			else
				change->j32_stack[i] = false;
		}
	}

	change->memory_changed_lowest = old_state->memory_changed_lowest;
	change->memory_changed_highest = old_state->memory_changed_highest;
}

static const char * const regnames[16] = { [A32_SP] = "SP", "LR", "PC" };
static const char * const modenames[16] =
{
	[MODE_USR] = "usr",
	[MODE_FIQ] = "fiq",
	[MODE_IRQ] = "irq",
	[MODE_SVC] = "svc",
	[MODE_MON] = "mon",
	[MODE_ABT] = "abt",
	[MODE_HYP] = "hyp",
	[MODE_UND] = "und",
	[MODE_SYS] = "sys",
};

static const char * const a32_condition[16] = { "eq", "ne", "cs"/*"hs"*/, "cc"/*"lo"*/, "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", ""/*"al"*/, "nv" };

static void a32_debug(FILE * file, arm_state_t * cpu, arm_debug_change_t * change)
{
	int i;
	for(i = 0; i < A32_SP - 8; i++)
	{
		fprintf(file, "R%d=\t%s%08X%s\tR%d=\t%s%08X%s\n",
			i,
			change && change->r[i] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i),
			change && change->r[i] ? ANSI_RESET : "",
			i + 8,
			change && change->r[i + 8] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i + 8),
			change && change->r[i + 8] ? ANSI_RESET : "");
	}
	for(; i < A32_PC_NUM - 8; i++)
	{
		fprintf(file, "R%d=\t%s%08X%s\t%s=\t%s%08X%s\n",
			i,
			change && change->r[i] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i),
			change && change->r[i] ? ANSI_RESET : "",
			regnames[i + 8],
			change && change->r[i + 8] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i + 8),
			change && change->r[i + 8] ? ANSI_RESET : "");
	}
	fprintf(file, "R%d=\t%s%08X%s\t%s=\t%s%08X%s\n",
		i,
		change && change->r[i] ? ANSI_BOLD : "",
		a32_register_get32(cpu, i),
		change && change->r[i] ? ANSI_RESET : "",
		regnames[i + 8],
		change && change->r[32] ? ANSI_BOLD : "",
		a32_register_get32_lhs(cpu, i + 8),
		change && change->r[32] ? ANSI_RESET : "");

	if(!a32_is_arm26(cpu))
	{
		fprintf(file, "CPSR=\t%s%08X%s\n",
			change && change->cpsr ? ANSI_BOLD : "",
			a32_get_cpsr(cpu),
			change && change->cpsr ? ANSI_RESET : "");
	}

	fprintf(file, "N=%s%d%s,Z=%s%d%s,C=%s%d%s,V=%s%d%s,E=%s%d%s,I=%s%d%s,F=%s%d%s,J=%s%d%s,T=%s%d%s, ",
		change && change->pstate.n ? ANSI_BOLD : "",
		cpu->pstate.n,
		change && change->pstate.n ? ANSI_RESET : "",
		change && change->pstate.z ? ANSI_BOLD : "",
		cpu->pstate.z,
		change && change->pstate.z ? ANSI_RESET : "",
		change && change->pstate.c ? ANSI_BOLD : "",
		cpu->pstate.c,
		change && change->pstate.c ? ANSI_RESET : "",
		change && change->pstate.v ? ANSI_BOLD : "",
		cpu->pstate.v,
		change && change->pstate.v ? ANSI_RESET : "",
		change && change->pstate.e ? ANSI_BOLD : "",
		cpu->pstate.e,
		change && change->pstate.e ? ANSI_RESET : "",
		change && change->pstate.i ? ANSI_BOLD : "",
		cpu->pstate.i,
		change && change->pstate.i ? ANSI_RESET : "",
		change && change->pstate.f ? ANSI_BOLD : "",
		cpu->pstate.f,
		change && change->pstate.f ? ANSI_RESET : "",
		change && change->pstate.j ? ANSI_BOLD : "",
		cpu->pstate.jt >> 1,
		change && change->pstate.j ? ANSI_RESET : "",
		change && change->pstate.t ? ANSI_BOLD : "",
		cpu->pstate.jt & 1,
		change && change->pstate.t ? ANSI_RESET : "");

	int mode = cpu->pstate.mode;
	fprintf(file, "mode: ");
	if(change && change->pstate.mode)
		fprintf(file, "%s", ANSI_BOLD);
	if(modenames[mode])
		fprintf(file, "%s", modenames[mode]);
	else
		fprintf(file, "invalid %X", mode);
	if(change && change->pstate.mode)
		fprintf(file, "%s", ANSI_RESET);
	fprintf(file, ", ");
	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
		if(change && change->pstate.rw)
			fprintf(file, "%s", ANSI_BOLD);
		fprintf(file, "ARM26");
		if(change && change->pstate.rw)
			fprintf(file, "%s", ANSI_RESET);
		break;
	case PSTATE_RW_32:
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_BOLD);
			fprintf(file, "ARM32");
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_RESET);
			break;
		case PSTATE_JT_THUMB:
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_BOLD);
			fprintf(file, "Thumb");
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_RESET);
			break;
		case PSTATE_JT_THUMBEE:
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_BOLD);
			fprintf(file, "ThumbEE");
			if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
				fprintf(file, "%s", ANSI_RESET);
			break;
		}
		break;
	}
	if((cpu->pstate.jt & PSTATE_JT_THUMB))
	{
		if((cpu->pstate.it & 0xF) != 0)
		{
			printf(", IT: ");
			int cond = (cpu->pstate.it & 0xF0) >> 4;
			int mask = cpu->pstate.it & 0x0F;
			if(change && change->pstate.it)
				fprintf(file, "%s", ANSI_BOLD);
			printf("%s", a32_condition[cond]);
			while((mask & 0x7) != 0)
			{
				cond = (cond & ~1) | (mask >> 3);
				mask = (mask << 1) & 0x0F;
				printf(", %s", a32_condition[cond]);
			}
			if(change && change->pstate.it)
				fprintf(file, "%s", ANSI_RESET);
		}
	}
	printf("\n");
}

static inline bool j32_stack_value_changed(arm_state_t * cpu, uint8_t index, arm_debug_change_t * change)
{
	if(!change)
		return false;

	return change->j32_stack[index];
}

#if J32_EMULATE_INTERNALS
static const char * const j32_register_names[] = { "R0", "R1", "R2", "R3", "R4", "R5", "TOS", "LOC", "CP", NULL, NULL, NULL, "R12", NULL, "R14", "PC" };
#endif

static void j32_debug(FILE * file, arm_state_t * cpu, arm_debug_change_t * change)
{
#if J32_EMULATE_INTERNALS
	// ignore R9, R10, R11, R13, optional: R12, R14
	int i;
	for(i = 0; i < 4; i++)
	{
		fprintf(file, "%3s=%s%08X%s\t",
			j32_register_names[i],
			change && change->r[i] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i),
			change && change->r[i] ? ANSI_RESET : "");
		fprintf(file, "%3s=%s%08X%s\n",
			j32_register_names[i + 5],
			change && change->r[i + 5] ? ANSI_BOLD : "",
			a32_register_get32(cpu, i + 5),
			change && change->r[i + 5] ? ANSI_RESET : "");
	}
	fprintf(file, "%3s=%s%08X%s\t",
		j32_register_names[i],
		change && change->r[i] ? ANSI_BOLD : "",
		a32_register_get32(cpu, i),
		change && change->r[i] ? ANSI_RESET : "");
	fprintf(file, "%3s=%s%08X%s\n",
		j32_register_names[A32_PC_NUM],
		change && change->r[32] ? ANSI_BOLD : "",
		a32_register_get32(cpu, A32_PC_NUM),
		change && change->r[32] ? ANSI_RESET : "");
#else
	fprintf(file, "TOS=%s%08X%s\t",
		change && change->r[J32_TOS] ? ANSI_BOLD : "",
		a32_register_get32(cpu, J32_TOS),
		change && change->r[J32_TOS] ? ANSI_RESET : "");
	fprintf(file, "LOC=%s%08X%s\t",
		change && change->r[J32_LOC] ? ANSI_BOLD : "",
		a32_register_get32(cpu, J32_LOC),
		change && change->r[J32_LOC] ? ANSI_RESET : "");
	fprintf(file, "CP =%s%08X%s\t",
		change && change->r[J32_CP] ? ANSI_BOLD : "",
		a32_register_get32(cpu, J32_CP),
		change && change->r[J32_CP] ? ANSI_RESET : "");
	fprintf(file, "PC =%s%08X%s\n",
		change && change->r[32] ? ANSI_BOLD : "",
		a32_register_get32(cpu, A32_PC_NUM),
		change && change->r[32] ? ANSI_RESET : "");
#endif

#if J32_EMULATE_INTERNALS
	fprintf(file, "Fast stack: ");
	{
		uint32_t size = j32_get_fast_stack_size(cpu);
		if(size == 0)
		{
			fprintf(file, "fully flushed\n");
		}
		else
		{
			uint32_t top = j32_get_fast_stack_top(cpu);
			for(uint32_t i = 0; i < size; i++)
				fprintf(file, "%sR%d", i == 0 ? "" : ", ", (top - i) & 3);
			fprintf(file, "\n");
		}
	}
#endif

	uint32_t loc = a32_register_get32(cpu, J32_LOC);
	for(int i = 0; i < 4; i++)
	{
		fprintf(file, "STK[%d] = %s%08X%s\t",
			i,
			j32_stack_value_changed(cpu, i, change) ? ANSI_BOLD : "",
			j32_get_stack_value(cpu, i),
			j32_stack_value_changed(cpu, i, change) ? ANSI_RESET : "");

		uint32_t address = loc + 4 * i;
		fprintf(file, "LOC[%d] = %s%08X%s",
			i,
			change && change->memory_changed_lowest <= address + 3 && address < change->memory_changed_highest ? ANSI_BOLD : "",
			arm_memory_read32_data(cpu, address),
			change && change->memory_changed_lowest <= address + 3 && address < change->memory_changed_highest ? ANSI_RESET : "");

		fprintf(file, "\n");
	}

	fprintf(file, "mode: ");
	if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
		fprintf(file, "%s", ANSI_BOLD);
	fprintf(file, "Jazelle");
	if(change && (change->pstate.rw || change->pstate.j || change->pstate.t))
		fprintf(file, "%s", ANSI_RESET);
	fprintf(file, "\n");
}

static void a64_debug(FILE * file, arm_state_t * cpu, arm_debug_change_t * change)
{
	int i;
	for(i = 0; i < 15; i++)
	{
		fprintf(file, "R%d=\t%s%016lX%s\tR%d=\t%s%016lX%s\n",
			i,
			change && change->r[i] ? ANSI_BOLD : "",
			a64_register_get64(cpu, i, PERMIT_SP),
			change && change->r[i] ? ANSI_RESET : "",
			i + 16,
			change && change->r[i + 16] ? ANSI_BOLD : "",
			a64_register_get64(cpu, i + 16, PERMIT_SP),
			change && change->r[i + 16] ? ANSI_RESET : "");
	}
	fprintf(file, "R%d=\t%s%016lX%s\tSP=\t%s%016lX%s\n",
		i,
		change && change->r[i] ? ANSI_BOLD : "",
		a64_register_get64(cpu, i, PERMIT_SP),
		change && change->r[i] ? ANSI_RESET : "",
		change && change->r[i + 16] ? ANSI_BOLD : "",
		a64_register_get64(cpu, i + 16, PERMIT_SP),
		change && change->r[i + 16] ? ANSI_RESET : "");
	fprintf(file, "PC=\t%s%016lX%s\t",
		change && change->r[32] ? ANSI_BOLD : "",
		cpu->r[PC],
		change && change->r[32] ? ANSI_RESET : "");
	fprintf(file, "N=%s%d%s,Z=%s%d%s,C=%s%d%s,V=%s%d%s,D=%s%d%s,A=%s%d%s,I=%s%d%s,F=%s%d%s ",
		change && change->pstate.n ? ANSI_BOLD : "",
		cpu->pstate.n,
		change && change->pstate.n ? ANSI_RESET : "",
		change && change->pstate.z ? ANSI_BOLD : "",
		cpu->pstate.z,
		change && change->pstate.z ? ANSI_RESET : "",
		change && change->pstate.c ? ANSI_BOLD : "",
		cpu->pstate.c,
		change && change->pstate.c ? ANSI_RESET : "",
		change && change->pstate.v ? ANSI_BOLD : "",
		cpu->pstate.v,
		change && change->pstate.v ? ANSI_RESET : "",
		change && change->pstate.d ? ANSI_BOLD : "",
		cpu->pstate.d,
		change && change->pstate.d ? ANSI_RESET : "",
		change && change->pstate.a ? ANSI_BOLD : "",
		cpu->pstate.a,
		change && change->pstate.a ? ANSI_RESET : "",
		change && change->pstate.i ? ANSI_BOLD : "",
		cpu->pstate.i,
		change && change->pstate.i ? ANSI_RESET : "",
		change && change->pstate.f ? ANSI_BOLD : "",
		cpu->pstate.f,
		change && change->pstate.f ? ANSI_RESET : "");

	fprintf(file, "mode: ");
	if(change && (change->pstate.el || change->pstate.sp))
		fprintf(file, "%s", ANSI_BOLD);
	fprintf(file, "EL%d", cpu->pstate.el);
	if(cpu->pstate.el != 0)
		fprintf(file, "%c", cpu->pstate.sp ? 'h' : 't');
	if(change && (change->pstate.el || change->pstate.sp))
		fprintf(file, "%s", ANSI_RESET);
	if(cpu->pstate.el != 0)
	{
		fprintf(file, " with ");
		if(change && (change->pstate.el || change->pstate.sp))
			fprintf(file, "%s", ANSI_BOLD);
		fprintf(file, "SP_EL%d", cpu->pstate.sp ? cpu->pstate.el : 0);
		if(change && (change->pstate.el || change->pstate.sp))
			fprintf(file, "%s", ANSI_RESET);
	}
	fprintf(file, ", ");
	if(change && change->pstate.rw)
		fprintf(file, "%s", ANSI_BOLD);
	fprintf(file, "ARM64");
	if(change && change->pstate.rw)
		fprintf(file, "%s", ANSI_RESET);
	fprintf(file, "\n");
}

void debug(FILE * file, arm_state_t * cpu, arm_debug_state_t * old_state)
{
	arm_debug_change_t change;
	arm_debug_state_t new_state[1];

	if(old_state)
	{
		arm_get_debug_state(new_state, cpu);
		arm_compare_debug_state(&change, old_state, new_state, cpu->config);
	}

	switch(cpu->pstate.rw)
	{
	case PSTATE_RW_26:
		/* ARM26 */
		a32_debug(file, cpu, old_state ? &change : NULL);
		break;
	case PSTATE_RW_32:
		switch(cpu->pstate.jt)
		{
		case PSTATE_JT_ARM:
		case PSTATE_JT_THUMB:
		case PSTATE_JT_THUMBEE:
			/* ARM32, Thumb or ThumbEE */
			a32_debug(file, cpu, old_state ? &change : NULL);
			break;
		case PSTATE_JT_JAZELLE:
			/* Jazelle */
			j32_debug(file, cpu, old_state ? &change : NULL);
			break;
		}
		break;
	case PSTATE_RW_64:
		/* ARM64 */
		a64_debug(file, cpu, old_state ? &change : NULL);
		break;
	}

	if(old_state && change.memory_changed_lowest <= change.memory_changed_highest)
	{
		fprintf(file, "Altered at ");
		if(cpu->pstate.rw != PSTATE_RW_64 && (change.memory_changed_highest & ~(uint64_t)0xFFFFFFFF) == 0)
			fprintf(file, "%08X", (uint32_t)change.memory_changed_lowest);
		else
			fprintf(file, "%016lX", change.memory_changed_lowest);
		fprintf(file, ":" ANSI_BOLD);
		int i;
		for(i = 0; i < 16 && change.memory_changed_lowest + i <= change.memory_changed_highest; i++)
		{
			fprintf(file, " %02X", arm_memory_read8_data(cpu, change.memory_changed_lowest + i));
		}
		if(change.memory_changed_lowest + i <= change.memory_changed_highest)
			fprintf(file, " ...");
		fprintf(file, ANSI_RESET "\n");
	}

	if(old_state)
	{
		*old_state = *new_state;
	}
}

