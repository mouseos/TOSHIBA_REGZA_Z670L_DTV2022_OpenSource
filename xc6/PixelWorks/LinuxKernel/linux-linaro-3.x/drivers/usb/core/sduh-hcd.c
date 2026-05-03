/*
* Host Controller Driver for the Vixs Systems Xcode3 SDUH USB Host
*
* Copyright(C) 2007 Vixs Systems Limited
* http://www.Vixs.com
*
* Author and Maintainer - Jerry Wang
* jlwang@vixs.com
*
* This program is free software;you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation, version 2.
*
*
*/ 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/pci_ids.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include "sduh-hcd.h"

#define dbg printk
#define SUSB_RETRY_LIMIT (1000)

u32 SDUH_HCD_REG_BASE = 0;
EXPORT_SYMBOL(SDUH_HCD_REG_BASE);

u32 SDUH_HCD_REF_CNT = 0;
EXPORT_SYMBOL(SDUH_HCD_REF_CNT);

u32 SDUH_HCD_ACTIVE_CNT = 0;
EXPORT_SYMBOL(SDUH_HCD_ACTIVE_CNT);

spinlock_t sduh_usb_indirect_reg_lock;
EXPORT_SYMBOL(sduh_usb_indirect_reg_lock);

//TODO: [JLWANG] Add debug message if failed in case
u32 SDUH_writel(u32 SDUH_reg_base, u32 ehci_reg_addr, u32 value, SDUH_HC_reg_type type)
{
	u32 regVal = 0, retry= 0;
    unsigned long flags;

	spin_lock_irqsave(&sduh_usb_indirect_reg_lock, flags);
	while(((regVal = reg_readl(SDUH_reg_base + SDUH_IAS_REG))&0x1)== 1)
	{
		if(++retry >= SUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}


    reg_writel(value, SDUH_reg_base + SDUH_WDATA_REG);
    switch(type)
    {
        case EHCI_REG:
            reg_writel(ehci_reg_addr|0x80000000, SDUH_reg_base + SDUH_EOC_REG);
            break;
        case OHCI_REG:
            reg_writel(ehci_reg_addr|0x80000000, SDUH_reg_base + SDUH_OOC_REG);
            break;
        default:
            spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
            return 0;
            break;
    }

	retry = 0;
	while(((regVal = reg_readl(SDUH_reg_base + SDUH_IAS_REG))&0x1)== 1)
	{
		if(++retry >= SUSB_RETRY_LIMIT) {
			BUG();
    spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
    return 0;
};
EXPORT_SYMBOL(SDUH_writel);


u32 SDUH_readl(u32 SDUH_reg_base, u32 ehci_reg_addr, SDUH_HC_reg_type type)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;

    spin_lock_irqsave(&sduh_usb_indirect_reg_lock, flags);

    while(((regVal = reg_readl(SDUH_reg_base + SDUH_IAS_REG))&0x1)== 1)
    {
		if(++retry >= SUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
			return 0;
		}
	}


    switch(type)
    {
    case EHCI_REG:
    	reg_writel(ehci_reg_addr, SDUH_reg_base + SDUH_EOC_REG);
    	break;
    case OHCI_REG:
    	reg_writel(ehci_reg_addr, SDUH_reg_base + SDUH_OOC_REG);
                    break;
    default:
        spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
    	 return 0;
    	 break;
    }

	retry = 0;
    while(((regVal = reg_readl(SDUH_reg_base + SDUH_IAS_REG))&0x1)== 1)
    {
		if(++retry >= SUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
			return 0;
		}
	}

	if(unlikely(regVal&0x2)) {
		BUG();
    spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
		return 0;
	}

    regVal = reg_readl(SDUH_reg_base + SDUH_RDATA_REG);
	spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);

    return regVal;
};
EXPORT_SYMBOL(SDUH_readl);


void init_sduh(struct usb_hcd *hcd)
{
		unsigned long temp;
		unsigned long board_id;

#ifdef CONFIG_PLAT_XCODE68xx
		board_id = reg_readl(CG_DUMMY_REG1 + XC_SOC_PROC_MMREG_BASE);
		//USB-2.0 use GPIO-6 for pwr enable on SDK board
		switch(board_id) {
		case 0x0030:
		case 0x0031:
		case 0x0032:
		case 0x0033:
            printk("Enable USB power through GPIO6\n");
            temp = reg_readl(GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
            temp |= (1 << 6);
            reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
            temp = reg_readl(GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
            temp |= (1 << 6);
            reg_writel(temp, GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
            udelay(100);
            temp &= ~(1 << 6);
            reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
            break;

		case 0x1100:
		case 0x1101:
		case 0x1102:
		case 0x1103:
		case 0x1104:
		case 0x1105:
		case 0x1106:
		case 0x1107:
		case 0x1108:
		case 0x1109:
		case 0x1140:
		case 0x1141:
        case 0x110d:
        case 0x110e:
        case 0x1110:
        case 0x1111:
        case 0x1112:
        case 0x1113:
        case 0x1150:
        case 0x1151:
        case 0x1152:
			printk("Enable USB power through GPIO6\n");
			temp = reg_readl(GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			temp |= (1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			temp = reg_readl(GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
			temp |= (1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
			udelay(100);
			temp &= ~(1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			break;

		case 0x0001:
		case 0x0005:
		case 0x1200:
		case 0x1201:
			printk("Enable USB power through GPIO6\n");
			temp = reg_readl(GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			temp &= ~(1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			temp = reg_readl(GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
			temp |= (1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUTEN + XC_SOC_PROC_MMREG_BASE);
			udelay(100);
			temp |= (1 << 6);
			reg_writel(temp, GPIO_DEDICATED_OUT + XC_SOC_PROC_MMREG_BASE);
			break;
		}
#endif

#ifndef EHCI_FPGA_BUILD
		//reset host controll
		dbg("try reset SDUH controller\n");
#ifdef CONFIG_PLAT_XCODE64xx
		temp = reg_readl(ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);
		temp |= USBH_RESET_MASK | USBD_RESET_MASK;
		reg_writel(temp, ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);
		udelay(1000);
		temp &= ~(USBH_RESET_MASK | USBD_RESET_MASK);
		reg_writel(temp, ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);	
		udelay(1000);

		temp = reg_readl(ACC_BLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
		temp &= ~(USBH_BLK_STOP_MASK);
		reg_writel(temp, ACC_BLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
		udelay(1000);

		temp = reg_readl(CG_CLK_SRC_SEL6 + XC_SOC_PROC_MMREG_BASE);
		temp &= ~CG_CLK_SRC_SEL6_UCLK_SRC_SEL_MASK;
		reg_writel(temp, CG_CLK_SRC_SEL6 + XC_SOC_PROC_MMREG_BASE);

		temp = reg_readl(CG_CLK_SRC_EN0 + XC_SOC_PROC_MMREG_BASE);
		temp |= CG_CLK_SRC_EN0_UCLK_SRC_EN_MASK;
		reg_writel(temp, CG_CLK_SRC_EN0 + XC_SOC_PROC_MMREG_BASE);

		temp = reg_readl(CG_CLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
		temp &= ~CG_CLK_STOP0_UCLK_STOP_MASK;
		reg_writel(temp, CG_CLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
		udelay(1000);
#else
        temp = reg_readl(ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);
        temp |= USBH_RESET_MASK;
        reg_writel(temp, ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);
        udelay(1000);
        temp &= ~(USBH_RESET_MASK);
        reg_writel(temp, ACC_RESET_REG0 + XC_SOC_PROC_MMREG_BASE);  
        udelay(1000);

        temp = reg_readl(ACC_BLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
        temp &= ~(USBH_BLK_STOP_MASK);
        reg_writel(temp, ACC_BLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
        udelay(1000);

        temp = reg_readl(CG1_CLK_SRC_SEL3 + XC_SOC_PROC_MMREG_BASE);
        temp &= ~CG1_CLK_SRC_SEL3_UCLK_SRC_SEL_MASK;
        reg_writel(temp, CG1_CLK_SRC_SEL3 + XC_SOC_PROC_MMREG_BASE);

        temp = reg_readl(CG1_CLK_SRC_EN0 + XC_SOC_PROC_MMREG_BASE);
        temp |= CG1_CLK_SRC_EN0_UCLK_SRC_EN_MASK;
        reg_writel(temp, CG1_CLK_SRC_EN0 + XC_SOC_PROC_MMREG_BASE);

        temp = reg_readl(CG1_CLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
        temp &= ~CG1_CLK_STOP0_UCLK_STOP_MASK;
        reg_writel(temp, CG1_CLK_STOP0 + XC_SOC_PROC_MMREG_BASE);
        udelay(1000);
#endif

#if 0
#if defined(CONFIG_USB_GADGET_XCODE)
		// Take the USBD out of soft reset
		temp = reg_readl(USBD_SOFT_RST+XC_SOC_PROC_MMREG_BASE);
		temp &= ~(PHY_SOFT_RST_MASK|UDC20_SOFT_RST_MASK);
		reg_writel(temp, (USBD_SOFT_RST+XC_SOC_PROC_MMREG_BASE));
		udelay(1000);
#endif
#endif

		//Configure the clock for PHY
		temp = 0x292;
#ifdef CONFIG_XC_USBH_USE_CRY_CLK /* external clock */
		//		temp |= (0<<PHY_REFCLKSEL_SHIFT);
#endif
#ifdef CONFIG_XC_USBH_USE_OSC_CLK /* external clock */
		//		temp |= (1<<PHY_REFCLKSEL_SHIFT);
#endif
#ifdef CONFIG_XC_USBH_USE_INTERNAL_PLL /* internal clock */
		//		temp = readl((void *)(CG_CLK_STOP1 + XC_SOC_PROC_MMREG_BASE));
		//		temp &= ~UCLK_STOP_MASK;
		//		writel(temp, (void *)(CG_CLK_STOP1 + XC_SOC_PROC_MMREG_BASE));
		//		temp = (2<<PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_12M
		//		temp |= (0<<PHY_REFCLKDIV_SHIFT);
#endif
#ifdef CONFIG_XC_USBH_USE_CLK_24M
		//		temp |= (1<<PHY_REFCLKDIV_SHIFT);
#endif
#ifdef CONFIG_XC_USBH_USE_CLK_48M
		//		temp |= (2<<PHY_REFCLKDIV_SHIFT);
#endif
#ifdef CONFIG_XC_USBH_USE_CLK_19M
		//		temp |= (3<<PHY_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_PLAT_XCODE64xx
		reg_writel(temp , USBD_PHY_CLK_CTRL+ XC_SOC_PROC_MMREG_BASE);
		udelay(1000);

		//Select USB PHY in host mode
		temp = reg_readl(USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
		temp &= 0xffffffef;
		temp |=0x3;

#ifdef CONFIG_USB_GADGET_XCODE
		reg_writel(temp|PHY_PORT0_H_D_MASK, USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
#else
		reg_writel(temp, USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
#endif
#endif

#endif
		udelay(1000);

		temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		temp &= ~(USBH_PHY_CTRL_REG_USBH_PHY_WORD_INTERFACE_0_MASK | USBH_PHY_CTRL_REG_USBH_PHY_WORD_INTERFACE_1_MASK);
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);

		// reset USBH PHY
		temp = reg_readl((u32)hcd->rsrc_start + SDUH_CONTROL_REG);
		temp |= USBH_CTRL_REG_SOFT_RESET_MASK | USBH_CTRL_REG_TSM_WR_MAX_WAIT_MASK;
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_CONTROL_REG);
		udelay(1000);

		// Take the USBH out of reset
		temp = reg_readl((u32)hcd->rsrc_start + SDUH_CONTROL_REG);
		temp &= ~USBH_CTRL_REG_SOFT_RESET_MASK;
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_CONTROL_REG);	
		udelay(1200);
		dbg("reset SDUH controller end\n");


		temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		temp |= (USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		udelay(1200);
		temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		temp &= ~(USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		udelay(1200);

#ifdef USE_IIA
		//enable usbh interrupt mask
		IIALocalSetMask(IIA_USBH_INT);
#endif
		dbg("phy register setting end\n");

		return;
}
EXPORT_SYMBOL(init_sduh);
