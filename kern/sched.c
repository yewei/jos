#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>


// Choose a user environment to run and run it.
void
sched_yield(void)
{
	// Implement simple round-robin scheduling.
	// Search through 'envs' for a runnable environment,
	// in circular fashion starting after the previously running env,
	// and switch to the first such environment found.
	// It's OK to choose the previously running env if no other env
	// is runnable.  If NO env is runnable, fall through to the idle loop.

	// LAB 3: Your code here. (Exercise 9)


	// If we reach this point, we know no environment is runnable.
	// Operating systems model this with an IDLE LOOP, which absorbs
	// leftover CPU time, often using a HLT instruction to conserve
	// power.
	// In most OSes the idle loop only runs until a different process
	// unblocks, because for example data read from disk is ready, or
	// because a packet arrives -- some hardware interrupt.
	// JOS has no such sources of interrupts.
	// Thus, once we reach the idle loop, the OS will never run any other
	// process again.
	cprintf("Idle loop - nothing more to do!\n");
	while (1)
		monitor(NULL);
}
