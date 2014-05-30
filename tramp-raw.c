#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <link.h>

#include "tramp.h"


/* Generate the tramp_page data.  The symbol is actually static, but we can't
   tell gcc that; declaring it external and hidden is almost as good.  */
asm(TRAMP_ASM_STRING);

extern const char tramp_page[PAGE_SIZE]
  __attribute__((aligned (PAGE_SIZE), visibility("hidden")));

/* ??? This and the dl_iterate_phdr callback below would not be needed if
   the kernel provided some way to duplicate an existing mapping within
   the current address space.

   Linux used to support mmap on /proc/self/mem, which would do exactly
   what we want, but that functionality was dropped in 2.3.27 series
   due to security and implementation reliability concerns.

   Perhaps a more robust solution could be found in something like

	mremap (old_addr, page_size, page_size,
		MREMAP_FIXED | MREMAP_COPY, new_addr);

   where MREMAP_COPY is a new flag that duplicates the mapping range
   instead of moving it.  Since there is no argument for the protection
   flags for the new mapping, there's no possibility of somehow forgetting
   a check and receiving a mapping with relaxed protection flags.

   In the meantime, locate the filename + offset pair at which the 
   tramp_page is located.  We don't need a lock on these since any thread
   that computes their value will yield the same value.  Use barriers and
   write to the filename last so that we know when we have a reliable pair.  */

static const char *tramp_dso_filename;
static off_t tramp_dso_offset;

/* Locate TRAMP_PAGE within one of the mapped object files.  */

#ifndef __RELOC_POINTER
# define __RELOC_POINTER(ptr, base) ((ptr) + (base))
#endif

static int
phdr_callback (struct dl_phdr_info *info, size_t size,
	       void *xdata __attribute__((unused)))
{
  const ElfW(Phdr) *phdr;
  uintptr_t ptr, load_base;
  long n;

  /* Make sure struct dl_phdr_info is at least as big as we need.  */
  if (size < offsetof (struct dl_phdr_info, dlpi_phnum)
             + sizeof (info->dlpi_phnum))
    return -1;

  phdr = info->dlpi_phdr;
  load_base = info->dlpi_addr;
  ptr = (uintptr_t) tramp_page;

  for (n = info->dlpi_phnum; --n >= 0; phdr++)
    if (phdr->p_type == PT_LOAD)
      {
        uintptr_t vaddr = __RELOC_POINTER (phdr->p_vaddr, load_base);
        if (ptr >= vaddr && ptr < vaddr + phdr->p_memsz)
          {
	    const char *filename = info->dlpi_name;
	    if (*filename == '\0')
	      filename = "/proc/self/exe";

            tramp_dso_offset = phdr->p_offset + (ptr - vaddr);
	    __sync_synchronize ();
	    tramp_dso_filename = filename;
            return 1;
          }
      }

  return 0;
}


void *
__tramp_alloc_pair (void)
{
  void *p;
  int fd;

  /* Open the dso file.  */
  /* ??? We could cache this, but run the risk of running the 
     application out of file descriptors.  */
  if (tramp_dso_filename == NULL)
    {
      if (dl_iterate_phdr (phdr_callback, NULL) <= 0)
	abort ();
    }
  fd = open (tramp_dso_filename, O_RDONLY);
  if (fd < 0)
    abort ();

  /* Allocate two pages.  */
  p = mmap (NULL, 2*PAGE_SIZE, PROT_READ|PROT_WRITE,
	    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    abort ();

  /* Overwrite the first one with a copy of TRAMP_PAGE.  */
  p = mmap (p, PAGE_SIZE, PROT_EXEC, MAP_FIXED|MAP_SHARED,
	    fd, tramp_dso_offset);
  if (p == MAP_FAILED)
    abort ();

  close (fd);
  return p;
}

void
__tramp_free_pair (void *page)
{
  if (munmap (page, 2*PAGE_SIZE) < 0)
    abort ();
}
