#include <stdint.h>

void * __tramp_stack_alloc (void *cfa, void *fnaddr, void *chain_value);

extern char bounce[];

#if defined(__x86_64__)
asm("bounce: movq %r10, %rax; ret");
#elif defined(__aarch64__)
asm("bounce: mov x0, x18; ret");
#elif defined(__arm__)
asm("bounce: mov r0, r12; mov pc, lr");
#else
# error unsupported
#endif

int main()
{
  intptr_t test = (intptr_t)0x1122334455667788ULL;
  void *t = __tramp_stack_alloc (__builtin_dwarf_cfa (), bounce, (void *)test);
  intptr_t (*tf)(void) = (intptr_t (*)(void)) t;

  intptr_t l = tf();
  return l != test;
}
