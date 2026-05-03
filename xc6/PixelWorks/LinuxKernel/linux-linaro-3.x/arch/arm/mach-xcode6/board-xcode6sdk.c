/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/suspend.h>
#include <plat/common.h>
#include <mach/io.h>
#include <mach/xcode6-common.h>


extern struct smp_operations xcode_smp_ops;

LogInfoStruct __iomem *xc_log_info = NULL;
void *xc_log_buf = NULL;
unsigned long xc_log_buf_len = 0;
EXPORT_SYMBOL(xc_log_info);
EXPORT_SYMBOL(xc_log_buf);
EXPORT_SYMBOL(xc_log_buf_len);

void *xcode6_p9fw_fb;
unsigned int xcode6_p9fw_fb_size;
struct notifier_block xcode6_p9fw_pm_notify;

extern void xcode6_timer_init(void);
extern int pollReg (unsigned int address, unsigned int expectedValue, unsigned int mask, int timeOut);
extern int InitPCIeHostBridge (int port);
#if 0
static int xcode6_get_osc_diff(void)
{
  u32 osc_cnt;

  udelay(5);
                                
  // program src_clk_cnt value 
  mmr_write (1000, CG_SRC_CLK_CNT_CTRL);
   
  // enable counting
  mmr_write (1000 | CG_SRC_CLK_CNT_CTRL_ENB_MASK, CG_SRC_CLK_CNT_CTRL);
  
  // check if counting has started. poll for busy
  while(!(mmr_read(CG_DST_CLK_CNT) & CG_DST_CLK_CNT_BUSY_MASK));
 
  // poll for NOT busy
  while((mmr_read(CG_DST_CLK_CNT) & CG_DST_CLK_CNT_BUSY_MASK));

  // disable counting
  mmr_write (0, CG_SRC_CLK_CNT_CTRL);
  // read osc_cnt
  osc_cnt = mmr_read (CG_DST_CLK_CNT);

  // the percent frequency change required is -(diff_osc_cnt*0.1)%
  return (1000 - (int)osc_cnt);  //increase frequency if negative
}
#endif
#ifdef CONFIG_PLAT_XCODE64xx
static u32 xcode6_oscclk_adj_to_25M (void)
{
  int diff_osc_cnt;
  u32 osc_setting;
  u32 osc_mask = 0xff;
  u32 iter;

  iter = 0;
  osc_setting = 0xF8; 

  do {
    // program AON_OSCCLK_CFG
    mmr_write (8 << 12 | (osc_setting<<OSC_SETTING_SHIFT), AON_OSCCLK_CFG);
    mmr_write (8 << 12 | (osc_setting<<OSC_SETTING_SHIFT) | AON_OSCCLK_CFG_OSC_SETTING_OVERRIDE_MASK, AON_OSCCLK_CFG);
    
    diff_osc_cnt = xcode6_get_osc_diff();
    if (diff_osc_cnt>-2 && diff_osc_cnt<2)   { break; } // good enough
    
    osc_setting = diff_osc_cnt<0 ? osc_setting+1 : osc_setting-1;
    osc_setting = osc_setting & osc_mask;
    
    iter++;
    
    // block off this zone.  From Mike Cave 
    if (osc_setting>=128 && osc_setting<160) { break; }

  } while(iter<128);

  return xcode6_get_osc_diff(); // adjusted chip 
}
#else
#define xcode6_oscclk_adj_to_25M() 1
#endif

static void __init xcode6_init_early(void)
{
    u32 ret;
    
    ret = xcode6_oscclk_adj_to_25M();

    printk("xcode6_oscclk_adj_to_25M return: %x\n", ret);
}

static irqreturn_t xcode6_resume_ipc_handler(int irq, void *dev_id)
{
    u32 intStatus0;

    intStatus0 = mmr_read(IPC_PROC5_INT_STATUS0);
    mmr_write(IPC_PROC5_INT_STATUS0_INT_STATUS_MASK, IPC_PROC5_INT_STATUS0);

    printk("[%s]: resume ipc received: INT_STATUS0: %x\n", __func__, intStatus0);
    return (IRQ_HANDLED);
}

void xcode6_suspend_ipc_send(void)
{
    mmr_write(mmr_read(ACC_RESET_REG2) |  ACC_RESET_REG2_PROC9_RESET_MASK, ACC_RESET_REG2);
    mmr_write(mmr_read(ACC_RESET_REG2) & ~ACC_RESET_REG2_PROC9_RESET_MASK, ACC_RESET_REG2);
    mmr_write(PROC9_RST_SOFT_RSTN_MASK, PROC9_RST);

    mmr_write((0x0200 << 8) | (0x0000), IPC_PROC5_ADDRESS);
    mmr_write(virt_to_phys(xcode6_p9fw_fb), IPC_PROC5_MESSAGE);
    mmr_write(xcode6_p9fw_fb_size, IPC_PROC5_COMMAND);
}

static int xcode6_suspend_begin(suspend_state_t state)
{  
    return 0;
}

static int xcode6_suspend_prepare(void)
{
    int err = 0;

    err = request_irq(XCODE6_IRQ_PROC5, xcode6_resume_ipc_handler,
                      IRQF_SHARED | IRQF_NO_SUSPEND,
                      "XCODE6-RESUME-IPC",(void *)XCODE6_IRQ_PROC5);
    
    if (err)
        printk("[%s]: request_irq for PM IPC failed\n", __func__);

    return err;
}

static int xcode6_suspend_enter(suspend_state_t state)
{    
    switch (state) {
    case PM_SUSPEND_MEM:
        disable_percpu_irq(30);     
#ifdef CONFIG_PCI
        if (mmr_read(PCIE_HOST_FB_MASK)& PCIE_HOST_FB_MASK_HOST_MODE_MASK)
        {
            //power down PCIe host0&1 PHY
            mmr_write(0x18, PCIE_PHY_ANALOG_CTRL);

            #ifdef CONFIG_PLAT_XCODE64xx
            mmr_write(0x18, PCIE_PHY_ANALOG_CTRL+0xF800);
            #endif            
        }
#endif
        xcode6_suspend_ipc_send();
        cpu_do_idle();
        enable_percpu_irq(30, 0);
        break;

    default:
        return -EINVAL;
    }
    return 0;
}

void xcode6_suspend_wake(void)
{
    free_irq(XCODE6_IRQ_PROC5, (void *)XCODE6_IRQ_PROC5);
#ifdef CONFIG_PCI
    if (mmr_read(PCIE_HOST_FB_MASK)& PCIE_HOST_FB_MASK_HOST_MODE_MASK)
    {
        InitPCIeHostBridge(0);
        
        #ifdef CONFIG_PLAT_XCODE64xx
        InitPCIeHostBridge(1);
        #endif
    }
#endif
}

void xcode6_suspend_end(void)
{    
    kfree(xcode6_p9fw_fb);
}


static const struct platform_suspend_ops xcode6_suspend_ops = {
    .begin = xcode6_suspend_begin,
    .prepare = xcode6_suspend_prepare,
    .enter = xcode6_suspend_enter,
    .wake  = xcode6_suspend_wake,
    .end   = xcode6_suspend_end,
    .valid = suspend_valid_only_mem,
};

/* 
 * xcode_monitor is the monitor for IO port power status
 * a 250ms timer is runnning for check the GPIO status
 * and control the power state of the port
 */
static struct timer_list xcode_monitor;
//static unsigned int oc_cnt = 0;
static void xcode_monitor_func(unsigned long bid)
{	
#ifdef CONFIG_PLAT_XCODE64xx    
	unsigned long in, out;
	int oc_flag = 0;
	//printk("set up timer for board 0x%x\n", (unsigned)bid);
	mod_timer(&xcode_monitor, (jiffies/(HZ/4) + 1) * (HZ/4));

	switch (bid) {
	case 0x0010:
	case 0x0011:
	case 0x0012:
		if (likely(!oc_cnt)) {
			in = mmr_read(GPIO_DEDICATED_IN);			
			if (unlikely(!(in & (1 << 3)))) {
				printk("USB Device Over Current Status Detected !!\nUSB Port Will Shut Down 5 Seconds.\n");
				mmr_write(mmr_read(GPIO_DEDICATED_OUT) & ~(0x3 << 4), GPIO_DEDICATED_OUT);
				oc_cnt++;	
			}
		} else {
			if (oc_cnt > 19){
				printk("USB Port Turn On.\n");
				mmr_write(mmr_read(GPIO_DEDICATED_OUT) | (0x3 << 4), GPIO_DEDICATED_OUT);
				oc_cnt = 0;
			} else {
				oc_cnt++;
			}			
		}
		break;

	case 0x1600:
	case 0x1601:		
		if (likely(!oc_cnt)) {
			in = mmr_read(GPIO_DEDICATED_IN);			
			if (unlikely(!(in & (1 << 3)))) {
				printk("USB 2.0 Device Over Current Status Detected !!\nUSB 2.0 Port Will Shut Down 5 Seconds.\n");
				mmr_write(mmr_read(GPIO_DEDICATED_OUT) & ~(0x1 << 10), GPIO_DEDICATED_OUT);
				oc_flag++;
			}
			if (unlikely(!(in & (1 << 4)))) {
				printk("USB 3.0 Device Over Current Status Detected !!\nUSB 3.0 Port Will Shut Down 5 Seconds.\n");
				mmr_write(mmr_read(GPIO_DEDICATED_OUT) & ~(1 << 5), GPIO_DEDICATED_OUT);
				oc_flag++;
			}
			/* GPIO-G */
			in = mmr_read(GPIO_G_IN);
			if (unlikely(!(in & 0x1))) {
				printk("SD Device Over Current Status Detected !!\nSD Port Will Shut Down 5 Seconds.\n");
				mmr_write(mmr_read(GPIO_DEDICATED_OUT) & ~(1 << 8), GPIO_DEDICATED_OUT);
				oc_flag++;
			}

			if(oc_flag)
				oc_cnt++;
		} else {
			if (oc_cnt > 19){
				out = mmr_read(GPIO_DEDICATED_OUT);
				if(!(out & (1 << 5))) {
					printk("USB 3.0 Port Turn On.\n");
					mmr_write(mmr_read(GPIO_DEDICATED_OUT) | (1 << 5), GPIO_DEDICATED_OUT);
				}
				if(!(out & (1 << 10))) {
					printk("USB 2.0 Port Turn On.\n");
					mmr_write(mmr_read(GPIO_DEDICATED_OUT) | (1 << 10), GPIO_DEDICATED_OUT);
				}
				if(!(out & (1 << 8))) {
					printk("SD Port Turn On.\n");
					mmr_write(mmr_read(GPIO_DEDICATED_OUT) | (1 << 8), GPIO_DEDICATED_OUT);
				}
				oc_cnt = 0;
			} else {
				oc_cnt++;
			}			
		}
		break;

	default:
		break;
	}	
#endif
}

static void xcode6_p9fwloader_load(struct device *dev)
{
    const struct firmware *fw_entry;

    if ((request_firmware(&fw_entry, "xcode6_p9_pm.bin", dev)) != 0) {
        printk("[%s]: request_firmware for PM IPC failed\n", __func__);
        return;
    }
    
    xcode6_p9fw_fb = kmalloc(fw_entry->size, GFP_KERNEL | __GFP_DMA);
    xcode6_p9fw_fb_size = fw_entry->size;
    
    if (xcode6_p9fw_fb == NULL) {
        printk("[%s]: kmalloc for P9 firmware buffer failed\n", __func__);
        return;
    }
    
    memcpy(xcode6_p9fw_fb, fw_entry->data, fw_entry->size);

    flush_dcache_range((u32)xcode6_p9fw_fb, ((u32)xcode6_p9fw_fb + xcode6_p9fw_fb_size));
    
    release_firmware(fw_entry);    
}

static void xcode6_p9fwloader_release(struct device *dev)
{
    printk("XCode6_P9FwLoader device released\n");
}

static struct device xcode6_p9fwloader_device = {
    .init_name = "XCode6_P9FwLoader",
    .release = xcode6_p9fwloader_release
};


static int xcode6_p9fwloader_pm_notify(struct notifier_block *notify_block,
            unsigned long mode, void *unused)
{
    switch (mode) {
    case PM_HIBERNATION_PREPARE:
    case PM_SUSPEND_PREPARE:
        xcode6_p9fwloader_load(&xcode6_p9fwloader_device);
        break;

    case PM_POST_SUSPEND:
    case PM_POST_HIBERNATION:
    case PM_POST_RESTORE:
        break;
    }

    return 0;
}

static int __init xcode6_p9fwloader_init(void)
{
    device_register(&xcode6_p9fwloader_device);
    xcode6_p9fw_pm_notify.notifier_call = xcode6_p9fwloader_pm_notify;
    register_pm_notifier(&xcode6_p9fw_pm_notify);
    return 0;
}

static void __init xcode6_p9fwloader_exit(void)
{
    unregister_pm_notifier(&xcode6_p9fw_pm_notify);
    device_unregister(&xcode6_p9fwloader_device);
}

module_init(xcode6_p9fwloader_init);
module_exit(xcode6_p9fwloader_exit);

static void __init xcode6_init(void)
{
	volatile unsigned long board_id = mmr_read(CG_DUMMY_REG1);
	
	setup_timer(&xcode_monitor, xcode_monitor_func, board_id);
	mod_timer(&xcode_monitor, (jiffies/(HZ/4) + 1) * (HZ/4));
	
	if (!xc_log_info) {		
		xc_log_info = ioremap_nocache(0x000ff000, 0x1000);
		memset((void *)xc_log_info, 0, 0x1000);
		xc_log_info->desc[1].addr = (u32)xc_log_buf;
		xc_log_info->desc[1].size = xc_log_buf_len;
		xc_log_info->desc[1].phead = 0x000ff04c;
		xc_log_info->desc[1].head = (u32)xc_log_buf;
	}

    /*
     * enable PM support for XC64xx and XC68xx SDK board
     */
    #ifdef CONFIG_PLAT_XCODE68xx
    if ((board_id & 0xFF00) == 0x0000) 
    {
        suspend_set_ops(&xcode6_suspend_ops);
    }
    #endif

    #ifdef CONFIG_PLAT_XCODE64xx
    if ((board_id & 0xFF00) == 0x0000) 
    {
        suspend_set_ops(&xcode6_suspend_ops);
    }
    #endif
}

void __init xcode6_map_io(void)
{
	xcode6_map_common_io();
}

static const char *xcode6_match[] __initdata = {
	"ViXS,Xcode6",
	NULL,
};

MACHINE_START(XCODE6, "Xcode6")
	.atag_offset	= 0x00000000, //Let's define it later
	.reserve	= xcode6_reserve,
	.smp        = smp_ops(xcode_smp_ops),
	.map_io		= xcode6_map_io,
	.init_early	= xcode6_init_early,
	.init_irq	= gic_init_irq,
//	.handle_irq = gic_handle_irq,
	.init_machine	= xcode6_init,
	.init_time		= xcode6_timer_init,
	.dt_compat	= xcode6_match,
MACHINE_END
