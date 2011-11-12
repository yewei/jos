/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_ENV_H
#define JOS_KERN_ENV_H

#include <inc/env.h>

extern Env *envs;		// All environments
extern Env *curenv;	        // Current environment

void	env_init(void);
int	env_alloc(Env **e, envid_t parent_id);
void	env_free(Env *e);
void	env_create(uint8_t *binary, size_t size);
void	env_destroy(Env *e);	// Does not return if e == curenv

int	envid2env(envid_t envid, Env **env_store, bool checkperm);
// The following function does not return.
void	env_run(Env *e) __attribute__((noreturn));

// For the grading script
#define ENV_CREATE2(start, size)	{		\
	extern uint8_t start[], size[];			\
	env_create(start, (int)size);			\
}

#define ENV_CREATE(x)			{		\
	extern uint8_t _binary_obj_##x##_start[],	\
		_binary_obj_##x##_size[];		\
	env_create(_binary_obj_##x##_start,		\
		(int)_binary_obj_##x##_size);		\
}

#endif // !JOS_KERN_ENV_H
