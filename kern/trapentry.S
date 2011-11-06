/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>


###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text
	/*0*/ TRAPHANDLER_NOEC(devide_by_zero, T_DIVIDE)
	/*1*/ TRAPHANDLER_NOEC(debug_exception, T_DEBUG)
	/*2*/ TRAPHANDLER_NOEC(nmi_handler, T_NMI) /*no error code?*/
	/*3*/ TRAPHANDLER_NOEC(breakpoint_handler, T_BRKPT)
	/*4*/ TRAPHANDLER_NOEC(overflow, T_OFLOW)
	/*5*/ TRAPHANDLER_NOEC(bounds_check, T_BOUND)
	/*6*/ TRAPHANDLER_NOEC(illegal_opcode, T_ILLOP)
	/*7*/ TRAPHANDLER_NOEC(device_not_avail, T_DEVICE)
	/*8*/ TRAPHANDLER(double_fault, T_DBLFLT) /*error code is always 0*/
	/*10*/ TRAPHANDLER(invalid_tss, T_TSS)
	/*11*/ TRAPHANDLER(segment_not_present, T_SEGNP)
	/*12*/ TRAPHANDLER(stack_exception, T_STACK)
	/*13*/ TRAPHANDLER(general_protection, T_GPFLT)
	/*14*/ TRAPHANDLER(page_fault, T_PGFLT)
	/*16*/ TRAPHANDLER_NOEC(floating_point_error, T_FPERR)
	/*17*/ TRAPHANDLER_NOEC(aligment_check, T_ALIGN)
	/*18*/ TRAPHANDLER_NOEC(machine_check, T_MCHK)
	/*19*/ TRAPHANDLER_NOEC(simd_floating_error, T_SIMDERR)
	/*48*/ TRAPHANDLER_NOEC(system_call, T_SYSCALL)	
	
_alltraps:
	push %ds
	push %es
	pushal
	movl $(GD_KD), %edx
	mov %dx, %ds
	mov %dx, %es
	mov %dx, %fs
	pushl %esp /*pass a pointer to Trapframe*/
	call trap 
	addl $4, %esp /*clean up argument stack*/
	popal
	pop %es
	pop %ds
	addl $8, %esp /*clean up num & error code*/
	iret
