#define TRAMP_FUNCADDR_FIRST 0
#define PAGE_SIZE	4096
#define TRAMP_RESERVE	0
#ifdef __s390x__
# define TRAMP_SIZE	16
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	4096\n"						\
"tramp_page:\n"							\
".rept	256\n"							\
"	.balign	16\n"						\
"	basr	%r1,0\n"					\
"	lmg	%r0,%r1,4096-2(%r1)\n"				\
"	br	%r1\n"						\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#else
# define TRAMP_SIZE	8
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	4096\n"						\
"tramp_page:\n"							\
".rept	512\n"							\
"	.balign	8\n"						\
"	basr	%r1,0\n"					\
"	lm	%r0,%r1,4096-2(%r1)\n"				\
"	br	%r1\n"						\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#endif
