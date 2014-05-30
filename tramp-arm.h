#define PAGE_SIZE	4096
#define TRAMP_SIZE	8
#define TRAMP_RESERVE	0

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",%progbits\n"	\
"	.balign 4096\n"						\
"tramp_page:\n"							\
".rept 512\n"							\
"	ldr	r12, [pc, #4096-8+4]\n"				\
"	ldr	pc,  [pc, #4096-12]\n"				\
".endr\n"							\
"	.balign	4096\n"						\
"	.size tramp_page, 4096\n"				\
"	.type tramp_page, %function\n"				\
"	.popsection"
