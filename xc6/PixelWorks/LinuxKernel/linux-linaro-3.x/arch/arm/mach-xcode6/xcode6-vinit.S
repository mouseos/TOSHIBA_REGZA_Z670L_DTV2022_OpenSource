#include <linux/linkage.h>
#include <mach/io.h>

	.section    ".jump.text","ax"

	@After CPU reset, default to be arm32 instruction set
	@Need to explicitly specify we are use arm32 instruction to avoid kernel to compile as thumb if CONFIG_KERNEL_THUMB2 is on.
	.arm

	.macro inituart divsor, tmp1, tmp2, tmp3
	ldr	\tmp1, =(XC_SOC_PROC_MMREG_BASE+0xfc7c)	@GPIO_H_CTRL
	ldr	\tmp2, [\tmp1]
	bic	\tmp2, \tmp2, #(3<<30)
	str \tmp2, [\tmp1]
	
	ldr	\tmp1, =(XC_SOC_PROC_MMREG_BASE+0x04a8)	@RBM_PADU_CTRL
	ldr	\tmp2, [\tmp1]
	bic \tmp2, \tmp2, #3
	bic \tmp2, \tmp2, #0x70
	str \tmp2, [\tmp1]

	ldr	\tmp1, =(XC_SOC_PROC_MMREG_BASE+0x16000)	@ACC_RESET_REG0
	ldr	\tmp2, [\tmp1]
	orr \tmp2, \tmp2, #(1<<16)
	str \tmp2, [\tmp1]
	bic \tmp2, \tmp2, #(1<<16)
	str \tmp2, [\tmp1]
	
	ldr \tmp1, =(XC_SOC_PROC_MMREG_BASE+0xf400)
	ldr	\tmp2, [\tmp1, #0x18] @UART0_MODEL_CTRL
	bic	\tmp2, \tmp2, #(3<<2)
	str \tmp2, [\tmp1, #0x18]

	ldr \tmp2, [\tmp1, #0x1c] @UART0_CTRL_REG
	bic \tmp2, \tmp2, #1
	str \tmp2, [\tmp1, #0x1c]
	
	ldr \tmp2, [\tmp1, #0x0c] @UART0_LINE_CTRL
	orr \tmp2, \tmp2, #(1<<7)
	str	\tmp2, [\tmp1, #0x0c]
	
	lsr \tmp2, \divsor, #4
	and \tmp2, \tmp2, #0xff
	str \tmp2, [\tmp1, #0x00]  @UART0_RX_TX_DIVLSB
	
	lsr \tmp2, \divsor, #12
	and \tmp2, \tmp2, #0xf
	str \tmp2, [\tmp1, #0x04] @UART0_INTEN_DIVMSB

	ldr \tmp2, [\tmp1, #0x0c] @UART0_LINE_CTRL
	bic \tmp2, \tmp2, #(1<<7)
	bic \tmp2, \tmp2, #(0xf<<16)	

	and \tmp3, \divsor, #0xf
	orr \tmp2, \tmp2, \tmp3, lsl #16
	str \tmp2, [\tmp1, #0x0c]

	ldr \tmp2, [\tmp1, #0x1c] @UART_CTRL_REG
	orr \tmp2, \tmp2, #(3<<8)
	str \tmp2, [\tmp1, #0x1c]

	ldr \tmp2, [\tmp1, #0x08] @UART_INTID_FIFOCTRL
	bic \tmp2, \tmp2, #(3<<6)
	str \tmp2, [\tmp1, #0x08]

	ldr \tmp2, [\tmp1, #0x0c] @UART0_LINE_CTRL
	orr \tmp2, \tmp2, #(3<<0)
	bic \tmp2, \tmp2, #(7<<2)
	str \tmp2, [\tmp1, #0x0c]

	ldr \tmp2, [\tmp1, #0x1c] @UART_CTRL_REG
	bic \tmp2, \tmp2, #(1<<4)
	str \tmp2, [\tmp1, #0x1c]
	.endm

        .macro disable_l2cc tmp1, tmp2
        ldr \tmp1, =0xfff02000
        mov \tmp2, #0
        str \tmp2, [\tmp1, #0x100]
        .endm

        .macro disable_mmu_caches tmp1
        mrc p15, 0, \tmp1, c1, c0, 0 @Disable MMU, I/D cache
        bic \tmp1, \tmp1, #(1<<12)
        bic \tmp1, \tmp1, #(1<<11)
        bic \tmp1, \tmp1, #(1<<2)
        bic \tmp1, \tmp1, #1
        mcr p15, 0, \tmp1, c1, c0, 0
        .endm

        .macro inv_l2_cache tmp1, tmp2, tmp3
        ldr \tmp1, =0xfff02000      @Invalidate all
        mov \tmp2, #1
        lsl \tmp2, \tmp2, #16
        sub \tmp2, \tmp2, #1
        str \tmp2, [\tmp1, #0x77c]
20001:
        ldr \tmp3, [\tmp1, #0x77c]  @cache_wait_way()
        cmp \tmp2, \tmp3
        bne 20001b

        mov \tmp2, #0               @cache_sync()
        str \tmp2, [\tmp1, #0x730]
        .endm

        .macro inv_l1_cache tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, tmp10
        @Invalidate L1 Instruction Cache, also flushes BTAC

        mov    \tmp1, #0                      @ sbz
        mcr    p15, 0, \tmp1, c7, c5, 0       @ iciallu - invalidate entire i cache, and flushes branch target cache

        @ invalidate data/unified caches

        mrc     p15, 1, \tmp1, c0, c0, 1      @ read clidr
        ands    \tmp4, \tmp1, #0x07000000        @ extract coherency level
        mov     \tmp4, \tmp4, lsr #23            @ total cache levels << 1
        beq     10005f                   @ if 0, no need to clean

        mov     \tmp9, #0                    @ \tmp9 holds current cache level << 1
10001:
        add     \tmp3, \tmp9, \tmp9, lsr #1       @ \tmp3 holds cache "set" position
        mov     \tmp2, \tmp1, lsr \tmp3             @ bottom 3 bits are the cache-type for this level
        and     \tmp2, \tmp2, #7                 @ isolate those lower 3 bits
        cmp     \tmp2, #2
        blt     10004f                       @ no cache or only instruction cache at this level

        mcr     p15, 2, \tmp9, c0, c0, 0     @ write the cache size selection register
        isb                                @ isb to sync the change to the cachesizeid reg
        mrc     p15, 1, \tmp2, c0, c0, 0      @ reads current cache size id register
        and     \tmp3, \tmp2, #7                 @ extract the line length field
        add     \tmp3, \tmp3, #4                 @ add 4 for the line length offset (log2 16 bytes)
        ldr     \tmp5, =0x3ff
        ands    \tmp5, \tmp5, \tmp2, lsr #3         @ \tmp5 is the max number on the way size (right aligned)
        clz     \tmp6, \tmp5                     @ \tmp6 is the bit position of the way size increment
        ldr     \tmp7, =0x7fff
        ands    \tmp7, \tmp7, \tmp2, lsr #13        @ \tmp7 is the max number of the index size (right aligned)

10002:
       mov     \tmp8, \tmp5                     @ \tmp8 working copy of the max way size (right aligned)

10003:
       ARM(orr     \tmp10, \tmp9, \tmp8, lsl \tmp6)       @ factor in the way number and cache number into \tmp10
       THUMB(mov    \tmp10, \tmp8, lsl \tmp6)
       THUMB(orr    \tmp10, \tmp9, \tmp10)
       ARM(orr     \tmp10, \tmp10, \tmp7, lsl \tmp3)       @ factor in the set number
       THUMB(mov    \tmp2, \tmp7, lsl \tmp3)
       THUMB(orr    \tmp10, \tmp10, \tmp2)
       mcr     p15, 0, \tmp10, c7, c6, 2     @ invalidate by set/way
       subs    \tmp8, \tmp8, #1                 @ decrement the way number
       bge     10003b
       subs    \tmp7, \tmp7, #1                 @ decrement the set number
       bge     10002b
10004:
       add     \tmp9, \tmp9, #2               @ increment the cache number
       cmp     \tmp4, \tmp9
       bgt     10001b

10005:
        .endm

        .macro enable_scu tmp1, tmp2
        mrc p15, 4, \tmp1, c15, c0, 0
        ldr \tmp2, [\tmp1, #0]
        orr \tmp2, \tmp2, #1
        str \tmp2, [\tmp1, #0]
        .endm

        .macro join_smp arg1, arg2, tmp1
        and \arg1, \arg1, #0x3
        mov \arg1, \arg1, lsl #2
        and \arg2, \arg2, #0xf
        mov \arg2, \arg2, lsl \arg1
        mrc p15, 4, \tmp1, c15, c0, 0
        str \arg2, [\tmp1, #0xc]

        mrc p15, 0, \arg1, c1, c0, 1 @Join SMP
        orr \arg1, \arg1, #0x40
        mcr p15, 0, \arg1, c1, c0, 1

        mrc p15, 0, \arg1, c1, c0, 1 @enableMaintenanceBroadcast
        mov \arg2, \arg1
        orr \arg1, \arg1, #0x1
        cmp \arg1, \arg2
        mcrne p15, 0, \arg1, c1, c0, 1
        .endm

        .macro enable_gic_proc_interface tmp1, tmp2
        mrc p15, 4, \tmp1, c15, c0, 0 @enableGICProcessorInterface

        ldr \tmp2, [\tmp1, #0x100]
        orr \tmp2, \tmp2, #0x3
        str \tmp2, [\tmp1, #0x100]
        .endm

        .macro gic_init tmp1, tmp2
        mrc p15, 4, \tmp1, c15, c0, 0 @enableGIC
        add \tmp1, \tmp1, #0x1000

        ldr \tmp2, [\tmp1]
        orr \tmp2, \tmp2, #0x1
        str \tmp2, [\tmp1]

        enable_gic_proc_interface \tmp1, \tmp2
        .endm

        .macro set_priority_mask arg1, tmp1
        mrc p15, 4, \tmp1, c15, c0, 0
        str \arg1, [\tmp1, #0x0104]
        .endm

        .macro enable_int_id arg1, tmp1, tmp2, tmp3
        mov \tmp1, \arg1
        mrc p15, 4, \arg1, c15, c0, 0
        mov \tmp2, \tmp1
        mov \tmp2, \tmp2, lsr #5
        mov \tmp2, \tmp2, lsl #2
        and \tmp1, \tmp1, #0x1f
        mov \tmp3, #1
        mov \tmp3, \tmp3, lsl \tmp1
        and \tmp2, \tmp2, #0x1100
        str \tmp3, [\arg1, \tmp2]
        .endm

        .macro set_int_priority arg1, arg2, tmp1, tmp2, tmp3
        mov \tmp1, \arg1
        mrc p15, 4, \arg1, c15, c0, 0
        and \arg2, \arg2, #0x1f
        mov \arg2, \arg2, lsl #3
        bic \tmp2, \tmp1, #0x3
        add \tmp2, \tmp2, #0x1400
        add \arg1, \arg1, \tmp2
        and \tmp1, \tmp1, #0x3
        mov \tmp1, \tmp1, lsl #3
        mov \tmp3, #0xff
        mov \tmp3, \tmp3, lsl \tmp1
        mov \arg2, \arg2, lsl \tmp1
        ldr \tmp2, [\arg1]
        bic \tmp2, \tmp2, \tmp3
        orr \tmp2, \tmp2, \arg2
        str \tmp2, [\arg1]
        .endm

_start:
	b reset
	b undefined
	b svc
	b prefetch
	b abort
	b deadcode
	b irq
	b fiq

reset:
    mrc p15, 0, r3, c0, c0, 5 @ We stop secondary CPU here
    ands r3, r3, #0x3
    bne secondary_reset

	ldr r1, =3506

	/* Store parameter from bootloader */
	adr r3, atag
	str r0, [r3]
	str r1, [r3, #4]
	str r2, [r3, #8]

	/* Let see all 1G memory */
	ldr r0, =0xffff0000
	ldr r1, =(XC_SOC_PROC_MMREG_BASE+0xb404)
	str r0, [r1]

	@ Init UART
	adr	r0, divisor
	ldmia	r0, {r0}
	inituart r0, r1, r2, r3
	
	dsb		@ Make sure the VINIT address write finished before turning on CPU 1
		@ check PROC5_ARM_VINITHI_CONFIG
		@ if the value less than 0x20000000, 
		@   it is old bootloader(before r2200)
		@ otherwise
		@   u-boot will setup the kernel start
		ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xb630)
		ldr r1, [r0]
		cmn	r1, #0xe0000001		@0x20000000
		bhi reset_core1
		mov r1, #CONFIG_PHYS_OFFSET
		str r1, [r0]

reset_core1:
        @ Stop clock for cpu1
        ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xb608)
        mov r1, #0x2
        str r1, [r0]

        @ Release cpu1 out of reset
        ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xb60c)
        mov r1, #~0
        str r1, [r0]

        @ Enable clock for cpu1
        ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xb608)
        mov r1, #0x0
        str r1, [r0]

secondary_reset:

	disable_l2cc r0, r1
	disable_mmu_caches r0
	
    @Invalidate L1 Instruction Cache, also flushes BTAC

    mov    r0, #0                      @ sbz
    mcr    p15, 0, r0, c7, c5, 0       @ iciallu - invalidate entire i cache, and flushes branch target cache

	inv_l1_cache r0, r1, r2, r3, r4, r5, r7, r9, r10, r11

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 6      @ bpiall - invalidate entire branch predictor array

	@ Load in data
	adr	r0, data
	ldmia	r0, {r1 - r4}

#ifdef CONFIG_DEBUG_LL
	@Output a marker to UART
	ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xf400)
	mov r1, #'*'
	str r1, [r0]
#endif

	@ Restore parameter from bootloader
	adr r3, atag
	ldr r0, [r3]
	ldr r1, [r3, #4]
	ldr r2, [r3, #8]

	@ Jump to kernel start
	ldr r6, =(XC_SOC_PROC_MMREG_BASE+0xb630)
	ldr r5, [r6]
	add r5, r5, #TEXT_OFFSET
	mov pc, r5


@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ Interrupt handlers
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

undefined:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #1
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #1
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever
	
svc:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #2
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #2
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

prefetch:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #3
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #3
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

abort:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #4
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #4
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

deadcode:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #5
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #5
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

irq:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #6
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #6
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

fiq:
	adr	r0, data
	ldmia	r0, {r1 - r4}

	add r1, r1, #7
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #7
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

loop_forever:
	b loop_forever

atag:   
		.word   0 @ r0
		.word   0 @ machine id
		.word   0 @ Store atag position from u-boot

data:
		.word	0xbeef0001
  		.word	0x0f000000
		.word	0xcafe0001
		.word	XC_SOC_PROC_MMREG_BASE

divisor:

#ifdef CONFIG_FPGA_STACK
							@7-stack
							@UART_CLK 	CONFIG_FPGA_APPCLK_SPEED
							@BAUD_RATE 	CONFIG_FPGA_UART_BAUDRATE
							@CLK_DIVIDER	CONFIG_FPGA_APPCLK_DIVIDER
		.word	0x2b		@ (((UART_CLK/UART_CLK_DIVIDER)*32)/(BAUD_RATE*16)+1)/2

#elif defined(CONFIG_FPGA_STANDALONE)
							@Standalone
							@UART_CLK 	CONFIG_FPGA_APPCLK_SPEED
							@BAUD_RATE 	CONFIG_FPGA_UART_BAUDRATE
							@CLK_DIVIDER	CONFIG_FPGA_APPCLK_DIVIDER
		.word	0x21		@ (((UART_CLK/UART_CLK_DIVIDER)*32)/(BAUD_RATE*16)+1)/2
#else  //ENGINERRING BOARD
							@7-stack
							@UART_CLK 	144000000
							@BAUD_RATE 	115200
							@CLK_DIVIDER	1
		.word	0x4e2		@ (((UART_CLK/UART_CLK_DIVIDER)*32)/(BAUD_RATE*16)+1)/2
#endif
