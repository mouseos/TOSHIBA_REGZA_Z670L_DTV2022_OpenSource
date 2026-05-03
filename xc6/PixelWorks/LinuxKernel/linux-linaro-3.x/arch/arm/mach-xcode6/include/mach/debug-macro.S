#include <linux/serial_reg.h>
#include <plat/xcodeRegDef.h>
#include <asm/memory.h>
#include <mach/io.h>

		.pushsection .data
		.popsection

		.macro	addruart, rp, rv, rtmp
		ldr	\rp, =(XC_SOC_PROC_MMREG_BASE+0xf400)									@ MMU disabled
		ldr	\rv, =(XC_SOC_PROC_MMREG_BASE+0xf400)									@ MMU enabled
		.endm

		.macro	senduart,rd,rx
		str \rd, [\rx, #0]
		.endm

		.macro	busyuart,rd,rx
1001:	ldr \rd, [\rx, #0x38]
		tst \rd, #2
		beq 1001b
		.endm

		.macro	waituart,rd,rx
		.endm

