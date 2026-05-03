#define XC_SOC_PROC_MMREG_BASE 0xffee0000

	.section    ".jump","ax"

	@After CPU reset, default to be arm32 instruction set
	@Need to explicitly specify we are use arm32 instruction to avoid kernel to compile as thumb if CONFIG_KERNEL_THUMB2 is on.
	.arm

_jstart:
	b reset
	b undefined
	b svc
	b prefetch
	b abort
	b deadcode
	b irq
	b fiq

reset:
	/* Let's stop CPU 1 first */
	ldr	r0, =(XC_SOC_PROC_MMREG_BASE+0xb60c)
	ldr	r1, [r0]
	bic r1, #2
	str r1, [r0]

	/* Let see all 2G memory */
	ldr r0, =0xffff0000
	ldr r1, =(XC_SOC_PROC_MMREG_BASE+0xb404)
	str r0, [r1]

	/* Borrowed from U-boot */
	/* set the cpu to SVC32 mode */
	mrs r0, cpsr
	bic r0, r0, #0x1f
	orr r0, r0, #0xd3
	msr cpsr,r0

	mov r0, #0          @ set up for MCR
	mcr p15, 0, r0, c8, c7, 0   @ invalidate TLBs
	mcr p15, 0, r0, c7, c5, 0   @ invalidate icache
	mcr p15, 0, r0, c7, c5, 6   @ invalidate BP array
	mcr     p15, 0, r0, c7, c10, 4  @ DSB
	mcr     p15, 0, r0, c7, c5, 4   @ ISB
	
	/*
	* disable MMU stuff and caches
	*/
	mrc p15, 0, r0, c1, c0, 0
	bic r0, r0, #0x00002000 @ clear bits 13 (--V-)
	bic r0, r0, #0x00000007 @ clear bits 2:0 (-CAM)
	#orr r0, r0, #0x00000002 @ set bit 1 (--A-) Align
	bic r0, r0, #0x00000002 @ clear bit 1 (--A-) Align
	orr r0, r0, #0x00000800 @ set bit 11 (Z---) BTB
	bic r0, r0, #0x00001000 @ clear bit 12 (I) I-cache
	mcr p15, 0, r0, c1, c0, 0

	/* End of U-boot */

primary:

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
	and \tmp2, \tmp2, #0xff
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

	ldr	r0, =0xfff02000	@Disable L2CC
	mov	r1, #0
	str r1, [r0, #0x100]

	mrc p15, 0, r0, c1, c0, 0 @Disable MMU, I/D cache
	bic	r0, r0, #(1<<12)
	bic r0, r0, #(1<<2)
	bic r0, r0, #1
	mcr p15, 0, r0, c1, c0, 0
	
	mov	r0, #0					@ Invalidate entire instruction TLB
	mcr p15, 0, r0, c8, c5, 0

	mov	r0, #0					@ Invalidate entire data TLB
	mcr p15, 0, r0, c8, c6, 0

	mov	r0, #0					@ Invalidate entire Unified TLB
	mcr p15, 0, r0, c8, c7, 0


    @Invalidate L1 Instruction Cache, also flushes BTAC

    mov    r0, #0                      @ sbz
    mcr    p15, 0, r0, c7, c5, 0       @ iciallu - invalidate entire i cache, and flushes branch target cache

    @ invalidate data/unified caches

    mrc     p15, 1, r0, c0, c0, 1      @ read clidr
    ands    r3, r0, #0x07000000        @ extract coherency level
    mov     r3, r3, lsr #23            @ total cache levels << 1
    beq     finished                   @ if 0, no need to clean

    mov     r10, #0                    @ r10 holds current cache level << 1
loop1:  
	add     r2, r10, r10, lsr #1       @ r2 holds cache "set" position
    mov     r1, r0, lsr r2             @ bottom 3 bits are the cache-type for this level
    and     r1, r1, #7                 @ isolate those lower 3 bits
    cmp     r1, #2
    blt     skip                       @ no cache or only instruction cache at this level

    mcr     p15, 2, r10, c0, c0, 0     @ write the cache size selection register
    isb                                @ isb to sync the change to the cachesizeid reg
    mrc     p15, 1, r1, c0, c0, 0      @ reads current cache size id register
    and     r2, r1, #7                 @ extract the line length field
    add     r2, r2, #4                 @ add 4 for the line length offset (log2 16 bytes)
    ldr     r4, =0x3ff
    ands    r4, r4, r1, lsr #3         @ r4 is the max number on the way size (right aligned)
    clz     r5, r4                     @ r5 is the bit position of the way size increment
    ldr     r7, =0x7fff
    ands    r7, r7, r1, lsr #13        @ r7 is the max number of the index size (right aligned)

loop2:
   mov     r9, r4                     @ r9 working copy of the max way size (right aligned)

loop3:
   orr     r11, r10, r9, lsl r5       @ factor in the way number and cache number into r11
   orr     r11, r11, r7, lsl r2       @ factor in the set number
   mcr     p15, 0, r11, c7, c6, 2     @ invalidate by set/way
   subs    r9, r9, #1                 @ decrement the way number
   bge     loop3
   subs    r7, r7, #1                 @ decrement the set number
   bge     loop2
skip:
   add     r10, r10, #2               @ increment the cache number
   cmp     r3, r10
   bgt     loop1

finished:

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 6      @ bpiall - invalidate entire branch predictor array

	@ Init UART
	adr	r0, divisor
	ldmia	r0, {r0}
	inituart r0, r1, r2, r3
	
	@ Load in data
	adr	r0, data
	ldmia	r0, {r1 - r5}

	@Try write a marker in memory
   	@str	r1, [r2]

	@Try write a marker in dummy register
   	@str	r3, [r4]

#if 0
	@Output a marker to UART
	ldr r0, =(XC_SOC_PROC_MMREG_BASE+0xf400)
	mov r1, #'*'
	str r1, [r0]
#endif

	@ Jump to kernel start
 	mov	pc, r5

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ Interrupt handlers
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

undefined:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #1
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #1
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever
	
svc:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #2
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #2
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

prefetch:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #3
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #3
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

abort:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #4
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #4
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

deadcode:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #5
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #5
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

irq:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #6
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #6
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

fiq:
	adr	r0, data
	ldmia	r0, {r1 - r5}

	add r1, r1, #7
	@Try write a marker in memory
   	str	r1, [r2]

	add r3, r3, #7
	@Try write a marker in dummy register
   	str	r3, [r4]
	b loop_forever

loop_forever:
	b loop_forever

data:
		.word	0xbeef0001
  		.word	0x0f000000
		.word	0xcafe0001
		.word	XC_SOC_PROC_MMREG_BASE
  		.word	CONFIG_SYS_TEXT_BASE

divisor:

/* #define CONFIG_FPGA_STANDALONE */
#ifdef CONFIG_FPGA_STACK
							@full build
							@UART_CLK 	750000
							@BAUD_RATE 	19200
							@CLK_DIVIDER 1
		.word	0x27		@ (((UART_CLK/UART_CLK_DIVIDER)*32)/(BAUD_RATE*16)+1)/2
#elif defined(CONFIG_FPGA_STANDALONE)
							@Standalone build
							@UART_CLK 	5000000
							@BAUD_RATE 	19200
							@CLK_DIVIDER 1
		.word	0x104		@ (((UART_CLK/UART_CLK_DIVIDER)*32)/(BAUD_RATE*16)+1)/2
#else
							@ASIC
							@UART_CLK 	162000000
							@BAUD_RATE 	115200
							@CLK_DIVIDER 1
		.word 0x57e
#endif
