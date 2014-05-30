#ifdef defined(__arch64__)
# define PAGE_SIZE	8192
# define TRAMP_SIZE	16
# define TRAMP_RESERVE	0
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	8192\n"						\
"tramp_page:\n"							\
".rept	512\n"							\
"	rd	%pc, %g1\n"					\
"	ldx	[%g1+8192], %g5\n"				\
"	jmp	%g5\n"						\
"	 ldx	[%g1+8192+8], %g5\n"				\
".endr\n"							\
"	.size tramp_page, 8192\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#else
# define PAGE_SIZE	4096
# define TRAMP_SIZE	12
# define TRAMP_RESERVE	2
# define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign 4096\n"						\
"tramp_page:\n"							\
"	mov	%o7, %g1\n"					\
"	mov	%g2, %o7\n"					\
"	ld	[%g1+4096-12+4], %g2\n"				\
"	ld	[%g1+4096-12], %g1\n"				\
"	jmp	%g1\n"						\
"	 nop\n"							\
".rept 339\n"							\
"	or	%o7, %g0, %g2\n"				\
"	call	tramp_page, 0\n"				\
"	 nop\n"							\
".endr\n"							\
"	.balign 4096\n"						\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
#endif
