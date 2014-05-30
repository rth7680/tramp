#define PAGE_SIZE	4096
#define TRAMP_SIZE	8
#define TRAMP_RESERVE	2

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign 4096\n"						\
"tramp_page:\n"							\
"	movl	$4096-5, %edx\n"				\
"	addl	(%esp), %edx\n"					\
"	movl	4(%edx), %ecx\n"				\
"	movl	(%edx), %edx\n"					\
"	ret\n"							\
".rept 510\n"							\
"	.balign	8\n"						\
"	call	tramp_page\n"					\
"	jmpl	*%edx\n"					\
".endr\n"							\
"	.balign	4096\n"						\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, @function\n"				\
"	.popsection"
