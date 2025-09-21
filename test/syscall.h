#ifndef _SYSCALL_H
#define _SYSCALL_H

asm(
	".global\t_start\n"
	"_start:\n"
	"\tbl\tmain\n"
	"\tbl\texit"
);

_Noreturn void exit(int status)
{
#if __arm__
	register int r0 asm("r0") = status;
# if __thumb__
	// the Thumb generator uses r7 as the frame pointer
	asm volatile("push\t{r7}\n"
		"\tmov\tr7, #1\n"
		"\tswi\t0\n"
		"\tpop\t{r7}" : : "r"(r0));
# else
	register int r7 asm("r7") = 1;
	asm volatile("swi\t0" : : "r"(r0), "r"(r7));
# endif
#elif __aarch64__
	register int w0 asm("w0") = status;
	register int w8 asm("w8") = 93;
	asm volatile("svc\t0" : : "r"(w8), "r"(w0));
#else
# error
#endif
	for(;;)
		;
}

long write(int fd, const void * buf, unsigned long count)
{
#if __arm__
	register int r0 asm("r0") = fd;
	register const void * r1 asm("r1") = buf;
	register unsigned long r2 asm("r2") = count;
	register long result asm("r0");
# if __thumb__
	asm volatile("push\t{r7}\n"
		"\tmov\tr7, #4\n"
		"\tswi\t0\n"
		"\tpop\t{r7}" : : "r"(r0));
# else
	// the Thumb generator uses r7 as the frame pointer
	register int r7 asm("r7") = 4;
	asm volatile("swi\t0" : "=r"(result) : "r"(r0), "r"(r1), "r"(r2), "r"(r7));
# endif
#elif __aarch64__
	register int w0 asm("w0") = fd;
	register const void * x1 asm("x1") = buf;
	register unsigned long x2 asm("x2") = count;
	register long result asm("x0");
	register int w8 asm("w8") = 64;
	asm volatile("svc\t0" : "=r"(result) : "r"(w0), "r"(x1), "r"(x2), "r"(w8));
#else
# error
#endif
	return result;
}

#endif /* _SYSCALL_H */
