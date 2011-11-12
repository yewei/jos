// program to cause a breakpoint trap

#include <inc/lib.h>

void
umain(int, char **)
{
	asm volatile("int $3");
}

