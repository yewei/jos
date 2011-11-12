// program to cause a general protection exception

#include <inc/lib.h>

void
umain(int, char **)
{
	// Try to load the kernel's TSS selector into the DS register.
	asm volatile("movw $0x28,%ax; movw %ax,%ds");
}

