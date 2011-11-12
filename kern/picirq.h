/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PICIRQ_H
#define JOS_KERN_PICIRQ_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#define MAX_IRQS	16	// Number of IRQs

// I/O Addresses of the two 8259A programmable interrupt controllers
#define IO_PIC1		0x20	// Master (IRQs 0-7)
#define IO_PIC2		0xA0	// Slave (IRQs 8-15)

#define IRQ_SLAVE	2	// IRQ at which slave connects to master
#define IRQ_OFFSET	32	// IRQ 0 corresponds to int IRQ_OFFSET

// Hardware IRQ numbers. We receive these as (IRQ_OFFSET+IRQ_WHATEVER)
#define IRQ_TIMER        0
#define IRQ_KBD          1
#define IRQ_SPURIOUS     7
#define IRQ_IDE         14
#define IRQ_ERROR       19

#endif // !JOS_KERN_PICIRQ_H
