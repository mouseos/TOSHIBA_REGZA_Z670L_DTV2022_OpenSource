#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <asm/tlb.h>

#include <asm/mach/map.h>

#include "io.h"

/*
 * The machine specific code may provide the extra mapping besides the
 * default mapping provided here.
 */

static struct map_desc xcode6_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)XC_SOC_PROC_MMREG_BASE,
		.pfn		= __phys_to_pfn((unsigned long)XC_SOC_PROC_MMREG_BASE),
		.length		= 0x00110000,
		.type		= MT_DEVICE,
	},
#if 0
#ifdef CONFIG_FPGA_BUILD  
//For memory log in FPGA test
	{
		.virtual	= 0xf8000000,
		.pfn		= __phys_to_pfn(0xf8000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE,
	}, 
#endif
#endif
};

void __init xcode6_map_common_io(void)
{
	iotable_init(xcode6_io_desc, ARRAY_SIZE(xcode6_io_desc));

	/* Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but we must also do it here because of the CPU
	 * revision check below.
	 */
	local_flush_tlb_all();
	flush_cache_all();
}

