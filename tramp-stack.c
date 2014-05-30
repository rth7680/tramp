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

   2	Entered the signal stack.  The data entry is ignored.

   CFA	Allocated trampoine entries on behalf of the function instance
	identified by its Canonical Frame Address.  The data entry is
	the number of trampolines allocated.  This number will never be
	more than the number of trampolines remaining in the current
	tramp page pair.  If the function requires more trampolines,
	we'll use additional log entries.
*/

#define LOG_NEW_LOG	0
#define LOG_NEW_PAGE	1
#define LOG_SIGSTACK	2
#define LOG_SIZE	(PAGE_SIZE / sizeof(uintptr_t))

/* All thread-local variables.  */
struct tramp_globals
{
  /* The current page from which we are allocating trampolines.  */
  void *cur_page;

  /* The current page from which we are logging actions.  */
  uintptr_t *cur_log;

  /* The number of trampolines allocated from the current page.  */
  unsigned int cur_page_inuse;

  /* The number of log entries in use in the current page.  */
  unsigned int cur_log_inuse;

  /* The "current" cfa for subsequent allocations from this function.  */
  uintptr_t cur_cfa;

  /* The active signal stack, assuming SS_ONSTACK is set.  */
  stack_t cur_sigstack;

  /* Previously allocated pages not yet released to the system.  */
  void *save_page;
  uintptr_t *save_log;
};

static __thread struct tramp_globals tramp_G = {
  .cur_page_inuse = TRAMP_COUNT,
};

static inline struct tramp_globals *
get_globals (void)
{
  struct tramp_globals *G = &tramp_G;

  /* ??? When using global-dynamic (or emulated) tls, avoid calling 
     into the runtime more than once.  */
#ifdef __PIC__
  asm("" : "+r"(G));
#endif

  return G;
}

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
static inline bool
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
replay_log (struct tramp_globals *G, uintptr_t cfa, bool exit_sigstack)
{
  uintptr_t *log = G->cur_log;
  unsigned int inuse = G->cur_log_inuse;

  while (inuse > 0)
    {
      uintptr_t action = log[inuse - 2];
      uintptr_t data = log[inuse - 1];

      switch (action)
	{
	case LOG_NEW_LOG:
	  free_one_log_page (G, log);
	  log = (uintptr_t *) data;
	  inuse = LOG_SIZE;
	  break;

	case LOG_NEW_PAGE:
	  assert (G->cur_page_inuse == TRAMP_RESERVE);
	  free_one_tramp_page (G, G->cur_page);
	  G->cur_page = (void *) data;
	  G->cur_page_inuse = TRAMP_COUNT;
	  break;

	case LOG_SIGSTACK:
	  if (!exit_sigstack)
	    goto egress;
	  exit_sigstack = false;
	  break;

	default:
	  if (!exit_sigstack && cfa_older_p (action, cfa))
	    goto egress;
	  assert (G->cur_page_inuse >= data);
	  G->cur_page_inuse -= data;
	  break;
	}

      inuse -= 2;
    }

 egress:
  G->cur_log = log;
  G->cur_log_inuse = inuse;
}


/* Add a log entry.  */

static void
add_log (struct tramp_globals *G, uintptr_t action, uintptr_t data)
{
  uintptr_t *log = G->cur_log;
  unsigned int inuse = G->cur_log_inuse;

  if (log == NULL || inuse == LOG_SIZE)
    {
      uintptr_t *new_log = alloc_one_log_page (G);
      new_log[0] = LOG_NEW_LOG;
      new_log[1] = (uintptr_t) log;
      inuse = 2;
      G->cur_log = log = new_log;
    }

  log[inuse++] = action;
  log[inuse++] = data;
  G->cur_log_inuse = inuse;
}


/* Set CFA only for the first allocation in the function; subsequent
   allocations use CFA=0.  */

void *
__tramp_stack_alloc (uintptr_t cfa, uintptr_t fnaddr, uintptr_t chain_value)
{
  struct tramp_globals *G = get_globals ();
  sigset_t old_set, full_set;

  sigfillset (&full_set);
  pthread_sigmask (SIG_SETMASK, &full_set, &old_set);

  if (cfa)
    {
      stack_t ss;

      /* ??? Use glibc tcb entries in order to quickly determine that the cfa
	 is indeed within the current thread's stack.  When this test fails,
	 there are two possibilities: (1) we're on the signal stack, (2) we're
	 using split stacks (which needs additional help in order to determine
	 whether the current frame is inner or outer of the previous), or
	 (3) the user is doing something odd with the stacks.  */

      sigaltstack (NULL, &ss);
      if (__builtin_expect (ss.ss_flags == SS_ONSTACK, 0))
	{
	  if (G->cur_sigstack.ss_flags == SS_ONSTACK)
	    {
	      uintptr_t replay_cfa = cfa;

	      /* We are still running on the signal stack.  Double-check
		 that it's the same stack, Just In Case.  */
	      if (ss.ss_sp != G->cur_sigstack.ss_sp
		  || ss.ss_size != G->cur_sigstack.ss_size)
		{
		  G->cur_sigstack = ss;
		  replay_cfa = -1;
		}

	      replay_log (G, replay_cfa, false);
	    }
	  else
	    {
	      G->cur_sigstack = ss;
	      add_log (G, LOG_SIGSTACK, 0);
	    }
	}
      else
	{
	  bool exit_sigstack = false;

	  if (G->cur_sigstack.ss_flags == SS_ONSTACK)
	    {
	      G->cur_sigstack.ss_flags = 0;
	      exit_sigstack = true;
	    }

	  replay_log (G, cfa, exit_sigstack);
	}

      G->cur_cfa = cfa;
    }

  /* If needed, allocate a new tramp page pair.  */
  if (G->cur_page_inuse == TRAMP_COUNT)
    {
      add_log (G, LOG_NEW_PAGE, (uintptr_t) G->cur_page);
      G->cur_page = alloc_one_tramp_page (G);
      G->cur_page_inuse = TRAMP_RESERVE;

      /* Force a new log entry for the current function frame.  */
      cfa = G->cur_cfa;
    }

  /* Add, or update, the log entry for the number of trampolines
     allocated by the current function frame.  */
  if (cfa)
    add_log (G, cfa, 1);
  else
    {
      assert (G->cur_log[G->cur_log_inuse - 2] == G->cur_cfa);
      G->cur_log[G->cur_log_inuse - 1] += 1;
    }

  {
    void *tramp_code, *page;
    uintptr_t *tramp_data;
    unsigned int index;

    page = G->cur_page;
    index = G->cur_page_inuse++;

    tramp_code = page + index * TRAMP_SIZE;
    tramp_data = tramp_code + PAGE_SIZE;

    tramp_data[TRAMP_FUNCADDR_FIRST ? 0 : 1] = fnaddr;
    tramp_data[TRAMP_FUNCADDR_FIRST ? 1 : 0] = chain_value;

    pthread_sigmask (SIG_SETMASK, &old_set, NULL);

    return tramp_code;
  }
}

void /* __attribute__((thread_destructor)) */
__tramp_stack_free_thread (void)
{
  struct tramp_globals *G = get_globals ();

  if (G->cur_log)
    replay_log (G, -1, true);
  if (G->save_log)
    free_one_log_page_raw (G->save_log);
  if (G->save_page)
    __tramp_free_pair (G->save_page);
}
