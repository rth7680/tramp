void * __tramp_stack_alloc (void *cfa, void *fnaddr, void *chain_value);

extern char bounce[];

#if defined(__x86_64__)
asm("bounce: movq %r10, %rax; ret");
#elif defined(__aarch64__)
asm("bounce: mov x0, x18; ret");
#else
# error unsupported
#endif

int main()
{
  void *t = __tramp_stack_alloc (__builtin_dwarf_cfa (), bounce,
				 (void *)0x1122334455667788);
  long (*tf)(void) = (long (*)(void)) t;

  long l = tf();
  return l != 0x1122334455667788;
}
