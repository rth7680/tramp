#define PAGE_SIZE	4096
#define TRAMP_SIZE	8
#define TRAMP_RESERVE	3

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign 4096\n"						\
"tramp_page:\n"							\
"	mflr	11\n"						\
"	mtlr	0\n"						\
"	lwz	0,4096-8(11)\n"					\
"	lwz	11,4096-4(11)\n"				\
"	mtctr	0\n"						\
"	bctr\n"							\
".rept 509\n"							\
"	mflr	0\n"						\
"	bcl	20,31,tramp_page\n"				\
".endr\n"							\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
