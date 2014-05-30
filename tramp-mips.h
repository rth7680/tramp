#define PAGE_SIZE	4096
#define TRAMP_SIZE	16
#define TRAMP_RESERVE	0

#if defined (_ABIN32)
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	4096\n"						\
"tramp_page:\n"							\
".rept	256\n"							\
"	.balign	16\n"						\
"	lw	$15,4096+4($25)\n"				\
"	lw	$25,4096($25)\n"				\
"	jr	$25\n"						\
"	 nop\n"							\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#elif defined (_ABI64)
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	4096\n"						\
"tramp_page:\n"							\
".rept	256\n"							\
"	.balign	16\n"						\
"	ld	$15,4096+8($25)\n"				\
"	ld	$25,4096($25)\n"				\
"	jr	$25\n"						\
"	 nop\n"							\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#else
# error "Unsupported mips abi"
#endif
