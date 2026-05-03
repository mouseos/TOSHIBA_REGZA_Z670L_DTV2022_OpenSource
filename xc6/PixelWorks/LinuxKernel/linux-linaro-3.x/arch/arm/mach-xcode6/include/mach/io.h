#ifndef MACH_IO_H
#define MACH_IO_H

//#define USE_IIA

#include <mach/iia.h>
#include <plat/io.h>
#include <plat/xcodeRegDef.h>

#define XC_SOC_PROC_MMREG_BASE 0xffee0000

#define xcode_readl(x)        readl((volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+(x)))
#define xcode_writel(val, x)  writel((val), (volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+(x)))

#define xcode_setval(val, field, x) \
  ({ \
    u32 t; \
    t=xcode_readl(x); \
    t&=~(x##_##field##_MASK); \
    t|=((val)<<x##_##field##_SHIFT); \
    xcode_writel(t, x); \
  })

#define xcode_getval(field, x) \
  ({ \
    u32 t; \
    t=xcode_readl(x); \
    t&=(x##_##field##_MASK); \
    t>>=(x##_##field##_SHIFT); \
		t; \
  })

#if 0
#define CORE_PERIPHERAL_BASE	0xfff00000UL
#else
#define CORE_PERIPHERAL_BASE 	({ \
	unsigned long base; \
	__asm__ __volatile__ ("mrc p15, 4, %0, c15, c0, 0" \
		: "=r&" (base) \
		: \
		: "cc"); \
		base; \
	})

#endif

#define CORE_SCU_OFFSET        		0x00000000UL
#define CORE_GIC_OFFSET         	0x00000100UL
#define CORE_GLOBAL_TIMER_OFFSET	0x00000200UL
#define CORE_PRIVATE_TIMER_OFFSET   0x00000600UL
#define CORE_INTDIS_OFFSET      	0x00001000UL
#define CORE_L2CACHE_OFFSET			0x00002000UL

#ifndef __ASSEMBLY__
extern void v7_flush_kern_dcache_area(void *addr, size_t c);
extern void v7_dma_inv_range(void *start, void *end);
extern void v7_flush_kern_cache_all(void);
unsigned long virt_to_phys_from_pte(void *addr);
extern unsigned long xc_fb_max;
extern unsigned long xc_vm_start;

//end = start + size - 1 
// The total memory size is stored in CG_DUMMY_REG2, it is initialized by the kernel memory init
// to flush and invalidate the L2C on memory address which was out of the kernel memory space, 
// the physical address is parsed from the page table

static inline void flush_dcache_range(u32 start, u32 end)
{ 
	v7_flush_kern_dcache_area((void *)(start), (size_t)(end)-(size_t)(start) + 1);

#ifdef CONFIG_OUTER_CACHE
{
	u32 addr = virt_to_phys((void *)start);
	if ( 
		(start < 0x80000000)	// virtual address from user space or kernel space
		|| 
		(start >= xc_vm_start)   // virtual address from kernel space on 4GB board
		||
		(addr >= xc_fb_max)	// mapped from physical addres out side kernel space
		)
	{
		//printk("[%s] start=0x%08x, addr=0x%08x, virt_to_phys_from_pte=0x%08x\n",__func__, (unsigned)start, (unsigned)addr, (unsigned)virt_to_phys_from_pte((void *)start));		
		outer_flush_range(virt_to_phys_from_pte((void *)start), virt_to_phys_from_pte((void *)end) + 1);
	}
	else
	{
		// mapped from kernel space address
		outer_flush_range(virt_to_phys((void *)start), virt_to_phys((void *)end) + 1);
	}
}
#endif
}

//end = start + size - 1
static inline void inv_dcache_range(u32 start, u32 end)
{
#ifdef CONFIG_OUTER_CACHE
{
	u32 addr = virt_to_phys((void *)start);
	if ( 
		(start < 0x80000000)	// virtual address from user space or kernel space
        || 
		(start >= xc_vm_start)   // virtual address from kernel space on 4GB board		
		|| 
		(addr >= xc_fb_max)	// mapped from physical addres out side kernel space
		)
	{
		//printk("[%s] start=0x%08x, addr=0x%08x, virt_to_phys_from_pte=0x%08x\n",__func__, (unsigned)start, (unsigned)addr, (unsigned)virt_to_phys_from_pte((void *)start));
		outer_inv_range(virt_to_phys_from_pte((void *)start), virt_to_phys_from_pte((void *)end) + 1);
	}
	else
	{
		// mapped from kernel space address
		outer_inv_range(virt_to_phys((void *)start), virt_to_phys((void *)end) + 1);
	}
}		
#endif
	v7_dma_inv_range((void *)(start), (void *)(end));
}

static inline void flush_dcache_all(void)
{
	v7_flush_kern_cache_all();
#ifdef CONFIG_OUTER_CACHE
	outer_flush_all();
#endif
}

/* 
 * L2 cache inv_all must be done while L2 cache is disabled
 * L2 inv_all is only used before turn on L2
 * Drivers should use inv_range, can not use inv_all
 *
 */
#define inv_dcache_all()	BUG()

#endif

#endif
