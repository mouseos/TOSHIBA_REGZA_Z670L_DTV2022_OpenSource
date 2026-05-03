/*
 *
 * Common functions for OMAP4/5 based boards
 *
 * (C) Copyright 2010
 * Texas Instruments, <www.ti.com>
 *
 * Author :
 *	Aneesh V	<aneesh@ti.com>
 *	Steve Sakoman	<steve@sakoman.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <common.h>
#include <spl.h>
#include <asm/arch/sys_proto.h>
#include <asm/sizes.h>
#include <asm/emif.h>
#include <asm/arch/xcodeRegDef.h>

DECLARE_GLOBAL_DATA_PTR;

void do_set_mux(u32 base, struct pad_conf_entry const *array, int size)
{
	int i;
	struct pad_conf_entry *pad = (struct pad_conf_entry *) array;

	for (i = 0; i < size; i++, pad++)
		writew(pad->val, base + pad->offset);
}

u32 cortex_rev(void)
{

	unsigned int rev;

	/* Read Main ID Register (MIDR) */
	asm ("mrc p15, 0, %0, c0, c0, 0" : "=r" (rev));

	return rev;
}

void xcode_rev_string(void)
{
	unsigned int id = XC_REG(RBM_BOARD_ID);

	printf("XCode %d Rev %d\n", (id>>8)&0xff, (id>>16)&0xff);
}

#ifdef CONFIG_SPL_BUILD
static void init_boot_params(void)
{
	boot_params_ptr = (u32 *) &boot_params;
}

void spl_display_print(void)
{
	xcode_rev_string();
}
#endif

/*
 * Routine: s_init
 * Description: Does early system init of watchdog, muxing,  andclocks
 * Watchdog disable is done always. For the rest what gets done
 * depends on the boot mode in which this function is executed
 *   1. s_init of SPL running from SRAM
 *   2. s_init of U-Boot running from FLASH
 *   3. s_init of U-Boot loaded to SDRAM by SPL
 *   4. s_init of U-Boot loaded to SDRAM by ROM code using the
 *	Configuration Header feature
 * Please have a look at the respective functions to see what gets
 * done in each of these cases
 * This function is called with SRAM stack.
 */
void s_init(void)
{
	watchdog_init();
#ifdef CONFIG_SPL_BUILD
	setup_clocks_for_console();

	gd = &gdata;

	preloader_console_init();
	do_io_settings();
#endif
	prcm_init();
#ifdef CONFIG_SPL_BUILD
	timer_init();

	/* For regular u-boot sdram_init() is called from dram_init() */
	sdram_init();
	init_boot_params();
#endif
}

/*
 * Routine: wait_for_command_complete
 * Description: Wait for posting to finish on watchdog
 */
void wait_for_command_complete(struct watchdog *wd_base)
{
	int pending = 1;
	do {
		pending = readl(&wd_base->wwps);
	} while (pending);
}

/*
 * Routine: watchdog_init
 * Description: Shut down watch dogs
 */
void watchdog_init(void)
{
}

u32 get_number_of_one(u32 i)
{
	i = i - ((i>>1) & 0x55555555);
	i = (i & 0x33333333) + ((i>>2) & 0x33333333);
	return ((((i + (i>>4)) & 0x0f0f0f0f) * 0x01010101) >> 24);
}

/*
 * This function finds the SDRAM size available in the system
 * based on DMM section configurations
 * This is needed because the size of memory installed may be
 * different on different versions of the board
 * The meminit info register defination
 * INFO0 
 * 	31:16 Board ID
 * INFO1 
 * 	31:28 Gen Init Version Major
 * 	27:20 Gen Init Version Minor
 * 	19:16 Gen Init Header Version Major
 *	15:08 Gen Init Header Version Minor
 *	07:00 Test Version
 */
u32 xcode_sdram_size(void)
{
	u32 size = 0;
	u32 present;

	u32 version, config; 
	version = XC_REG(MC_MEM_INIT_INFO2) >> 16;
	if (version >= 0x1810) {
		present = XC_REG(MC_MEM_INIT_INFO6);
		config = XC_REG(MC_CONFIG_REG);
		size = get_number_of_one(present) * 128;
		printf("DRAM size %d MB, CH0: 0x%04x CH1: 0x%04x mc_config 0x%08x\n", 
			size, present & 0xFFFF, present >> 16, config);
	}

	/*
	 * the DRAM size = (FB_SIZE + 1) * 128MB
	 * Reserve top 6MB for 4GB board
	 */
	size = XC_REG(SEQ_CH0_FB_SIZE) & SEQ_CH0_FB_SIZE_SIZE_MASK;
	size >>= SEQ_CH0_FB_SIZE_SIZE_SHIFT;
	size++;
	size <<= 7;
	if(size == 4096)
		size -= 6;
	debug("%s total dram size %d MB\n", __func__, size);
	return (size << 20);
}


/*
 * Routine: dram_init
 * Description: sets uboots idea of sdram size
 */
int dram_init(void)
{
	sdram_init();
	gd->ram_size = xcode_sdram_size();
	return 0;
}

/*
 * Print board information
 */
int checkboard(void)
{
	puts(sysinfo.board_string);

	return 0;
}

/*
 *  get_device_type(): tell if GP/HS/EMU/TST
 */
u32 get_device_type(void)
{
	struct omap_sys_ctrl_regs *ctrl =
		      (struct omap_sys_ctrl_regs *) SYSCTRL_GENERAL_CORE_BASE;

	return (readl(&ctrl->control_status) &
				      (DEVICE_TYPE_MASK)) >> DEVICE_TYPE_SHIFT;
}

/*
 * Print CPU information
 */
int print_cpuinfo(void)
{
	puts("CPU  : ");
	xcode_rev_string();

	return 0;
}
#ifndef CONFIG_SYS_DCACHE_OFF
void enable_caches(void)
{
	/* Enable D-cache. I-cache is already enabled in start.S */
	dcache_enable();
}
#endif
