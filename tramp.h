#ifndef GCC_TRAMP_H
#define GCC_TRAMP_H 1

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#include "tramp-cpu.h"

/* In almost all cases, the trampoline is standardized to put the
   function address first and the chain value second.  */
#ifndef TRAMP_FUNCADDR_FIRST
# define TRAMP_FUNCADDR_FIRST 1
#endif

#define TRAMP_COUNT		(PAGE_SIZE / TRAMP_SIZE - TRAMP_RESERVE)

#pragma GCC visibility push(hidden)

extern void* __tramp_alloc_pair (void);
extern void __tramp_free_pair (void *page);

#pragma GCC visibility pop

extern void *__tramp_stack_alloc (uintptr_t cfa, uintptr_t, uintptr_t);
extern void __tramp_stack_free_thread (void);

extern void *__tramp_heap_alloc (uintptr_t fn, uintptr_t chain);
extern void __tramp_heap_free (void *tramp);

#endif /* GCC_TRAMP_H */
