// buggy program - faults with a write to a kernel location

#include <inc/lib.h>

void
umain(int, char **)
{
	*(unsigned*)0xf0100000 = 0;
}

