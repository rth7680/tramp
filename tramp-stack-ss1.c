#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>

#include "tramp.h"


/* The "log" is a record of the actions we have performed within the
   current thread.  These actions include allocating trampolines on
   behalf of a given stack frame, allocating trampoline page pairs,
   and allocating more log space.  It is organized this way in order
   to minimize memory allocation overhead.

   Entries in the log are pairs of uintptr_t.  The first entry of the
   pair indicates the type of action, and the second is some sort of
   data associated with that action:

   0	Allocated a new log page.  The data entry is the pointer to
	the previous log page.  This will only ever appear as the
	first entry of a log page.

   1	Allocated a new tramp page pair.  The data entry is the pointer
	to the previous pair.

   CFA	Allocated trampoine entries on behalf of the function instance
	identified by its Canonical Frame Address.  The data entry is
	the number of trampolines allocated.  This number will never be
	more than the number of trampolines remaining in the current
	tramp page pair.  If the function requires more trampolines,
	we'll use additional log entries.
*/

#define LOG_NEW_LOG	0
#define LOG_NEW_PAGE	1
#define LOG_SIZE	(PAGE_SIZE / sizeof(uintptr_t))

/* Allocations within a given context.  */
struct tramp_alloc_state
{
  /* The current page from which we are allocating trampolines.  */
  void *cur_page;

  /* The current page from which we are logging actions.  */
  uintptr_t *cur_log;

  /* The number of trampolines allocated from the current page.  */
  unsigned int cur_page_inuse;

  /* The number of log entries in use in the current page.  */
  unsigned int cur_log_inuse;
};

/* All thread-local variables.  */
struct tramp_globals
{
  /* The allocations from the thread stack.  */
  struct tramp_alloc_state thread_state;

  /* The allocations from the signal stack.  */
  struct tramp_alloc_state signal_state;

  /* The "current" state for subsequent allocations from this function.  */
  struct tramp_alloc_state *cur_state;
  uintptr_t cur_cfa;

  /* Previously allocated pages not yet released to the system.  */
  void *save_page;
  uintptr_t *save_log;
};

static __thread struct tramp_globals tramp_G = {
  .thread_state.cur_page_inuse = TRAMP_COUNT,
  .signal_state.cur_page_inuse = TRAMP_COUNT,
  .thread_state.cur_log_inuse = LOG_SIZE,
  .signal_state.cur_log_inuse = LOG_SIZE,
};

/* Allocate and free one trampoline page pair, using a thread-local cache.  */

static inline void *
alloc_one_tramp_page (struct tramp_globals *G)
{
  void *ret = G->save_page;
  if (ret == 0)
    ret = __tramp_alloc_pair ();
  else
    G->save_page = 0;
  return ret;
}

static inline void
free_one_tramp_page (struct tramp_globals *G, void *page)
{
  void *old = G->save_page;
  G->save_page = page;
  if (old)
    __tramp_free_pair (old);
}

/* Allocate and free one log page, using a thread-local cache.  */

static inline uintptr_t *
alloc_one_log_page (struct tramp_globals *G)
{
  void *ret = G->save_log;
  if (ret == 0)
    {
      ret = mmap (NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
		  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      if (ret == MAP_FAILED)
	abort ();
    }
  else
    G->save_log = 0;
  return ret;
}

static inline void
free_one_log_page_raw (uintptr_t *log)
{
  if (munmap (log, PAGE_SIZE) < 0)
    abort ();
}

static inline void
free_one_log_page (struct tramp_globals *G, uintptr_t *log)
{
  uintptr_t *old = G->save_log;
  G->save_log = log;
  if (old)
    free_one_log_page_raw (old);
}

/* Return true if CFA A is older than CFA B on the stack.  */
/* ??? Under normal conditions this can be a simple pointer comparison
   based on whether or not the stack grows down or up.  If split stacks
   are in use, this ought to involve some sort of data structure search
   to which we have no access given the current API.  */
static inline _Bool
cfa_older_p (uintptr_t a, uintptr_t b)
{
  /* ??? Assume stack grows down.  */
  return a > b;
}

/* Replay the log until we get back to an entry older than CFA.
   Note that -1 can be used in order to reply the entire log.  */
/* ??? Except that -1 assumes stack grows down; 0 would be the
   stack grows up magic value.  */

static void
replay_log (struct tramp_globals *G, struct tramp_alloc_state *A,
	    uintptr_t cfa)
{
  uintptr_t *log = A->cur_log;
  unsigned int inuse = A->cur_log_inuse;

  while (inuse > 0)
    {
      uintptr_t action = log[inuse - 2];
      uintptr_t data = log[inuse - 1];

      if (action == LOG_NEW_LOG)
	{
	  free_one_log_page (G, log);
	  log = (uintptr_t *) data;
	  inuse = LOG_SIZE;
	}
      else if (action == LOG_NEW_PAGE)
	{
	  assert (A->cur_page_inuse == TRAMP_RESERVE);
	  free_one_tramp_page (G, A->cur_page);
	  A->cur_page = (void *) data;
	  A->cur_page_inuse = TRAMP_COUNT;
	}
      else if (cfa_older_p (action, cfa))
	break;
      else
	{
	  assert (A->cur_page_inuse >= data);
	  A->cur_page_inuse -= data;
	}

      inuse -= 2;
    }

  A->cur_log = log;
  A->cur_log_inuse = inuse;
}


/* Add a log entry.  */

static void
add_log (struct tramp_globals *G, struct tramp_alloc_state *A,
	 uintptr_t action, uintptr_t data)
{
  uintptr_t *log = A->cur_log;
  unsigned int inuse = A->cur_log_inuse;

  if (inuse == LOG_SIZE)
    {
      uintptr_t *new_log = alloc_one_log_page (G);
      inuse = 0;
      if (log != NULL)
	{
	  new_log[0] = LOG_NEW_LOG;
	  new_log[1] = (uintptr_t) log;
	  A->cur_log = new_log;
	  inuse = 2;
	}
      log = new_log;
    }

  log[inuse++] = action;
  log[inuse++] = data;
  A->cur_log_inuse = inuse;
}


/* Set CFA only for the first allocation in the function; subsequent
   allocations use CFA=0.  */

void *
__tramp_stack_alloc (uintptr_t cfa, uintptr_t fnaddr, uintptr_t chain_value)
{
  struct tramp_globals *G = &tramp_G;
  struct tramp_alloc_state *A = G->cur_state;

  if (cfa)
    {
      stack_t ss;

      /* ??? Use glibc tcb entries in order to quickly determine that the cfa
	 is indeed within the current thread's stack.  When this test fails,
	 there are two possibilities: (1) we're on the signal stack, (2) we're
	 using split stacks (which needs additional help in order to determine
	 whether the current frame is inner or outer of the previous), or
	 (3) the user is doing something odd with the stacks.  */

      if (sigaltstack (NULL, &ss) < 0)
	abort ();
      if (ss.ss_flags == SS_ONSTACK)
	{
	  /* We're running on the signal stack.  Should we check to see that
	     it's the same signal stack we may have been using before?  */
	  A = &G->signal_state;
	}
      else
	{
	  /* We're not on the signal stack, but we had been.  */
	  if (G->signal_state.cur_log_inuse)
	    replay_log (G, &G->signal_state, -1);
	  A = &G->thread_state;
	}
      replay_log (G, A, cfa);

      G->cur_state = A;
      G->cur_cfa = cfa;
    }

  /* If needed, allocate a new tramp page pair.  */
  if (A->cur_page_inuse == TRAMP_COUNT)
    {
      add_log (G, A, LOG_NEW_PAGE, (uintptr_t) A->cur_page);
      A->cur_page = alloc_one_tramp_page (G);
      A->cur_page_inuse = TRAMP_RESERVE;

      /* Force a new log entry for the current function frame.  */
      cfa = G->cur_cfa;
    }

  /* Add, or update, the log entry for the number of trampolines
     allocated by the current function frame.  */
  if (cfa)
    add_log (G, A, cfa, 1);
  else
    {
      assert (A->cur_log[A->cur_log_inuse - 2] == G->cur_cfa);
      A->cur_log[A->cur_log_inuse - 1] += 1;
    }

  {
    void *tramp_code, *page;
    uintptr_t *tramp_data;
    unsigned int index;

    page = A->cur_page;
    index = A->cur_page_inuse++;

    tramp_code = page + index * TRAMP_SIZE;
    tramp_data = tramp_code + PAGE_SIZE;

    tramp_data[TRAMP_FUNCADDR_FIRST ? 0 : 1] = fnaddr;
    tramp_data[TRAMP_FUNCADDR_FIRST ? 1 : 0] = chain_value;

    return tramp_code;
  }
}

void /* __attribute__((thread_destructor)) */
__tramp_stack_free_thread (void)
{
  struct tramp_globals *G = &tramp_G;

  if (G->signal_state.cur_log)
    {
      replay_log (G, &G->signal_state, -1);
      free_one_log_page (G, G->signal_state.cur_log);
    }
  if (G->thread_state.cur_log)
    {
      replay_log (G, &G->thread_state, -1);
      free_one_log_page (G, G->thread_state.cur_log);
    }
  if (G->save_page)
    __tramp_free_pair (G->save_page);
  if (G->save_log)
    free_one_log_page_raw (G->save_log);
}
