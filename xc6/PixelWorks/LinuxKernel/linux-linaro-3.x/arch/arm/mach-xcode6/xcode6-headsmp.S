#include <linux/linkage.h>
#include <linux/init.h>
#include <mach/io.h>

@	__INIT

ENTRY(xcode6_secondary_startup)
	dsb
	cpsie i
hold:
	/* wfi */
	ldr r8, =(XC_SOC_PROC_MMREG_BASE)  @temporary as gic wakeup doesn't working now.
	ldr r7, [r8]
	ldr	r8, =0xcafebabe
	cmp r7, r8
	bne hold

	ldr r8, =(XC_SOC_PROC_MMREG_BASE)
	ldr r7, =0xbabecafe
	str r7, [r8]

	/*
	 * we've been released from the wait loop,secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
ENDPROC(xcode6_secondary_startup)

	@After CPU reset, default to be arm32 instruction set
	@Need to explicitly specify we are use arm32 instruction to avoid kernel to compile as thumb if CONFIG_KERNEL_THUMB2 is on.
    .arm

ENTRY(xcode6_secondary_trampoline)
	ldr	r0, [pc]
	bx	r0
.globl xcode6_secondary_trampoline_jump
xcode6_secondary_trampoline_jump:
	/* Space for jumping address */
	.word	/* cpu 1 */
.globl xcode6_secondary_trampoline_end
xcode6_secondary_trampoline_end:

ENDPROC(xcode6_secondary_trampoline)
