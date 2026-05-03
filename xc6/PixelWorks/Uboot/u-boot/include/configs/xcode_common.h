#ifndef __CONFIG_XCODE_COMMON_H
#define __CONFIG_XCODE_COMMON_H

//#define FPGA_BUILD 1
/*
 * High Level Configuration Options
 */
#define CONFIG_ARMV7		1	/* This is an ARM V7 CPU core */
#define CONFIG_XCODE		1

#define CONFIG_OMAP_GPIO

#define CONFIG_IP_DEFRAG


/* Get CPU defs */
#include <asm/arch/cpu.h>
#include <asm/arch/omap.h>

/* Display CPU and Board Info */
#define CONFIG_DISPLAY_CPUINFO		1
#define CONFIG_DISPLAY_BOARDINFO	1

/* Clock Defines */
#define V_OSCK			38400000	/* Clock output from T2 */
#define V_SCLK                   V_OSCK

#define CONFIG_MISC_INIT_R

#define CONFIG_OF_LIBFDT		1

#define CONFIG_CMDLINE_TAG		1	/* enable passing of ATAGs */
//#define CONFIG_SETUP_MEMORY_TAGS	1
//#define CONFIG_INITRD_TAG		1
#define CONFIG_REVISION_TAG		1
#define CONFIG_INITRD_TAG 1

#define CONFIG_MD5  1
#define CONFIG_BZIP2 1

/*
 * Size of malloc() pool
 * Total Size Environment - 128k
 * Malloc - add 64M
 */
#define CONFIG_SYS_MALLOC_LEN		(CONFIG_ENV_SIZE + (64 << 20))

/* Vector Base */
#define CONFIG_SYS_CA9_VECTOR_BASE	SRAM_ROM_VECT_BASE

/*
 * Hardware drivers
 */

/*
 * serial port - NS16550 compatible
 */
#define V_NS16550_CLK			48000000

#define CONFIG_XCODE_SERIAL

#define CONFIG_BAUDRATE			115200
#define CONFIG_SYS_BAUDRATE_TABLE	{4800, 9600, 19200, 38400, 57600,\
					115200}
/* I2C  */
//#define CONFIG_HARD_I2C			1
#define CONFIG_SYS_I2C_BUS_SELECT	1
#define CONFIG_I2C_MULTI_BUS		1
#define CONFIG_XCODE_I2C
#define CONFIG_DRIVER_XCODE_I2C

#define CONFIG_SYS_NAND_MAX_CHIPS 8


#define CONFIG_SYS_CONSOLE_IS_IN_ENV	1


/*
 * Miscellaneous configurable options
 */

#define CONFIG_SYS_LONGHELP	/* undef to save memory */
#define CONFIG_SYS_HUSH_PARSER	/* use "hush" command parser */
#define CONFIG_SYS_CBSIZE		512
/* Print Buffer Size */
#define CONFIG_SYS_PBSIZE		(CONFIG_SYS_CBSIZE + \
					sizeof(CONFIG_SYS_PROMPT) + 16)
#define CONFIG_SYS_MAXARGS		16
/* Boot Argument Buffer Size */
#define CONFIG_SYS_BARGSIZE		(CONFIG_SYS_CBSIZE)

/*
 * Memtest setup
 * The test region is determined after auto detect, this is the default region
 */
#define CONFIG_SYS_ALT_MEMTEST 1
#define CONFIG_SYS_MEMTEST_ITER         1
#define CONFIG_SYS_MEMTEST_START       (0x0UL)
#define CONFIG_SYS_MEMTEST_END         (0x40000000UL)

/* Default setup */
/* load address */
#define CONFIG_SYS_LOAD_ADDR            (0x16000000UL)
#define CONFIG_SYS_UBOOT_RELOC_MASK     (0x20000000UL - 1)

/* Use General purpose timer 1 */
#define CONFIG_SYS_TIMERBASE		GPT2_BASE
#define CONFIG_SYS_PTV			2	/* Divisor: 2^(PTV+1) => 8 */
#define CONFIG_SYS_HZ			1000

/*
 * SDRAM Memory Map
 * Even though we use two CS all the memory
 * is mapped to one contiguous block
 */
#define CONFIG_NR_DRAM_BANKS	1

#define CONFIG_SYS_SDRAM_BASE		0x00000000
#define CONFIG_SYS_INIT_SP_ADDR     0x00002000
#define CONFIG_ATAG_BASE			0x14800000

#define CONFIG_SYS_SDRAM_NON_CACHE_MIRROR 1
#define CONFIG_SYS_SDRAM_NON_CACHE_MIRROR_OFFSET 0x80000000

//#define CONFIG_SYS_DCACHE_OFF 1
#define CONFIG_SYS_L2CACHE_OFF
#ifndef CONFIG_SYS_L2CACHE_OFF
#define CONFIG_SYS_L2_PL310		1
#define CONFIG_SYS_PL310_BASE	0x48242000
#endif
#define CONFIG_SYS_CACHELINE_SIZE	32

#define ALIGN_CACHE_SIZE(x) 	(((x)+CONFIG_SYS_CACHELINE_SIZE-1)&~(CONFIG_SYS_CACHELINE_SIZE-1))

/* Defines for SDRAM init */
#define CONFIG_SYS_EMIF_PRECALCULATED_TIMING_REGS

#ifndef CONFIG_SYS_EMIF_PRECALCULATED_TIMING_REGS
#define CONFIG_SYS_AUTOMATIC_SDRAM_DETECTION
#define CONFIG_SYS_DEFAULT_LPDDR2_TIMINGS
#endif

/*
 * 64 bytes before this address should be set aside for u-boot.img's
 * header. That is 80E7FFC0--0x80E80000 should not be used for any
 * other needs.
 */
#define CONFIG_SYS_JUMP_BASE		0x01000000
#define CONFIG_SYS_TEXT_BASE		0x01008000
//#define CONFIG_SYS_UBOOT_START		0x01008000

//#define CONFIG_SYS_THUMB_BUILD
#define CONFIG_KERNEL_MEM_SIZE	(512 * 1024 * 1024)
#define XC_SOC_PROC_MMREG_BASE 0xffee0000UL
#define XC_REG(reg)         *(volatile unsigned int *)(XC_SOC_PROC_MMREG_BASE+(reg))

#define xc_writel(val, addr)    *(volatile unsigned int *)(addr)=(val)
#define xc_readl(addr)          *(volatile unsigned int *)(addr)

#if 0
#define CORE_PERIPHERAL_BASE    0xfff00000UL
#else
#define CORE_PERIPHERAL_BASE    ({ \
    unsigned long base; \
    __asm__ __volatile__ ("mrc p15, 4, %0, c15, c0, 0" \
        : "=r&" (base) \
        : \
        : "cc"); \
        base; \
    })

#endif

#define CORE_SCU_OFFSET             0x00000000UL
#define CORE_GIC_OFFSET             0x00000100UL
#define CORE_GLOBAL_TIMER_OFFSET    0x00000200UL
#define CORE_PRIVATE_TIMER_OFFSET   0x00000600UL
#define CORE_INTDIS_OFFSET          0x00001000UL
#define CORE_L2CACHE_OFFSET         0x00002000UL

/* SPI Flash */
# define CONFIG_SF_DEFAULT_SPEED	1000000
# define CONFIG_SF_DEFAULT_MODE		SPI_MODE_3
# define CONFIG_SF_DEFAULT_CS		0
# define CONFIG_SF_DEFAULT_BUS		0
#endif /* __CONFIG_XCODE_COMMON_H */
