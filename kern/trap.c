#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/picirq.h>

static struct Taskstate ts;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};

extern "C" {
void devide_by_zero(void);
void debug_exception(void);
void nmi_handler(void);
void breakpoint_handler(void);
void overflow(void);
void bounds_check(void);
void illegal_opcode(void);
void device_not_avail(void);
void double_fault(void);
void invalid_tss(void);
void segment_not_present(void);
void stack_exception(void);
void general_protection(void);
void page_fault(void);
void floating_point_error(void);
void aligment_check(void);
void machine_check(void);
void simd_floating_error(void);
void system_call(void); 
}

static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Falt",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if ((size_t) trapno < sizeof(excnames) / sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";

	return "(unknown trap)";
}


void
idt_init(void)
{
	extern struct Segdesc gdt[];
	
	// LAB 2: Your code here.
	SETGATE(idt[T_DIVIDE], 1, GD_KT, &devide_by_zero, 0);
	SETGATE(idt[T_DEBUG], 1, GD_KT, &debug_exception, 0);
	SETGATE(idt[T_NMI], 1, GD_KT, &nmi_handler, 0);
	SETGATE(idt[T_BRKPT], 1, GD_KT, &breakpoint_handler, 3);
	SETGATE(idt[T_OFLOW], 1, GD_KT, &overflow, 3);
	SETGATE(idt[T_BOUND], 1, GD_KT, &bounds_check, 3);
	SETGATE(idt[T_ILLOP], 1, GD_KT, &illegal_opcode, 0);
	SETGATE(idt[T_DEVICE], 1, GD_KT, &device_not_avail, 0);
	SETGATE(idt[T_DBLFLT], 1, GD_KT, &double_fault, 0);
	SETGATE(idt[T_TSS], 1, GD_KT, &invalid_tss, 0);
	SETGATE(idt[T_SEGNP], 1, GD_KT, &segment_not_present, 0);
	SETGATE(idt[T_STACK], 1, GD_KT, &stack_exception, 0);
	SETGATE(idt[T_GPFLT], 1, GD_KT, &general_protection, 0);
	SETGATE(idt[T_PGFLT], 1, GD_KT, &page_fault, 0);
	SETGATE(idt[T_FPERR], 1, GD_KT, &floating_point_error, 0);
	SETGATE(idt[T_ALIGN], 1, GD_KT, &aligment_check, 0);
	SETGATE(idt[T_MCHK], 1, GD_KT, &machine_check, 0);
	SETGATE(idt[T_SIMDERR], 1, GD_KT, &simd_floating_error, 0);

	// Set a gate for the system call interrupt.
	// Hint: Must this gate be accessible from userlevel?
	// LAB 3: Your code here.
	SETGATE(idt[T_SYSCALL], 1, GD_KT, system_call, 3);
	
	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = KSTACKTOP;
	ts.ts_ss0 = GD_KD;

	// Initialize the TSS field of the gdt.
	gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
					sizeof(struct Taskstate), 0);
	gdt[GD_TSS >> 3].sd_s = 0;

	// Load the TSS
	ltr(GD_TSS);

	// Load the IDT
	asm volatile("lidt idt_pd");
}


// We use these variables to print "Incoming TRAP frame" lines.
static struct Trapframe *print_trapframe_incoming, *print_trapframe_copy;

void
print_trapframe(struct Trapframe *tf)
{
	if (tf == print_trapframe_copy)
		cprintf("Incoming TRAP frame at %p\n", print_trapframe_incoming);
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	cprintf("  err  0x%08x\n", tf->tf_err);
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
}

void
print_regs(struct Registers *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

extern "C" {
void
trap(struct Trapframe *tf)
{
	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		assert(curenv);
		curenv->env_tf = *tf;
		// Print incoming trapframe address as well as real address.
		print_trapframe_incoming = tf;
		print_trapframe_copy = &curenv->env_tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Dispatch based on what type of trap occurred
	switch (tf->tf_trapno) {

	// LAB 2: Your code here.
	case T_DIVIDE:
	case T_DEBUG:
	case T_NMI:
	case T_BRKPT:
	case T_OFLOW:
	case T_BOUND:
	case T_GPFLT:

	// Handle page faults (T_PGFLT).
	// LAB 3: Your code here.
	case T_PGFLT:
		cprintf("trigger interrupt/trap %s\n", trapname(tf->tf_trapno));
		panic("unhandled page faults");

	// Handle system calls (T_SYSCALL).
	// LAB 3: Your code here.

	default:
		// Unexpected trap: The user process or the kernel has a bug.
		print_trapframe(tf);
		if (tf->tf_cs == GD_KT)
			panic("unhandled trap in kernel");
		else {
			env_destroy(curenv);
			return;
		}
	}
	//panic("trap ok!");
        // Return to the current environment, which should be runnable.
        assert(curenv && curenv->env_status == ENV_RUNNABLE);
        env_run(curenv);
}
}

void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	
	// LAB 3: Your code here.

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

