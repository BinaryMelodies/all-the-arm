
/* Simple Hello, World test */

#define ABI_IGNORE_ARGC_ARGV_ENVP 1
#include "syscall.h"

int main(void)
{
	write(1, "Hello!\n", 7);
	return 123;
}

