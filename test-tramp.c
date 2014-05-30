void * __tramp_stack_alloc (void *cfa, void *fnaddr, void *chain_value);

extern char bounce[];
asm("bounce: movq %r10, %rax; ret");

int main()
{
  void *t = __tramp_stack_alloc (__builtin_dwarf_cfa (), bounce,
				 (void *)0x1122334455667788);
  long (*tf)(void) = (long (*)(void)) t;

  long l = tf();
  return l != 0x1122334455667788;
}
