/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/monitor.h>
#include <kern/sched.h>

Env *envs = NULL;			// All environments
Env *curenv = NULL;			// The current env
static Env *free_envs = NULL;		// Free list

#define ENVGENSHIFT	12		// >= LOGNENV

// Convert an envid to an env pointer.
//
// RETURNS
//   0 on success, -E_BAD_ENV on error.
//   On success, sets *penv to the environment.
//   On error, sets *penv to NULL.
int
envid2env(envid_t envid, Env **env_store, bool checkperm)
{
	Env *e;

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = curenv;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check that structure's env_id field to ensure that the envid
	// is current (i.e., it refers to the _current_ environment in this
	// envs[] slot).
	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment must be either the
	// current environment or an immediate child of the current
	// environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	*env_store = e;
	return 0;
}


// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the env_free_list.
// Insert in reverse order, so that the first call to env_alloc()
// returns envs[0].  (This is important for grading.)
void
env_init(void)
{
	// LAB 3: Your code here.
	int idx;
	for (idx = NENV-1; idx >= 0; idx--) {
		envs[idx].env_status = ENV_FREE;
		envs[idx].env_id = 0;
		envs[idx].env_next = free_envs;
		free_envs = &envs[idx];
	}	
}


// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-E_NO_MEM if page directory or table could not be allocated.
static int
env_mem_init(Env *e)
{
	unsigned i;
	int r;
	Page *p = NULL;
	pde_t *from_dir;
	pde_t *to_dir;	
	pte_t *from_pt;
	pte_t *to_pt;
	unsigned long size, nr;

	// Allocate a page for the page directory
	if (!(p = page_alloc()))
		return -E_NO_MEM;

	// Now, set e->env_pgdir and initialize the page directory.
	//
	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//      (except at UVPT, which we've set below).
	//	See inc/memlayout.h for permissions and layout.
	//	Can you use kern_pgdir as a template?  Hint: Yes.
	//	(Make sure you got the permissions right in Lab 2.)
	//    - The initial VA below UTOP is empty.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: pp_ref is not maintained for most physical pages
	//	mapped above UTOP -- but you do need to increment
	//	env_pgdir's pp_ref!

	// LAB 3: Your code here.
	memset(p->data(), 0x0, PGSIZE);
	p->pp_ref++;
	e->env_pgdir = (pde_t *)(p->data());
	
	from_dir = &kern_pgdir[PDX(UTOP)];
	to_dir = &(e->env_pgdir[PDX(UTOP)]);	
	size = (UVPT - UTOP) >> PTSHIFT;
	for (; size--> 0; from_dir++, to_dir++) {
		if ( !(PTE_P & *from_dir) )
			continue;
		//allocate a page for page table.
		p = page_alloc();
		if ( p == NULL)
			return -E_NO_MEM;
		p->pp_ref++;
		// UTOP-->UVPT is readonly map for user
		*to_dir = (pde_t)(p->physaddr() | PTE_AVAIL | PTE_P | PTE_U);	
		from_pt = (pte_t *)KADDR(PTE_ADDR(*from_dir));
		to_pt = (pte_t *)KADDR(PTE_ADDR(*to_dir)); 
		for (nr = 0; nr < NPTENTRIES; nr++) {
			*to_pt = *from_pt;
			*to_pt &= ~PTE_W;
			*to_pt |= PTE_U;
			to_pt++;
			from_pt++;
		}
	}
	
	from_dir = &kern_pgdir[PDX(ULIM)];
	to_dir = &(e->env_pgdir[PDX(ULIM)]);	
	size = (~0 - ULIM) >> PTSHIFT; 
	for (; size--> 0; from_dir++, to_dir++) {
		if ( !(PTE_P & *from_dir) )
			continue;
		//allocate a page for page table.
		p = page_alloc();
		if ( p == NULL)
			return -E_NO_MEM;
		p->pp_ref++;
		// ULIM-->~0 is accessed when user swtiches to kernel mode
		//*to_dir = (pde_t)(p->physaddr() | PTE_AVAIL | PTE_P | PTE_W);	
		*to_dir = (pde_t)(p->physaddr() | PTE_AVAIL | PTE_P | PTE_W | PTE_U);	
		from_pt = (pte_t *)KADDR(PTE_ADDR(*from_dir));
		to_pt = (pte_t *)KADDR(PTE_ADDR(*to_dir)); 
		for (nr = 0; nr < NPTENTRIES; nr++) {
			*to_pt = *from_pt;
			//*to_pt |= PTE_U;
			to_pt++;
			from_pt++;
		}
	}
	
	// Recursively insert 'kern_pgdir' in itself as a page table, to form
	// a read-only virtual page table at virtual address UVPT.
	// (For now, you don't have understand the greater purpose of the
	// following two lines.)
	// UVPT permissions: kernel RO, user RO
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

	return 0;
}


// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-E_NO_FREE_ENV if all NENVS environments are allocated
//	-E_NO_MEM on memory exhaustion
int
env_alloc(Env **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	Env *e;

	if (!(e = free_envs))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_mem_init(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);
	
	// Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and 
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.

	// commit the allocation
	free_envs = e->env_next;
	*newenv_store = e;

	cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}


// Allocate len bytes of physical memory for environment env,
// and map it at virtual address va in the environment's address space.
// Does not need to zero or otherwise initialize the mapped pages in any way.
// Pages should be writable by user and kernel.
// Panic if any allocation attempt fails.
static void
segment_alloc(Env *e, uintptr_t va, size_t len)
{
	// LAB 3: Your code here.
	// (But only if you need it for load_elf.)
	//
	// Hint: It is easier to use segment_alloc if the caller can pass
	//   'va' and 'len' values that are not page-aligned.
	//   You should round va down, and round len up.
	unsigned long _va;
	struct Page *pg;
	int err;

	va = round_down(va, PGSIZE);
	len = round_up(len, PGSIZE);

	for (_va = va; _va < va + len; _va += PGSIZE) {
		pg = page_alloc();
		assert(pg != NULL);
		err = page_insert(e->env_pgdir, pg, _va, PTE_USER);
		assert(err == 0);
	}
}


// Set up the initial program binary, stack, and processor flags for the
// very first user process.  
// This function is ONLY called during kernel initialization.
//
// Loads all loadable segments from the ELF binary image into the
// environment's user memory, starting at the appropriate virtual addresses
// indicated in the ELF program header.  Also clears to zero any portions of
// these segments that are marked in the program header as being mapped but
// not actually present in the ELF file - i.e., the program's bss section.
// (This is very similar to what our boot loader does, except the boot loader
// doesn't check whether segments are loadable and it reads code from disk
// rather than a memory array like 'binary'.  So take a look at boot/main.c to
// get ideas.)
//
// Also maps one page for the program's initial stack.
//
// This function should panic on error; if the first process can't be loaded,
// no other process will work either!
static void
load_elf(Env *e, uint8_t *binary, size_t size)
{
	// Hints: 
	//  Load each program segment into virtual memory at the address
	//  specified in the segment header (struct Proghdr).
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like segment_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	// Hint:
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() below.)

	// LAB 3: Your code here.
	struct Elf *elf_hdr = (struct Elf*)binary;
	struct Proghdr *ph, *eph;
	unsigned long va;
	struct Page *pg;
	int i;

	if (elf_hdr->e_magic != ELF_MAGIC)
		panic("Invalid binary file!\n");
	
	// use env->env_pgdir temporaly, this make elf
	// segment copy to its virtual address easily.
	lcr3(PADDR(e->env_pgdir));

	ph = (struct Proghdr *)((uint8_t *)elf_hdr + elf_hdr->e_phoff);
	//eph = ph + elf_hdr->e_phnum;
	
	//for (; ph < eph; ph++) {
	for (i = 0; i < elf_hdr->e_phnum; i++, ph++) {
		if (ph->p_type != ELF_PROG_LOAD) {
			continue;
		}

		segment_alloc(e, ph->p_va, ph->p_memsz);
		memcpy((void *)ph->p_va, (void *)(((char *)elf_hdr) + ph->p_offset), ph->p_filesz);
		//memcpy((void *)ph->p_va, (void *)(elf_hdr + ph->p_offset), ph->p_filesz);
		va = ph->p_va + ph->p_filesz;
		for (; va < ph->p_va + ph->p_memsz; va++)
			*((uint8_t *) va) = 0;
	}
	lcr3(PADDR(kern_pgdir));

	//set the entry point of the program
	e->env_tf.tf_eip = elf_hdr->e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.
	// LAB 3: Your code here.
	pg = page_alloc();
	page_insert(e->env_pgdir, pg, USTACKTOP - PGSIZE, PTE_W | PTE_U);
}


// Allocates a new env and loads the named elf binary into it.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
// The new env's parent ID is set to 0.
void
env_create(uint8_t *binary, size_t size)
{
	// LAB 3: Your code here.
	Env *env;
	
	env_alloc(&env, 0);
	load_elf(env, binary, size);
}


// Free env e and all memory it uses.
void
env_free(Env *e)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;

	// Note the environment's demise.
	cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*) KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(&pages[PGNUM(pa)]);
	}

	// Before freeing the page directory, we switch to kern_pgdir if 'e'
	// is the currently active environment -- just in case 'e->env_pgdir'
	// gets reused for some other purpose before we run another
	// environment.
	if (e == curenv)
		lcr3(PADDR(kern_pgdir));
	
	// free the page directory
	pa = PADDR(e->env_pgdir);
	e->env_pgdir = 0;
	page_decref(&pages[PGNUM(pa)]);

	// return the environment to the free list
	e->env_status = ENV_FREE;
	e->env_next = free_envs;
	free_envs = e;
}


// Free environment e.
// If e was the current env, then runs a new environment (and does not return
// to the caller).
void
env_destroy(Env *e) 
{
	env_free(e);

	if (curenv == e) {
		curenv = NULL;
		sched_yield();
	}
}


// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
// This function does not return.
void env_iret(struct Trapframe *tf) __attribute__((noreturn));
void
env_iret(struct Trapframe *tf)
{
	__asm __volatile("movl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}


// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
// This function does not return.
void
env_run(Env *e)
{
	// Step 1: If this is a context switch (a new environment is running),
	//	   then set 'curenv' to the new environment,
	//	   update the new environment's 'env_runs' counter, and
	//	   and use lcr3() to switch to its address space.
	// Step 2: Use env_iret() to restore the environment's
	//         registers and drop into user mode in the
	//         environment.
	//         Hint: env_iret() loads the new environment's state from
	//	   e->env_tf.  Go back through the code you wrote above
	//	   and make sure you have set the relevant parts of
	//	   e->env_tf to sensible values.
	
	// LAB 3: Your code here.
	if ( curenv != e ) {
		// a context switch
		curenv = e;
		e->env_runs++;
		lcr3(PADDR(e->env_pgdir));
	}

	env_iret(&e->env_tf);
        panic("env_run not yet implemented");
}

