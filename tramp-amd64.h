#define PAGE_SIZE		4096
#define TRAMP_SIZE		16
#define TRAMP_RESERVE		0

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	4096\n"						\
"tramp_page:\n"							\
".rept	256\n"							\
"	.balign	16\n"						\
"1:	movq	1b+4096+8(%rip), %r10\n"			\
"	jmpq	*1b+4096(%rip)\n"				\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
