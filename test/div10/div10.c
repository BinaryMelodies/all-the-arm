
#include <stddef.h>
#include "syscall.h"

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

int main(void)
{
	putdec_u(12345);
	return 123;
}

