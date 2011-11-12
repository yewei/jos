// hello, world
#include <inc/lib.h>

void
umain(int, char **)
{
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", env->env_id);
}
