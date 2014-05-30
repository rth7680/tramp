#define PAGE_SIZE		65536
#define TRAMP_SIZE		16
#define TRAMP_RESERVE		0

#define TRAMP_ASM_STRING					\
"	.pushsection .text.tramp_page,\"ax\",@progbits\n"	\
"	.balign	65536\n"					\
"tramp_page:\n"							\
".rept	4096\n"							\
"	.balign	16\n"						\
"1:	ldr	x17, 1b+0x10000\n"				\
"	ldr	x18, 1b+0x10008\n"				\
"	br	x17\n"						\
".endr\n"							\
"	.size tramp_page, 65536\n"				\
"	.type tramp_page, %function\n"				\
"	.popsection"
