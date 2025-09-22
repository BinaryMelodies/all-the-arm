
/* Tests the command line arguments and the environment variables */

#include <stddef.h>
#include "syscall.h"

size_t strlen(const char * s)
{
	size_t i;
	for(i = 0; s[i] != '\0'; i++)
		;
	return i;
}

void putstr(const char * s)
{
	write(1, s, strlen(s));
}

void putdec_u(size_t value)
{
	char buffer[sizeof(size_t) * 5 / 2];
	size_t ptr = sizeof buffer;
	do
	{
		int d = value % 10;
		value /= 10;
		buffer[--ptr] = '0' + d;
	} while(value != 0);
	write(1, &buffer[ptr], sizeof buffer - ptr);
}

int main(int argc, char ** argv, char ** envp)
{
	putstr("argc = ");
	putdec_u(argc);
	putstr("\n");
	for(int i = 0; i < argc; i++)
	{
		putstr("argv[");
		putdec_u(i);
		putstr("] = \"");
		putstr(argv[i]);
		putstr("\"\n");
	}
	for(int i = 0; envp[i] != 0; i++)
	{
		putstr("envp[");
		putdec_u(i);
		putstr("] = \"");
		putstr(envp[i]);
		putstr("\"\n");
	}
	return 0;
}

