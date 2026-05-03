/**\file
 *  This file defines the wrapper for the platform/OS related functions
 *  The function definitions needs to be modified according to the platform 
 *  and the Operating system used.
 *  This file should be handled with greatest care while porting the driver
 *  to a different platform running different operating system other than
 *  Linux 2.6.xx.
 * \internal
 * ----------------------------REVISION HISTORY-----------------------------
 * Synopsys			01/Aug/2007			Created
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/serial.h>

#include <linux/delay.h>
#include <linux/delay.h>
#include <asm/types.h>
#include <asm/io.h>

#include "synopGMAC_plat.h"
#include "synopGMAC_Dev.h"

#define  AON_ETH0_CFG 0x16688
    #define AON_ETH0_CFG_INTF_SEL_MASK 0x00000001
    #define AON_ETH0_CFG_INTF_SEL_SHIFT 0
#define  AON_ETH1_CFG 0x1668C
	#define AON_ETH1_CFG_INTF_SEL_MASK 0x00000001
	#define AON_ETH1_CFG_INTF_SEL_SHIFT 0

/**
  * This is a wrapper function for Memory allocation routine. In linux Kernel 
  * it it kmalloc function
  * @param[in] bytes in bytes to allocate
  */

void *plat_alloc_memory(u32 bytes) 
{
	return kmalloc((size_t)bytes, GFP_KERNEL);
}

/**
  * This is a wrapper function for consistent dma-able Memory allocation routine. 
  * In linux Kernel, it depends on pci dev structure
  * @param[in] bytes in bytes to allocate
  */

void *plat_alloc_consistent_dmaable_memory(u32 size,u32 * dmaAddr) 
{	
	u32 addr;

	panic("ETH: %s not support now.\n", __func__);

	addr = (u32)kzalloc(size,GFP_KERNEL);
	*dmaAddr = addr & 0x7fffffff;
	return (void *) addr;
}
/**
  * This is a wrapper function for freeing consistent dma-able Memory.
  * In linux Kernel, it depends on pci dev structure
  * @param[in] bytes in bytes to allocate
  */

void plat_free_consistent_dmaable_memory(u32 size,void * addr) 
{
 //pci_free_consistent (pcidev,size,addr,dma_addr);
 return;
}
/**
  * This is a wrapper function for Memory free routine. In linux Kernel 
  * it it kfree function
  * @param[in] buffer pointer to be freed
  */
void plat_free_memory(void *buffer) 
{
	kfree(buffer);
	return ;
}


/**
  * This is a wrapper function for platform dependent delay 
  * Take care while passing the argument to this function 
  * @param[in] buffer pointer to be freed
  */
void plat_delay(u32 delay)
{
	//while (delay--);
	mdelay(delay/1000 + 1);
	return;
}

#define XC_LOWPOWER_HW_MUTEX		MIPS_INTERLOCK5
#define XC_LOWPOWER_ID_SHIFT		MIPS_INTERLOCK5_ID0_SHIFT
#define XC_LOWPOWER_ETH_DRV_ID	    (2)

void turnon_eth_clk()
{
	u32 mmreg_base = XC_SOC_PROC_MMREG_BASE;
	unsigned int tmp;

	unsigned long board_id;

	board_id = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1));

    // get mutex for low power register
	xc_writel((XC_LOWPOWER_ETH_DRV_ID << XC_LOWPOWER_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX));
	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	while(tmp != XC_LOWPOWER_ETH_DRV_ID) {
		msleep(1) ;
		xc_writel((XC_LOWPOWER_ETH_DRV_ID <<  XC_LOWPOWER_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX));
		tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	}

	printk("Turn on GTX clock\n");

	// turn eth0
	tmp = readl((unsigned long *)(ACC_BLK_STOP0 + mmreg_base));
 	tmp &= ~(ACC_BLK_STOP0_ETH0_BLK_STOP_MASK);
	writel(tmp, (unsigned long *)(ACC_BLK_STOP0 + mmreg_base));

#ifdef CONFIG_PLAT_XCODE64xx
	tmp = readl((unsigned long *)(CG1_CLK_STOP1 + mmreg_base));
	tmp &= ~(CG1_CLK_STOP1_ETHR0_REFCLK_STOP_MASK);
    tmp &= ~(CG1_CLK_STOP1_ETHR0_TXCLK_STOP_MASK);
	writel(tmp, (unsigned long *)(CG1_CLK_STOP1 + mmreg_base));
#else
    tmp = readl((unsigned long *)(CG1_CLK_STOP0 + mmreg_base));
    tmp &= ~(CG1_CLK_STOP0_ETHR0_REFCLK_STOP_MASK);
    tmp &= ~(CG1_CLK_STOP0_ETHR0_TXCLK_STOP_MASK);
    writel(tmp, (unsigned long *)(CG1_CLK_STOP0 + mmreg_base));
#endif

	writel(readl((unsigned long *)(ETH_CTRL_REG+ mmreg_base)) |  ETH_CTRL_REG_GTXCLK_SOURCE_MASK, (unsigned long *)(ETH_CTRL_REG+ mmreg_base));

#ifdef CONFIG_PLAT_XCODE64xx
	writel(readl((unsigned long *)(CG1_CLK_SRC_SEL8 + mmreg_base)) & ~(ETHR0_TXCLK_SRC_SEL_MASK), (unsigned long *)(CG1_CLK_SRC_SEL8+ mmreg_base));
#else
    writel(readl((unsigned long *)(CG1_CLK_SRC_SEL4 + mmreg_base)) & ~(ETHR0_TXCLK_SRC_SEL_MASK), (unsigned long *)(CG1_CLK_SRC_SEL4+ mmreg_base));
#endif

#ifdef CONFIG_PLAT_XCODE64xx
#if CONFIG_VIXS_SYNOPSYS_NUM_PORTS >=2
	// turn on eth 1
	tmp = readl((unsigned long *)(ACC_BLK_STOP0 + mmreg_base));
	tmp &= ~(ACC_BLK_STOP0_ETH1_BLK_STOP_MASK);
	writel(tmp, (unsigned long *)(ACC_BLK_STOP0 + mmreg_base));

	tmp = readl((unsigned long *)(CG1_CLK_STOP1 + mmreg_base));
	tmp &= ~(CG1_CLK_STOP1_ETHR1_REFCLK_STOP_MASK);
	tmp &= ~(CG1_CLK_STOP1_ETHR1_TXCLK_STOP_MASK);
	writel(tmp, (unsigned long *)(CG1_CLK_STOP1 + mmreg_base));

	writel(readl((unsigned long *)(ETH_CTRL_REG1+ mmreg_base)) |  ETH_CTRL_REG1_GTXCLK_SOURCE_MASK, (unsigned long *)(ETH_CTRL_REG1+ mmreg_base));
	writel(readl((unsigned long *)(CG1_CLK_SRC_SEL8 + mmreg_base)) & ~(ETHR1_TXCLK_SRC_SEL_MASK), (unsigned long *)(CG1_CLK_SRC_SEL8+ mmreg_base));
#else
	// stop eth 1. because eth 1 maybe turned on by uboot 
	tmp = readl((unsigned long *)(ACC_BLK_STOP0 + mmreg_base));
	tmp |= (ACC_BLK_STOP0_ETH1_BLK_STOP_MASK);
	writel(tmp, (unsigned long *)(ACC_BLK_STOP0 + mmreg_base));

	tmp = readl((unsigned long *)(CG1_CLK_STOP1 + mmreg_base));
	tmp |= (CG1_CLK_STOP1_ETHR1_REFCLK_STOP_MASK);
	tmp |= (CG1_CLK_STOP1_ETHR1_TXCLK_STOP_MASK);
	writel(tmp, (unsigned long *)(CG1_CLK_STOP1 + mmreg_base));

	writel(readl((unsigned long *)(ETH_CTRL_REG1+ mmreg_base)) |  ETH_CTRL_REG1_GTXCLK_SOURCE_MASK, (unsigned long *)(ETH_CTRL_REG1+ mmreg_base));
	writel(readl((unsigned long *)(CG1_CLK_SRC_SEL8 + mmreg_base)) & ~(ETHR1_TXCLK_SRC_SEL_MASK), (unsigned long *)(CG1_CLK_SRC_SEL8+ mmreg_base));
	
#endif
#endif

    // release mutex for low power register
	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	if(tmp == XC_LOWPOWER_ETH_DRV_ID){
		xc_writel((XC_LOWPOWER_ETH_DRV_ID& 0x0F) << XC_LOWPOWER_ID_SHIFT, XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX);
	}
	
    /*
    #ifdef LOW_POWER_MODE_ON

    //// For ETHERNET 
      //// When using Gigabit ETHERNET
    CG_CLK_STOP0 \ ETHR125CLK_STOP   
      // When using ETHERNET0
    CG_CLK_STOP0 \ ETHR0CLK_STOP   
    CG_BLK_CLK_STOP0 \ ETH0_MCLK_STOP   
    CG_BLK_CLK_STOP0 \ ETH0_SCLK_STOP   
      // When using ETHERNET1
    CG_CLK_STOP0 \ ETHR1CLK_STOP   
    CG_BLK_CLK_STOP0 \ ETH1_MCLK_STOP   
    CG_BLK_CLK_STOP0 \ ETH1_SCLK_STOP   
    ////////////////////////////////////

    #endif
	*/
}

void turnoff_eth_clk()
{
	//u32 mmreg_base = XC_SOC_PROC_MMREG_BASE;
    unsigned int tmp;
    
    // get mutex for low power register
	xc_writel((XC_LOWPOWER_ETH_DRV_ID << XC_LOWPOWER_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX));
	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	while(tmp != XC_LOWPOWER_ETH_DRV_ID) {
		msleep(1) ;
		xc_writel((XC_LOWPOWER_ETH_DRV_ID <<  XC_LOWPOWER_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX));
		tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	}
	
    /*
     * todo: turn off CG_CLK_STOP0, CG_BLK_CLK_STOP0
     */

    // release mutex for low power register
	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX) >> XC_LOWPOWER_ID_SHIFT) & 0x0F;
	if(tmp == XC_LOWPOWER_ETH_DRV_ID){
		xc_writel((XC_LOWPOWER_ETH_DRV_ID& 0x0F) << XC_LOWPOWER_ID_SHIFT, XC_SOC_PROC_MMREG_BASE + XC_LOWPOWER_HW_MUTEX);
	}	
}

/* Added by Stanley on Aug 11,2008*/
void __init plat_eth_init(void)
{
	unsigned long board_id;
    unsigned long reg;
	u32 mmreg_base = (u32)XC_SOC_PROC_MMREG_BASE;

	board_id = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1));

    /* Set Ethernet urgent bit, bit 14, on MC */
    reg = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + MC_CH0_ARB_MAIN_CTRL));
    reg |= 0x4000;
    writel(reg, (unsigned long *)(XC_SOC_PROC_MMREG_BASE + MC_CH0_ARB_MAIN_CTRL));
    reg = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + MC_CH1_ARB_MAIN_CTRL));
    reg |= 0x4000;
    writel(reg, (unsigned long *)(XC_SOC_PROC_MMREG_BASE + MC_CH1_ARB_MAIN_CTRL));
	
	// if u-boot reset ethernet, don't need to do it again
	//if (readl((unsigned long *)(ETH_CTRL_REG + mmreg_base)) & ETH_CTRL_REG_RESETN_MASK 
		//&& readl((unsigned long *)(ETH_CTRL_REG1 + mmreg_base)) & ETH_CTRL_REG_RESETN_MASK )
	if (readl((unsigned long *)(ETH_CTRL_REG + mmreg_base))  & ETH_CTRL_REG_RESETN_MASK) 
	{
		printk("skip ethernet reset\n");
		goto Skip_Ethernet_Reset;
	}

#ifdef USE_RGMII

#ifdef CONFIG_PLAT_XCODE64xx

	// eth0 port
	if((board_id & 0xff00) != 0x1700)
		writel(AON_ETH0_CFG_INTF_SEL_MASK | readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ AON_ETH0_CFG)) , (unsigned long *)(XC_SOC_PROC_MMREG_BASE + AON_ETH0_CFG));

	// eth 1 port
	switch (board_id & 0xff00) {
	case 0x1400:
		TR0("select MII interface for eth 1\n");
		writel( ( (~AON_ETH1_CFG_INTF_SEL_MASK) & readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ AON_ETH1_CFG))), (unsigned long *)(XC_SOC_PROC_MMREG_BASE + AON_ETH1_CFG));
		break;
	case 0x1100:
		break;	
	case 0x1700:
		break;
	default:
		TR0("select RGMII interface for eth 1\n");
		writel( (AON_ETH1_CFG_INTF_SEL_MASK | readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ AON_ETH1_CFG))), (unsigned long *)(XC_SOC_PROC_MMREG_BASE + AON_ETH1_CFG));
	}
#endif

#endif

Skip_Ethernet_Reset:   

	
	// set those shared pins for ethernet instead of gpio
	writel(readl((unsigned long *)(GPIO_B_CTRL + mmreg_base)) & ~GPIO_B_CTRL_GPIO_MODE_SEL_MASK, (unsigned long *)(GPIO_B_CTRL + mmreg_base));
#ifdef CONFIG_PLAT_XCODE64xx    
	writel(readl((unsigned long *)(GPIO_A_CTRL + mmreg_base)) & ~GPIO_A_CTRL_GPIO_MODE_SEL_MASK, (unsigned long *)(GPIO_A_CTRL + mmreg_base));
#endif
	
	// reset ethernet blocks
    writel(ETH0_RESET_MASK | readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0)), (unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0));
    writel(~ ETH0_RESET_MASK & readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0)), (unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0));
	
    writel(ETH1_RESET_MASK | readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0)), (unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0));
    writel(~ ETH1_RESET_MASK & readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0)), (unsigned long *)(XC_SOC_PROC_MMREG_BASE+ACC_RESET_REG0));

	// software reset ethernet 
    writel(ETH_CTRL_REG_RESETN_MASK, (unsigned int long *)(mmreg_base + ETH_CTRL_REG));

#ifdef CONFIG_PLAT_XCODE64xx
    // for eth1 port
	// this depends if phy uses external clk or XCode PLL
	if ((board_id & 0xff00) == 0x1400) // customer2 board
	{
		TR0("output 25MHz for ETHR1_TXCLK\n");
		writel( ((1<<ETHR1_TXCLK_SRC_SEL_SHIFT) | readl((unsigned long *)(mmreg_base+CG1_CLK_SRC_SEL8))), (unsigned long *)(mmreg_base + CG1_CLK_SRC_SEL8));
	}

    writel(ETH_CTRL_REG_RESETN_MASK, (unsigned int long *)(mmreg_base + ETH_CTRL_REG1));
#endif

#ifdef USE_RGMII

#ifdef CONFIG_PLAT_XCODE64xx
    if ((board_id & 0xff00) == 0x1100) // customer1 board
    {
        TR0("select MII interface \n");
    }else{
        writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) & ~ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK, \
            (unsigned long *)(mmreg_base + ETH_CTRL_REG));

        writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG1)) & ~ETH_CTRL_REG1_RGMII_FIFO_RSTN_MASK, \
            (unsigned long *)(mmreg_base + ETH_CTRL_REG1));
        
        writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) | \
            ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK | \
            ETH_CTRL_REG_INTF_SEL_MASK | \
            ETH_CTRL_REG_CLK_SHIFT_EN_MASK | \
            ETH_CTRL_REG_GTXCLK_SOURCE_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG));

        if ((board_id & 0xff00) == 0x1400) 
        {
            TR0("select MII interface for eth 1\n"); // customer2 board
        }
        else
        {
            writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG1)) | \
                ETH_CTRL_REG1_RGMII_FIFO_RSTN_MASK | \
                ETH_CTRL_REG1_INTF_SEL_MASK | \
                ETH_CTRL_REG1_CLK_SHIFT_EN_MASK | \
                ETH_CTRL_REG1_GTXCLK_SOURCE_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG1));
        }
    }
#else
    switch (board_id & 0xFFFF){
		case 0x1100:
		case 0x1101:
		case 0x1102:
		case 0x1103:
		case 0x1104:
		case 0x1140:
		case 0x1141:
        case 0x110d:
        case 0x110e:
        case 0x1150:
        case 0x1151:
        case 0x1152:
			TR0("select MII interface \n");
			break;

        case 0x1105:
        case 0x1106:
        case 0x1107:
        case 0x1108:
        case 0x1109:
        case 0x1110:
        case 0x1111:
        case 0x1112:
        case 0x1113:
            writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) & ~ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK, \
            (unsigned long *)(mmreg_base + ETH_CTRL_REG));
            writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) | \
            ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK | \
            ETH_CTRL_REG_INTF_SEL_MASK | \
            ETH_CTRL_REG_CLK_SHIFT_EN_MASK | \
            ETH_CTRL_REG_GTXCLK_SOURCE_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG));
            break;
        default:
                writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) & ~ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK, \
                (unsigned long *)(mmreg_base + ETH_CTRL_REG));
                writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) | \
                ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK | \
                ETH_CTRL_REG_INTF_SEL_MASK | \
                ETH_CTRL_REG_CLK_SHIFT_EN_MASK | \
                ETH_CTRL_REG_GTXCLK_SOURCE_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG));
			break;
    }
#endif

#endif
	
    writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG)) | \
			ETH_CTRL_REG_RX_DESC_CACHE_EN_MASK | \
			ETH_CTRL_REG_TX_DESC_CACHE_EN_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG));

#ifdef CONFIG_PLAT_XCODE64xx
    writel(readl((unsigned long *)(mmreg_base + ETH_CTRL_REG1)) | \
			ETH_CTRL_REG_RX_DESC_CACHE_EN_MASK | \
			ETH_CTRL_REG_TX_DESC_CACHE_EN_MASK, (unsigned long *)(mmreg_base + ETH_CTRL_REG1));
#endif

#ifdef CONFIG_PLAT_XCODE64xx
	if(((board_id & 0xFF00) ==0x1100) || ((board_id & 0xFF00) ==0x1400) ||
		((board_id & 0xFF00) ==0x1700 )	){
		writel(GPIO_DEDICATED_OUTEN7_MASK | readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUTEN)), 
				(unsigned long *)(XC_SOC_PROC_MMREG_BASE+GPIO_DEDICATED_OUTEN));
		writel( ~(GPIO_DEDICATED_OUT7_MASK) & readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT)), 
				(unsigned long *)(XC_SOC_PROC_MMREG_BASE+GPIO_DEDICATED_OUT));
	}
#endif

#ifdef CONFIG_PLAT_XCODE68xx
	if((board_id & 0xff00) == 0x1100)  {
		printk("%s:%d toggle GPIO10\n", __func__,__LINE__);
		mdelay(20);
		//reset ethernet phy by GPIO pin 10
		writel(readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUTEN)) | GPIO_DEDICATED_OUTEN10_MASK, \
				(unsigned long *)(XC_SOC_PROC_MMREG_BASE+GPIO_DEDICATED_OUTEN));

		writel(readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT)) & ~(GPIO_DEDICATED_OUT10_MASK), \
				(unsigned long *)(XC_SOC_PROC_MMREG_BASE+GPIO_DEDICATED_OUT));
		mdelay(20);
		writel(readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE+ GPIO_DEDICATED_OUT)) | GPIO_DEDICATED_OUT10_MASK, \
			(unsigned long *)(XC_SOC_PROC_MMREG_BASE+GPIO_DEDICATED_OUT));
		if ((board_id&0xffff) >= 0x1105) {
            mdelay(30);
        }else{
		    mdelay(10);
        }
	}
#endif

	return;
}




