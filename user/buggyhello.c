// buggy hello world -- unmapped pointer passed to kernel
// kernel should destroy user environment in response

#include <inc/lib.h>

void
umain(int, char **)
{
	sys_cputs((char*)1, 1);
}

