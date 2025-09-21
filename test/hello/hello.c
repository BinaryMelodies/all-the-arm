
#include "syscall.h"

int main(void)
{
	write(1, "Hello!\n", 7);
	return 123;
}

