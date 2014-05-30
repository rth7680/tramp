#define PAGE_SIZE	8192
#define TRAMP_SIZE	16
#define TRAMP_RESERVE	0

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	8192\n"						\
"tramp_page:\n"							\
".rept	512\n"							\
"	ldq	$1,8192+8($27)\n"				\
"	ldq	$27,8192($27)\n"				\
"	jmp	$31,($27),0\n"					\
"	nop\n"							\
".endr\n"							\
"	.size tramp_page, 8192\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
