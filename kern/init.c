/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/trap.h>
#include <kern/env.h>


extern "C" {
void
i386_init(void)
{
	extern char edata[], end[];
	extern const uint32_t sctors[], ectors[];
	const uint32_t *ctorva;

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	// Then call any global constructors.
	// This relies on linker script magic to define the 'sctors' and
	// 'ectors' symbols; see kern/kernel.ld.
	// Call after cons_init() so we can cprintf() if necessary.
	for (ctorva = ectors; ctorva > sctors; )
		((void(*)()) *--ctorva)();

	cprintf("6828 decimal is %o octal!\n", 6828);

	// Lab 2 memory management initialization functions
	mem_init();

	// Lab 2 interrupt and gate descriptor initialization functions
	idt_init();

	// Lab 3 user environment initialization functions
	env_init();

#ifdef TEST
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE);
#else
	// Touch all you want.
	ENV_CREATE(user_hello);
#endif // TEST*

	// We only have one user environment for now, so just run it.
	env_run(&envs[0]);

	// Drop into the kernel monitor.
	while (1)
		monitor(NULL);
}
}


/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
static const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void
_warn(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
