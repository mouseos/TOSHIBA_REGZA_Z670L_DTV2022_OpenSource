#ifndef ARCH_XCODE6_COMMON_H
#define ARCH_XCODE6_COMMON_H

#ifdef CONFIG_THUMB2_KERNEL
#define do_wfi() __asm__ __volatile__ ("wfi" : : : "memory")
#else
#define do_wfi()			\
		__asm__ __volatile__ (".word	0xe320f003" : : : "memory")
#endif

#ifdef CONFIG_CACHE_L2X0
extern void __iomem *l2cache_base;
#endif

extern void __iomem *gic_dist_base_addr;

extern void __init gic_init_irq(void);
extern void xcode6_smc1(u32 fn, u32 arg);

/* For XCODE dump */
#define XC_LOG_BUF_LEN ((1 << CONFIG_LOG_BUF_SHIFT) + PAGE_SIZE + 32)
#define XC_DEBUG_BUF_DESC_PHYS	(0x000FF000UL)

typedef struct _NandInfo
{
	u32	start;
	u32	size;
	u32	page_size;
	u32	data_buf;
	u32	buff_size;
	u32	spare_buf;
	u32	resv[2];
} NandInfo;

typedef struct _DebugBufDesc
{
	u32	addr;
	u32	size;
	u32	phead;
	u32	head;
	u32	reserved[4];
} DebugBufDesc;

typedef struct _LogInfoStruct
{
	NandInfo nandinfo;
	DebugBufDesc desc[];	
} LogInfoStruct;

#ifdef CONFIG_SMP
/* Needed for secondary core boot */
extern void xcode6_secondary_startup(void);
extern u32 xcode6_modify_auxcoreboot0(u32 set_mask, u32 clear_mask);
extern void xcode6_auxcoreboot_addr(u32 cpu_addr);
extern u32 xcode6_read_auxcoreboot0(void);
extern void secondary_startup(void);
extern char xcode6_secondary_trampoline;
extern char xcode6_secondary_trampoline_jump;
extern char xcode6_secondary_trampoline_end;
#endif
int xc_dma_host_to_host(unsigned char* dest, unsigned char* src, unsigned int len);
#endif
