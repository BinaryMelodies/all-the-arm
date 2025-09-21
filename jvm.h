#ifndef _JVM_H
#define _JVM_H

/* Constants for parsing Java class files and executing a minimal JVM environment */

#include <stdint.h>
#include <stdio.h>
#include "main.h"
#include "arm.h"

typedef enum jvm_constant_type_t
{
	CONSTANT_Utf8 = 1,
	CONSTANT_Integer = 3,
	CONSTANT_Float = 4,
	CONSTANT_Long = 5,
	CONSTANT_Double = 6,
	CONSTANT_Class = 7,
	CONSTANT_String = 8,
	CONSTANT_Fieldref = 9,
	CONSTANT_Methodref = 10,
	CONSTANT_InterfaceMethodref = 11,
	CONSTANT_NameAndType = 12,
	CONSTANT_MethodHandle = 15,
	CONSTANT_MethodType = 16,
	CONSTANT_InvokeDynamic = 18,
} jvm_constant_type_t;

typedef struct jvm_constant_t
{
	jvm_constant_type_t type;
	union
	{
		uint16_t _class;
		struct
		{
			uint16_t class_index;
			uint16_t name_and_type_index;
		} fieldref, methodref, interfacemethodref;
		uint16_t string;
		uint32_t integer;
		float32_t _float;
		struct
		{
			uint16_t name_index;
			uint16_t type_index;
		} name_and_type;
		struct
		{
			uint16_t length;
			char * bytes;
		} utf8;
		uint64_t _long;
		float64_t _double;
	};
} jvm_constant_t;

enum
{
	T_BOOLEAN = 4,
	T_CHAR = 5,
	T_FLOAT = 6,
	T_DOUBLE = 7,
	T_BYTE = 8,
	T_SHORT = 9,
	T_INT = 10,
	T_LONG = 11,

	ACC_STATIC = 0x0008,
};

enum
{
	J32_LINK = 9,
	J32_HEAP = 10,
};

extern jvm_constant_t * constant_pool;

void read_class_file(FILE * input_file, environment_t * env);

extern void j32_invoke(arm_state_t * cpu, uint32_t argument_count, uint32_t local_count, uint32_t address);
extern bool j32_simulate_instruction(arm_state_t * cpu, uint32_t heap_start);

#endif // _JVM_H
