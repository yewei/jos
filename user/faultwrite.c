// buggy program - faults with a write to location zero

#include <inc/lib.h>

void
umain(int, char **)
{
	*(unsigned*)0 = 0;
}

