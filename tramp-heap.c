#define _GNU_SOURCE
#include <limits.h>
#include <pthread.h>

#include "tramp.h"

#define BITS_PER_INT	(CHAR_BIT * sizeof(int))
#define MASK_SIZE	((TRAMP_COUNT + BITS_PER_INT - 1) / BITS_PER_INT)

struct tramp_heap_page;

struct tramp_heap_data
{
  struct tramp_heap_page *prev, *next;
  unsigned int inuse;
  unsigned int inuse_mask[MASK_SIZE];
};

struct tramp_heap_page
{
  char code[PAGE_SIZE];
  struct tramp_heap_data data __attribute__((aligned(PAGE_SIZE)));
};

#define TRAMP_HEAP_RESERVE \
  ((sizeof (struct tramp_heap_data) + TRAMP_SIZE - 1) / TRAMP_SIZE)

#define TRAMP_HEAP_COUNT \
  (TRAMP_COUNT - TRAMP_HEAP_RESERVE)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct tramp_heap_page *cur_page;
static struct tramp_heap_page *notfull_page_list;


void *
__tramp_heap_alloc (uintptr_t fnaddr, uintptr_t chain_value)
{
  struct tramp_heap_page *page;
  unsigned int index;

  pthread_mutex_lock (&lock);

  /* Find a page with unused entries.  */
  page = cur_page;
  if (page == NULL)
    {
      page = notfull_page_list;
      if (page != NULL)
	{
	  notfull_page_list = page->data.next;
	  if (page->data.next)
	    {
	      page->data.next->data.prev = NULL;
	      page->data.next = NULL;
	    }
	}
      else
	page = __tramp_alloc_pair ();
    }

  /* Increment the use count on this page.  */
  index = page->data.inuse++;
  cur_page = (index == TRAMP_HEAP_COUNT - 1 ? NULL : page);

  /* Find a free entry in the page.  Try index first.  */
  {
    unsigned int iofs, bofs, mask, old;

    iofs = index / BITS_PER_INT;
    bofs = index % BITS_PER_INT;
    mask = 1u << bofs;
    old = page->data.inuse_mask[iofs];

    if (old & mask)
      {
	while (old != ~0u)
	  {
	    if (++iofs == MASK_SIZE)
	      iofs = 0;
	    old = page->data.inuse_mask[iofs];
	  }

	mask = ~old & -~old;
	bofs = __builtin_ctz (mask);
	index = iofs * BITS_PER_INT + bofs;
      }

    page->data.inuse_mask[iofs] = old | mask;
  }

  {
    void *tramp_code;
    uintptr_t *tramp_data;
    unsigned int index;

    tramp_code = page->code + (index + TRAMP_HEAP_RESERVE) * TRAMP_SIZE;
    tramp_data = tramp_code + PAGE_SIZE;

    tramp_data[TRAMP_FUNCADDR_FIRST ? 0 : 1] = fnaddr;
    tramp_data[TRAMP_FUNCADDR_FIRST ? 1 : 0] = chain_value;

    pthread_mutex_unlock (&lock);

    return tramp_code;
  }
}

void
__tramp_heap_free (void *tramp)
{
  struct tramp_heap_page *page;
  unsigned int index;

  page = (void *)((uintptr_t)tramp & -PAGE_SIZE);
  index = ((uintptr_t)tramp & (PAGE_SIZE - 1)) / TRAMP_SIZE;
  index -= TRAMP_HEAP_RESERVE;

  pthread_mutex_lock (&lock);

  /* Decrement the inuse counter on the page.  Shuffle the page around
     to the proper list while we're at it.  */
  {
    unsigned int inuse = --page->data.inuse;

    if (page != cur_page)
      {
	/* If the page had been full, it isn't on any lists.  */
	if (inuse == TRAMP_HEAP_COUNT - 1)
	  {
	    struct tramp_heap_page *next = notfull_page_list;
	    page->data.next = next;
	    if (next)
	      next->data.prev = page;
	    notfull_page_list = page;
	  }
	/* If the page is now empty, remove it from the notfull list.
	   Then either pop it into the cur_page slot or free it.  */
	else if (inuse == 0)
	  {
	    struct tramp_heap_page *next, *prev;
	    next = page->data.next;
	    prev = page->data.prev;
	    if (next)
	      next->data.prev = prev;
	    if (prev)
	      prev->data.next = next;
	    page->data.next = page->data.prev = NULL;

	    if (cur_page == NULL)
	      cur_page = page;
	    else
	      {
		__tramp_free_pair (page);
		goto egress;
	      }
	  }
      }
  }

  /* Clear the inuse bit in the mask.  */
  {
    unsigned int iofs, bofs, mask;

    iofs = index / BITS_PER_INT;
    bofs = index % BITS_PER_INT;
    mask = 1u << bofs;

    page->data.inuse_mask[iofs] &= ~mask;
  }

 egress:
  pthread_mutex_unlock (&lock);
}
