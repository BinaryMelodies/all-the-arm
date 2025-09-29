
WARNING! THIS PROJECT IS EXPERIMENTAL, INCOMPLETE AND HIGHLY UNTESTED!

IT IS INTENDED AS AN EDUCATIONAL TOOL AND LEARNING EXPERIMENT, AND AS SUCH IT COMES WITH ABSOLUTELY NO WARRANTY! USE AT YOUR OWN RISK!

The author shares this code with the expectation that others might find it informative, but it is expected that most of it will not work in its current state.

# An ARM emulator and disassembler for multiple generations

This project is an ARM architecture emulator and disassembler, intended to support multiple ARM generations going back to the 1980s.
The emulator and disassembler supports multiple processor versions, instruction sets and environments, from ARMv1 to ARMv8.

[Some collected information on the ARM architecture](https://html-preview.github.io/?url=https://github.com/BinaryMelodies/all-the-arm/blob/main/doc/info.html)

# Status

A lot of the ARM architecture is still missing. However, there is sufficient code to run very basic Linux binaries (such as a Hello, World application).

The non-floating point, non-SIMD, non-DSP and non-privileged instruction set is ready for the ARM, Thumb and Jazelle execution modes, and a subset of the A64 instructions are also available.
The disassembler can recognize all the instructions up to ARMv7, and many of the A64 instructions as well.

Rudimentary support for many of the features excluded above is also available.

The Linux emulation only recognizes 2 system calls: `exit` and `write`, accessed via either the OABI (in 32-bit ARM mode only) or EABI interfaces.
More can be easily implemented.

# Using the emulator/disassembler

The emulator/disassembler takes the name of an input file, and depending on its format and command line options given, will execute or disassemble it accordingly.

By default, files that are ELF executables or Java class files will be executed, and any other binary files will be disassembled.

The following options can modify this behavior:

* `-d`: Forces *disassembly*, or if execution is selected, enters *debug* mode.

* `-r`: Forces execution (*runs* the executable).

* `-l=...`: Sets the load address of the binary file, and unless the `-d` option is provided (without `-r` also being specified), executes the binary. Ignores the format of the file and loads it as a raw binary.

To set the initial execution/disassembly mode and instruction set, there are several options.
The emulator will force a CPU version that permits this execution mode.

* `-a26`: Emulates a mode that corresponds to the original 26-bit ARM environment. Not available from ARMv4 on.

* `-a32`: Emulates the 32-bit ARM instruction set. Only available starting from ARMv3, not available on M-profile variants.

* `-t16`: Emulates the 16-bit Thumb instruction set. Only available on ARMv4T, ARMv5T and processors starting from ARMv6.

* `-thumb1`: Emulates the pre-Thumb-2 instruction set, a specific version of the Thumb instruction set. This forces the processor version to be before ARMv6T2.

* `-thumb2`: Emulates the Thumb-2 instruction set, a specific version of the Thumb instruction set. This forces the processor to be ARMv6T2 or ARMv7 or later.

* `-j`: Emulates the Java bytecode in Jazelle mode. Only available on ARMv5TEJ, ARMv6 and some ARMv7 processors.

* `-tee`: Emulates the ThumbEE instruction set. Only available on ARMv7, not available on ARMv7-M.

* `-a64`: Emulates the A64 instruction set. Only available since ARMv8, not available on M-profile variants.

The CPU version can be selected with the `-v` flag.
Further options can be added using the `+` option, followed by a feature or coprocessor.

# Supported instruction sets

## ARM instruction set

The ARM instruction set is the original instruction set of ARM processors, introduced with the first version, ARMv1.
It is a fixed length, 32-bit wide instruction encoding that can address 16 general purpose registers, each 32 bits wide, and one of which references the program counter (and in 26-bit versions, the processor status flags).
Versions prior to ARMv5 had an implicit condition checking for all instructions, and most instructions are still conditional from ARMv5.

The emulator supports multiple versions of the ARM instruction set, including 26-bit as well as 32-bit variants.
Among others, it emulates the prototype ARMv1 with the instructions unavailable in later versions.
It emulates the first widely used 26-bit version, ARMv2.
It can also emulate switching between 26-bit and 32-bit modes in ARMv3.
Later versions are also supported, and in particular the emulator implements the following versions:

ARMv1, ARMv2, ARMv2a, ARMv3, ARMv3M, ARMv3G, ARMv4(xM), ARMv4T(xM), ARMv5(xM), ARMv5T(xM), ARMv5TE(xP), ARMv5TEJ, ARMv6, ARMv6T2, ARMv7 (A and R profiles), ARMv8 (A and R profiles)

## Thumb intsruction set

The Thumb instruction set is a compressed instruction set for ARM that was originally a fixed width 16-bit wide instruction encoding, introduced with ARMv4T.
The processor state and register set maps to the ARM mode in a straight forward manner, and in particular all registers are treated as 32-bit.
Most instructions in the original Thumb can only access the first 8 general purpose registers and do not have implicit condition checking.
There are also 2 pairs of 16-bit instructions that function as one unit (`bl` and since ARMv5T, `blx`).

Later in Thumb-2, introduced in ARMv6T2, the instruction set was extended to a variable length encoding, where instructions can be either 16-bit or 32-bit.
This was possible by reinterpreting the first part of the 2 pairs of 16-bit instructions from the original Thumb encoding as a prefix code for 32-bit instructions.
Thumb-2 supports instructions that can access all registers, and provides conditional blocks (If-Then blocks) that can span up to 4 instructions.
Thereby it extends the instruction set in a way that is essentially equivalent to the ARM instruction set but with a different encoding.

Thumb-2 also has a separate execution mode called ThumbEE that was only implemented on ARMv7.
It was intended to be used for just-in-time compiled memory managed environments.
Most instructions are identical, with a few replaced or altered for better memory management, and all memory instructions check for null pointers before memory access.

The emulator implements the following versions:

ARMv4T, ARMv5T, ARMv6, ARMv6T2, ARMv7 (A and R profiles), ARMv8 (A and R profiles)

Furthermore, the M-profile offers further variants for the Thumb instruction set:

ARMv6-M, ARMv7-M

## Jazelle instruction set

The Jazelle exection mode enables running Java bytecode directly on the processor.
The Java bytecode is a variable length instruction set where instructions can be as short as a single byte, or take multiple parameters (always stored as big endian values) with no theoretical boundaries (such as `lookupswitch` or `tableswitch`).
Since the Java bytecode includes many instructions that require a Java virtual machine to be running, including memory management and an object model, Jazelle does not implement all the bytecodes.
Instead, any unimplemented bytecode would issue a call to a handler in ARM (or Thumb) mode that would simulate the missing instruction.

The Jazelle execution mode is probably the least documented part of the ARM architecture.
Documentation is sparse and confidential, with developers expected to obtain a license for the required technology from ARM.
Furthermore modern implementations resort to the "trivial" implementation where no bytecode is ever executed.
However there have been efforts to reverse engineer its functioning, and this emulator attempts to replicate the known behavior.

Jazelle is not a fully functional, stand alone instruction set for the ARM processor, with a lot of its functionality expected to be provided by the environment.
This project attempts to provide a completely new design, unrelated to ARM's work, that extends native Jazelle mode to a stand alone environment, similarly to how Thumb has been extended in ARMv7.
To avoid creating a completely new set of instructions from scratch, some of these instructions have been borrowed from a separate processor that provides a hardware JVM bytecode implementation, picoJava.
The way picoJava and Jazelle execute Java bytecode is not compatible, for one the stack grows downwards in picoJava but upwards in Jazelle, so a full picoJava execution environment is unlikely to be implemented.
However, the disassembler is capable of recognizing all picoJava instructions, if configured that way.
By default, the emulator/disassembler will only recognize instructions available in Jazelle, but the `+javaext` flag (added to the `-v` option) enables all the extensions.

When compiling the code base, if the `J32_EMULATE_INTERNALS` flag is defined and non-zero, the emulator will attempt to replicate as much of the known behavior of Jazelle mode as possible, including caching stack values in processor registers.
Otherwise it only emulates the parts of the behavior visible from ARM mode, such as the Jazelle mode stack pointer.

Further resources on the behavior of the Jazelle execution state:

* https://hackspire.org/index.php/Jazelle
* https://github.com/SonoSooS/libjz
* https://git.lain.faith/BLAHAJ/jacking
* https://github.com/neuschaefer/jzvm

## A64 instruction set

ARMv8 introduced a new 64-bit execution environment, and with it a new instruction set called A64.
Like the original ARM instruction set, it is a fixed width, 32-bit encoding.
The register set is expanded to 32 general purpose registers, all of which can be accessed as 32-bit or 64-bit values, and register 31 is hardwired to constant zero.
Some instructions access the stack pointer when register 31 is specified.
The program counter is no longer a general purpose register.
Condition checks are only provided for certain instructions.

The emulator implements a subset of the A64 instruction set, which corresponds mostly to user-mode instructions that are not floating point or SIMD instructions.
The goal is to support a full 64-bit environment as well as the 32-bit version.

## Floating Point Accelerator instruction set

The first floating point coprocessor for ARM was the Floating Point Accelerator.
Although it was only ever produced for ARMv2 and ARMv3, software emulation was available, and the early ARM Procedure Calling Specification references it.

The FPA provides 8 floating-point registers, each encoding an 80-bit IEEE extended double precision value.

The emulator provides a rudimentary implementation, but much of the IEEE standard is missing.

## Vector Floating Point and Advanced SIMD instruction sets

A newer floating point instruction set, introduced for ARMv5TE, the Vector Floating Point instruction set was later implemented.
Currently, 5 versions of this instruction set are available.

The original VFP provided 32 single precision floating point registers that can be paired as 16 double precision floating point registers in some implementations.
Later, the number of double precision registers was extended to 32.
The register set can also be organized as vectors of up to 4 single precision registers, and arithmetic instructions can handle them as vectors.
This behavior was later deprecated.
Since VFPv3, a separate SIMD instruction set is available that can also handle these registers as up to 128-bit units.

The emulator provides a rudimentary implementation for most instructions up to VFPv2.
The following versions are recognized.

VFPv1, VFPv2, VFPv3 and Advanced SIMDv1, VFPv4/FPv4 and Advanced SIMDv2, FPv5

## Endianness

Depending on which ARM version is chosen, the emulator can run in little endian mode (LE), word invariant big endian mode (BE-32) and byte invariant big endian mode. (BE-8)

The original ARM was little endian, meaning that the least significant byte of a word was stored at the lowest memory address.

ARMv3 introduced the option for word invariant big endian mode (BE-32), where each byte at address `A` would be accessed at memory address `A ^ 3` (where `^` is the bitwise exclusive-or operation).
This means that the bytes read at addresses 0, 1, 2, 3, 4, 5... would be those actually stored at addresses 3, 2, 1, 0, 7, 6..., but word reads at aligned addresses (that is, address values that are divisible by 4) would still be read as the same value as in little endian mode, hence the name _word invariant_ big endian mode.
This made it possible to read the bytes inside an (aligned) word in a big endian ordering.
All data and instructions are seen as big endian.
This mode has to be configured at the system level, and it was discontinued from ARMv7 on.

ARMv6 introduced the option for byte invariant big endian mode (BE-8).
Instead of changing the memory addresses of bytes, every memory read and write that uses multiple bytes would load those bytes in a big endian order, hence the name _byte invariant_ big endian mode.
This does not impact instruction fetches, where the instruction stream is still interpreted the same way as in little endian mode.
This mode can be enabled or disabled by a user privileged program.

# Supported environments

The emulator also offers a barebone Linux application environment.
It can parse ELF executables, transfer command line arguments and environment variables and simulate system calls.

## Minimal Java environment

To test the Jazelle mode, the emulator can parse Java classes and offers a minimalistic JVM environment.
To simplify the implementation, and avoid integrating a full JVM into the emulator, there are strict restrictions as to what the emulator can execute:

* All methods must be static, and only static method calls are permitted. Loaded class files can contain instance methods but the emulator halts if a virtual call is attempted.
* As a special exception, invokedynamic can be used to create function pointers to static methods using LambdaMetafactory. Such function pointers can be called via an interface call.
* No objects can be instantiated, except for arrays.
* Compile time String constants are also allowed, as long as the only operation done on them is getBytes().
* No exception handling support is provided.
* The entry point is a `static void _start()` or `static void _start(byte[][] argv, byte[][] envp)`. If one exists, the `<clinit>` method gets called first and returns to the beginning of the `_start` method.
* System calls are accessed via native static methods in the `abi.Linux` class.
* No garbage collection, the `abi.Linux.brk` system call can be used to deallocate arrays.

A Java class file that conforms to these expectations can run under this emulator as well as in any Java virtual machine, provided that the `main` method loads the native implementation of `abi.Linux` and `main` calls the `_start` method.

The register assignment is described in this table.
Some of the registers are hardwired by the ARM architecture, and some of them follow the way Jazelle works.
Those marked with _JVM_ are assigned by the emulator, and on a typical hardware they would be preserved between entering and exiting the Jazelle execution environment.

| Register | Assignment | picoJava   | Usage                                             |
| -------- | ---------- | ---------- | ------------------------------------------------- |
| R0       | Jazelle    | -          | stack value                                       |
| R1       | Jazelle    | -          | stack value                                       |
| R2       | Jazelle    | -          | stack value                                       |
| R3       | Jazelle    | -          | stack value                                       |
| R4       | Jazelle    | -          | local variable 0                                  |
| R5       | Jazelle    | -          | software handler table                            |
| R6       | Jazelle    | OPTOP      | pointer to top of stack                           |
| R7       | Jazelle    | VARS       | pointer to local variables                        |
| R8       | Jazelle    | CONST_POOL | pointer to constant pool                          |
| R9       | JVM        | FRAME      | method link pointer                               |
| R10      | JVM        | -          | heap top pointer                                  |
| R11      | -          | -          | -                                                 |
| R12      | Jazelle    | -          | Jazelle temporary, cannot be used in handler code |
| R13      | ARM        | -          | ARM mode stack pointer                            |
| R14      | Jazelle    | -          | pointer to code on entry/exit                     |
| R15      | ARM        | PC         | pointer to code                                   |

# Extensions to the ELF format

The disassembler recognizes the standard mapping symbols `$a` (for ARM mode), `$t` (for Thumb mode), `$t.x` (for ThumbEE mode) in ARM files, `$x` (for 64-bit code) in AArch64 files, and `$d` (for data) in either.
As an extension, 32-bit and 64-bit mapping symbols may be intermixed in the same file (`$a`/`$t`/`$t.x` for 32-bit modes and `$x` for 64-bit mode), as well as `$j` for Jazelle mode.

# References

A variety of references were used to map out information on older as well as newer architectures.
This is a non-exhaustive list of sources that were consulted during the development of this project.

* [ARM official documents](https://developer.arm.com/)
* [RISC OS 3 Programmer's Reference Manual](http://www.riscos.com/support/developers/asm/fpinstrs.html)
* Pete Cockerell: ARM Assembly Language Programming
* [Wikipedia for ARM architecture family](https://en.wikipedia.org/wiki/ARM_architecture_family)
* [Arc Wiki](https://arcwiki.org.uk/index.php/Main_Page)
* [WikiChip](https://en.wikichip.org/wiki/arm)
* [List of ARM processor IDs](https://github.com/bp0/armids/blob/master/arm.ids)
* [GNU assembler manual](https://sourceware.org/binutils/docs/as/index.html)
* [GCC manuals](https://gcc.gnu.org/onlinedocs/)

