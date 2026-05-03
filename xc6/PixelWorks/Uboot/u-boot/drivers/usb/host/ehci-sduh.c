/*
 * SDUH USB HOST EHCI Controller
 *
 * Copyright(C) 2015 Vixs Systems Limited
 * http://www.Vixs.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <common.h>
#include <usb.h>
#include <asm/arch/cpu.h>
#include <asm/arch/ehci.h>
#include <asm/arch-xc6/xcodeRegDef.h>
#include "ehci.h"

#define reg_writel(b,addr) (*(volatile unsigned int *) (addr) = (b))
#define reg_readl(addr) (*(volatile unsigned int *) (addr))

#ifdef DEBUG
#define dbg(format, arg...) printf("DEBUG: " format "\n", ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif /* DEBUG */

u32 SDUH_writel(u32 ehci_reg_addr, u32 value, SDUH_HC_reg_type type)
{
    u32 regVal = 0;

    while(((regVal = reg_readl(SDUH_REG_BASE + SDUH_IAS_REG))&0x1)== 1){};

    reg_writel(value, SDUH_REG_BASE + SDUH_WDATA_REG);
    switch(type)
    {
        case EHCI_REG:
            reg_writel(ehci_reg_addr|0x80000000, SDUH_REG_BASE + SDUH_EOC_REG);
            break;
        case OHCI_REG:
            reg_writel(ehci_reg_addr|0x80000000, SDUH_REG_BASE + SDUH_OOC_REG);
            break;
        default:
            return 0;
            break;
    }
    while(((regVal = reg_readl(SDUH_REG_BASE + SDUH_IAS_REG))&0x1)== 1){};

    if(regVal&0x2) { return 0;};

    return 0;
};
EXPORT_SYMBOL(SDUH_writel);

u32 SDUH_readl(u32 ehci_reg_addr, SDUH_HC_reg_type type)
{
    u32 regVal = 0;

    while(((regVal = reg_readl(SDUH_REG_BASE + SDUH_IAS_REG))&0x1)== 1);

    switch(type)
    {
    case EHCI_REG:
    	reg_writel(ehci_reg_addr, SDUH_REG_BASE + SDUH_EOC_REG);
    	break;
    case OHCI_REG:
    	reg_writel(ehci_reg_addr, SDUH_REG_BASE + SDUH_OOC_REG);
                    break;
    default:
    	 return 0;
    	 break;
    }
    while(((regVal = reg_readl(SDUH_REG_BASE + SDUH_IAS_REG))&0x1)== 1);

    if(regVal&0x2) { return 0;};

    regVal = reg_readl(SDUH_REG_BASE + SDUH_RDATA_REG);

    return regVal;
};
EXPORT_SYMBOL(SDUH_readl);

/*
 * EHCI-initialization
 * Create the appropriate control structures to manage
 * a new EHCI host controller.
 */
int ehci_hcd_init(int index, struct ehci_hccr **hccr, struct ehci_hcor **hcor)
{
    unsigned long temp;
    unsigned long board_id;
    
    board_id = reg_readl(CG_DUMMY_REG1 + XC_SOC_PROC_MMREG_BASE);
    //USB-2.0 use GPIO-6 for pwr enable on Eng and SDK board
	switch (board_id) {
	case 0x0030:
	case 0x0031:
	case 0x0032:
	case 0x0033:
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
        case 0x110d:
        case 0x110e:
	case 0x1140:
	case 0x1141:
	case 0x1110:
	case 0x1111:
	case 0x1112:
	case 0x1113:
	case 0x1150:
	case 0x1151:
        case 0x1152:
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

	case 0x1200:
	case 0x1201:
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

	default:
		printf("Unknown board id 0x%04x\n", board_id);
		break;
	}

    //reset host controll
    dbg("try reset SDUH controller\n");
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

    temp = reg_readl(SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);
    temp &= ~(USBH_PHY_CTRL_REG_USBH_PHY_WORD_INTERFACE_0_MASK | USBH_PHY_CTRL_REG_USBH_PHY_WORD_INTERFACE_1_MASK);
    reg_writel(temp, SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);

    // reset USBH PHY
    temp = reg_readl(SDUH_REG_BASE + SDUH_CONTROL_REG);
    temp |= USBH_CTRL_REG_SOFT_RESET_MASK | USBH_CTRL_REG_TSM_WR_MAX_WAIT_MASK;
    reg_writel(temp, SDUH_REG_BASE + SDUH_CONTROL_REG);
    udelay(1000);

    // Take the USBH out of reset
    temp = reg_readl(SDUH_REG_BASE + SDUH_CONTROL_REG);
    temp &= ~USBH_CTRL_REG_SOFT_RESET_MASK;
    reg_writel(temp, SDUH_REG_BASE + SDUH_CONTROL_REG);  
    udelay(1000);
    dbg("reset SDUH controller end\n");

    temp = reg_readl(SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);
    temp |= (USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
    reg_writel(temp, SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);
    udelay(1000);
    temp = reg_readl(SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);
    temp &= ~(USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
    reg_writel(temp, SDUH_REG_BASE + SDUH_PHY_CONTROL_REG);
    udelay(1000);

    *hccr = (struct ehci_hccr *)0;
	*hcor = (struct ehci_hcor *)((uint32_t)*hccr +
			HC_LENGTH(ehci_readl(&(*hccr)->cr_capbase)));
    
	return 0;
}

/*
 * Destroy the appropriate control structures corresponding
 * the EHCI host controller.
 */
int ehci_hcd_stop(int index)
{
	return 0;
}
