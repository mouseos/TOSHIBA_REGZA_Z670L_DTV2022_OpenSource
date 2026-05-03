/*
* Enhanced Host Controller Driver for the Vixs Systems Xcode3 SDUH USB Host
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

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include <plat/xcode5_reg.h>

#include "../core/vusb-hcd.h"

/* FIXME: Power Managment is un-ported so temporarily disable it */
//#undef CONFIG_PM
#ifdef CONFIG_FPGA_BUILD
#define  EHCI_FPGA_BUILD
#endif
/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


static struct platform_device *vusb_ehci_device = NULL;
static struct resource vusb_ehci_resources[2];
extern spinlock_t vusb_usb_indirect_reg_lock;
extern void vusb_ohci_init_wakeup(void);

extern volatile unsigned int xc5_pci_dev_initialized;
extern spinlock_t xc5_register_lock;

//static struct device_dma_parameters __dma_parms ={
  //   .max_segment_size = 65536,	
//};
/**
 * usb_hcd_sduh_probe - initialize Xcode3-based HCDs
 * @drvier: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
int usb_hcd_vusb_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	printk("%s XCode5 USB host initialization!\n", __func__);	

    if (!pdev->dev.dma_mask)
        pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	

    if (!pdev->dev.coherent_dma_mask)
        pdev->dev.coherent_dma_mask =  g_XC_USBH_info.m_dma_mask;//DMA_BIT_MASK(32);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
                        dev_name(&(pdev->dev)));
//			pdev->dev.bus_id);
		return -ENODEV;
	}
	irq = res->start;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&(pdev->dev)));

	DBK("hcd:%p\n",hcd);
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}
	g_XC_USBH_info.mps_ehci_hcd=hcd;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
                        dev_name(&(pdev->dev)));
//			pdev->dev.bus_id);
		retval = -ENODEV;
		goto err2;
	}

//	hcd->rsrc_start = res->start;
	hcd->rsrc_start =  ( resource_size_t)(g_XC_USBH_info.mp_mmr+XC_USBH_CSR_STATUS_REG);//res->start;
	DBK(" hcd->rsrc_start:%p ,g_XC_USBH_info.mp_mmr:%p\n",(void*)hcd->rsrc_start,g_XC_USBH_info.mp_mmr);
	hcd->rsrc_len = res->end - res->start + 1;

	//indirect register access, set the base regs address to 0
	hcd->regs = 0;//ioremap(hcd->rsrc_start, hcd->rsrc_len);

       if(VUSB_HCD_REG_BASE == 0)
       {
#if 0
        	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
        				driver->description)) {
        		dbg(&pdev->dev, "controller already in use\n");
        		retval = -EBUSY;
        		goto err2;
        	}
#endif

//To bypass ARC's MMU problem, don't use ioremap right now
//        	VUSB_HCD_REG_BASE = (u32)ioremap_nocache((unsigned int)hcd->rsrc_start, hcd->rsrc_len);
        	VUSB_HCD_REG_BASE = (u32)hcd->rsrc_start;
        	DBK("VUSB_HCD_REG_BASE is  %x \n", VUSB_HCD_REG_BASE);

        	if (VUSB_HCD_REG_BASE == 0) {
			DBK_LOC;
        		retval = -EFAULT;
        		goto err3;
        	}
            
       }
	retval = usb_add_hcd(hcd, irq, /*IRQF_DISABLED | */IRQF_SHARED);
	if (retval != 0)
		goto err4;

	DBK("echi VUSB add success\n");
      	g_XC_USBH_info.mps_ehci=hcd_to_ehci(hcd);
       VUSB_HCD_REF_CNT++;
       VUSB_HCD_ACTIVE_CNT++;
	return retval;

      err4:
	//iounmap((void*)(VUSB_HCD_REG_BASE));
       VUSB_HCD_REG_BASE = 0;    
       err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
      err2:
	usb_put_hcd(hcd);
      err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", 
                dev_name(&(pdev->dev)),
                retval);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_sduh_remove - shutdown processing for Xcode3-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_fsl_probe().
 *
 */
void usb_hcd_vusb_ehci_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
       VUSB_HCD_REF_CNT--;
       VUSB_HCD_ACTIVE_CNT--;
       if( VUSB_HCD_REF_CNT == 0)
        {
//        	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
        	iounmap((void*)(VUSB_HCD_REG_BASE));
              VUSB_HCD_REG_BASE = 0;
              VUSB_HCD_ACTIVE_CNT = 0;
        }
    
	usb_put_hcd(hcd);
}

/* called during probe() after chip reset completes */
static int ehci_vusb_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;
	volatile u32 temp;
        unsigned long flags;

	DBK_LOC;
	if(VUSB_HCD_REF_CNT == 0)//first time
	{
		/* Setup USB PHY */
#ifndef EHCI_FPGA_BUILD
#define  USBD_PHY_OVERRIDE1 0x25F0
		//reset USB D firstl
		DBK("try reset ehci VUSB controller %p\n",g_XC_USBH_info.mp_mmr);
		spin_lock_irqsave(&xc5_register_lock, flags);
#ifdef CONFIG_XC_USBH_USE_INTERNAL_PLL
		temp = VMMR_READ(XC_CG_CLK_STOP1);
		temp &= ~XC_CG_CLK_STOP1_UCLK_STOP_MASK;
		VMMR_WRITE(XC_CG_CLK_STOP1, temp);
#endif
		temp = VMMR_READ(XC_CG_BLK_CLK_STOP2);
		temp &= ~(XC_CG_BLK_CLK_STOP2_USBH_MCLK_STOP_MASK | XC_CG_BLK_CLK_STOP2_USBH_SCLK_STOP_MASK);
		VMMR_WRITE(XC_CG_BLK_CLK_STOP2, temp);

		temp = VMMR_READ(XC_CG_RESET_REG);
		temp &= ~XC_CG_RESET_REG_USBD_RESET_MASK;
		VMMR_WRITE(XC_CG_RESET_REG, temp);	
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1000);

		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBD_PHY_OVERRIDE1);
		temp |= (0x3 << XC_USBD_PHY_OVERRIDE1_PHY_TXPREEMPHASISTUNE_SHIFT);
		VMMR_WRITE(USBD_PHY_OVERRIDE1, temp);

		temp = VMMR_READ(XC_USBD_PHY_OVERRIDE2);
		temp |= (0xf << XC_USBD_PHY_OVERRIDE2_PHY_TXVREFTUNE0_SHIFT);
		temp |= (0xf << XC_USBD_PHY_OVERRIDE2_PHY_TXVREFTUNE1_SHIFT);
		VMMR_WRITE(XC_USBD_PHY_OVERRIDE2, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1000);

		/* USB PHY depend on USBD, take the USBD out of reset */
		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBD_SOFT_RST);
		temp &= ~(XC_USBD_SOFT_RST_PHY_SOFT_RST_MASK|XC_USBD_SOFT_RST_UDC20_SOFT_RST_MASK);
		VMMR_WRITE(XC_USBD_SOFT_RST, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1000);

		/*
		 * Configure the clock for PHY
		 * Options are:
		 * 1. External Crystal
		 * 2. External Oscillator
		 * 3. Internal PLL
		 */
		temp = 0;
#if defined(CONFIG_XC_USBH_USE_CRY_CLK)
		/* external clock */
		temp |= (0<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKSEL_SHIFT);
#elif defined(CONFIG_XC_USBH_USE_OSC_CLK)
		/* external clock */
		temp |= (1<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKSEL_SHIFT);
#elif defined(CONFIG_XC_USBH_USE_INTERNAL_PLL)
		/* internal clock */
		temp = (2<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKSEL_SHIFT);
#endif

#if defined(CONFIG_XC_USBH_USE_CLK_12M)
		temp |= (0<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#elif defined(CONFIG_XC_USBH_USE_CLK_24M)
		temp |= (1<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#elif defined(CONFIG_XC_USBH_USE_CLK_48M)
		temp |= (2<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#elif defined(CONFIG_XC_USBH_USE_CLK_19M)
		temp |= (3<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#endif

		VMMR_WRITE(XC_USBD_PHY_CLK_CTRL, temp);
		udelay(1000);

		//Select USB PHY in host mode
		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBD_PHY_CTRL);
		temp &= ~(XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK|XC_USBD_PHY_CTRL_PHY_PORT1_H_D_MASK);

#if 1
		/* select host mode */
		temp |= XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK;
		VMMR_WRITE(XC_USBD_PHY_CTRL, temp);
#else
		temp |= (XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK|XC_USBD_PHY_CTRL_PHY_PORT1_H_D_MASK);
		VMMR_WRITE(USBD_PHY_CTRL, temp);
#endif
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1000);
#endif /*EHCI_FPGA_BUILD*/

		/* Reset USB host controller */
		DBK("try reset ehci VUSB controller g_XC_USBH_info.mp_mmr:%p \n",(void*)g_XC_USBH_info.mp_mmr);
		spin_lock_irqsave(&xc5_register_lock, flags);
		/* Set the MC ARB ungent bit for USB host controller */
		temp =  VMMR_READ(XC_MC_CH0_ARB_MAIN_CTRL);
		temp |= (1<<5);
		VMMR_WRITE(XC_MC_CH0_ARB_MAIN_CTRL, temp);
		temp =  VMMR_READ(XC_MC_CH1_ARB_MAIN_CTRL);
		temp |= (1<<5);
		VMMR_WRITE(XC_MC_CH1_ARB_MAIN_CTRL, temp);

		temp = VMMR_READ(XC_CG_RESET_REG);
		temp |= XC_CG_RESET_REG_USBH_RESET_MASK;
		VMMR_WRITE(XC_CG_RESET_REG, temp);
		udelay(1000);
		temp &= ~XC_CG_RESET_REG_USBH_RESET_MASK;
		VMMR_WRITE(XC_CG_RESET_REG, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);		
		udelay(1000);

	        DBK("hcd->rsrc_start:%p ,g_XC_USBH_info.mp_mmr:%p\n",(void*)hcd->rsrc_start,(void*)g_XC_USBH_info.mp_mmr);

		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBH_CTRL_REG);
		temp |= USBH_CTRL_REG_SOFT_RESET_MASK;
		VMMR_WRITE(XC_USBH_CTRL_REG, temp);
		DBK_LOC;
		// reset USBH PHY
#ifdef EHCI_FPGA_BUILD
		temp = VMMR_READ(XC_USBH_PHY_CTRL_REG);
		temp &= ~XC_USBH_PHY_CTRL_REG_USBH_PHY_WORD_INTERFACE_0_MASK;
		VMMR_WRITE(XC_USBH_PHY_CTRL_REG, temp);
#else		
		temp = VMMR_READ(XC_USBH_PHY_CTRL_REG);
		temp |= XC_USBH_PHY_CTRL_REG_USBH_PHY_POR_MASK;
		VMMR_WRITE(XC_USBH_PHY_CTRL_REG, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1200);

		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBH_PHY_CTRL_REG);		
		temp &= ~(XC_USBH_PHY_CTRL_REG_USBH_PHY_POR_MASK);
		VMMR_WRITE(XC_USBH_PHY_CTRL_REG, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1200);
#endif

		// Take the USBH out of reset
		spin_lock_irqsave(&xc5_register_lock, flags);
		temp = VMMR_READ(XC_USBH_CTRL_REG);
		temp &= ~XC_USBH_CTRL_REG_SOFT_RESET_MASK;
		VMMR_WRITE(XC_USBH_CTRL_REG, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);
		udelay(1200);
		printk("reset ehci vusb controller end\n");
	}
     
	/* EHCI registers start at offset 0x100 */
	DBK("try to read ehci cap register\n");

	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs +
	HC_LENGTH(ehci,ehci_readl(ehci, &ehci->caps->hc_capbase));
	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	DBK("try to read ehci params register\n");
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	//[jlwang] Since we are sharing USBH port 1 with USBD, I will hack the port number to 1 here
#if 1
	//def CONFIG_USB_GADGET_XCODE
	ehci->hcs_params &= ~0xf;
      ehci->hcs_params |=1;
#endif

	DBK_LOC;
#if 0 
	DBK("try to call ehci_halt");
	retval = ehci_halt(ehci);

#else
ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	spin_lock_irqsave(&xc5_register_lock, flags);		
	//enable usbh interrupt mask
	temp = VMMR_READ(XC_USBH_HOST_INT_MASK);
	temp |= XC_USBH_HOST_INT_MASK_EHCI_INT_STATUS_EN_MASK;
	VMMR_WRITE(XC_USBH_HOST_INT_MASK, temp);
	temp = VMMR_READ(XC_HOST_INTERRUPT_MASK);
	temp |= XC_HOST_INTERRUPT_MASK_USBH_INT_MASK;
	VMMR_WRITE(XC_HOST_INTERRUPT_MASK, temp);
	spin_unlock_irqrestore(&xc5_register_lock, flags);
	DBK("enable interrupt mask end");

    if (ehci_is_TDI(ehci) && !tdi_in_host_mode(ehci)) {
	DBK_LOC;
        return 0;
    }

    ehci->command &= ~CMD_RUN;
    temp = ehci_readl(ehci, &ehci->regs->command);
    temp &= ~(CMD_RUN | CMD_IAAD);
    ehci_writel(ehci, temp, &ehci->regs->command);

    synchronize_irq(ehci_to_hcd(ehci)->irq);

    retval = handshake(ehci, &ehci->regs->status,
              STS_HALT, STS_HALT, 16 * 125);
#endif
    	if (retval)
		return retval;
	
	DBK("try to call ehci_init\n");
	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	//ehci->is_tdi_rh_tt = 1;

	ehci->sbrn = 0x20;
//	ehci->use_dummy_qh=0;
	ehci_reset(ehci);

	return retval;
}




#ifdef	CONFIG_PM

/* suspend/resume, section 4.3 */

/* These routines rely on the PCI bus glue
 * to handle powerdown and wakeup, and currently also on
 * transceivers that don't need any software attention to set up
 * the right sort of wakeup.
 * Also they depend on separate root hub suspend/resume.
 */
static int ehci_vusb_suspend(struct usb_hcd *hcd, bool do_wakeup)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	unsigned long		flags;
	int			rc = 0;

	if (time_before(jiffies, ehci->next_statechange))
		msleep(10);

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ehci_writel (ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);

	/* make sure snapshot being resumed re-enumerates everything */
	if (do_wakeup)
	{
		ehci_halt(ehci);
		ehci_reset(ehci);
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
 bail:
	spin_unlock_irqrestore (&ehci->lock, flags);

	// could save FLADJ in case of Vaux power loss
	// ... we'd only use it to handle clock skew

	return rc;
}

static int ehci_vusb_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	unsigned		port;
	int			retval = -EINVAL;

	// maybe restore FLADJ

	if (time_before(jiffies, ehci->next_statechange))
		msleep(100);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	/* If CF is clear, we lost PCI Vaux power and need to restart.  */
	if (ehci_readl(ehci, &ehci->regs->configured_flag) != FLAG_CF)
		goto restart;

	/* If any port is suspended (or owned by the companion),
	 * we know we can/must resume the HC (and mustn't reset it).
	 * We just defer that to the root hub code.
	 */
	for (port = HCS_N_PORTS(ehci->hcs_params); port > 0; ) {
		u32	status;
		port--;
		status = ehci_readl(ehci, &ehci->regs->port_status [port]);
		if (!(status & PORT_POWER))
			continue;
		if (status & (PORT_SUSPEND | PORT_RESUME | PORT_OWNER)) {
			usb_hcd_resume_root_hub(hcd);
			return 0;
		}
	}

restart:
	ehci_dbg(ehci, "lost power, restarting\n");
	usb_root_hub_lost_power(hcd->self.root_hub);

	/* Else reset, to cope with power loss or flush-to-storage
	 * style "resume" having let BIOS kick in during reboot.
	 */
	(void) ehci_halt(ehci);
	(void) ehci_reset(ehci);
//	ehci_port_power(ehci, 0);

	/* emptying the schedule aborts any urbs */
	spin_lock_irq(&ehci->lock);
	end_unlink_async(ehci);
	ehci_work(ehci);
	spin_unlock_irq(&ehci->lock);

	/* restart; khubd will disconnect devices */
	retval = ehci_run(hcd);

	/* here we "know" root ports should always stay powered */
//	ehci_port_power(ehci, 1);

	return retval;
}
#endif
void  vusb_unmap_urb_for_dma (struct usb_hcd *hcd, struct urb *urb){
  if (urb->setup_dma)
     	DBK("urb->setup_dma:%x\n",urb->setup_dma);

    vusb_data_buf_free(urb->setup_dma);
    urb->setup_dma = urb->setup_dma_hack;  
}


static const struct hc_driver ehci_vusb_hc_driver = {
	.description = hcd_name,
	.product_desc = "Vixs EHCI VUSB Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2, //HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_vusb_setup,
	.start = ehci_run,
#ifdef	CONFIG_PM
	.pci_suspend =		ehci_vusb_suspend,
	.pci_resume =		ehci_vusb_resume,
#endif
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
#ifdef	CONFIG_PM   	
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
#endif	
//	.unmap_urb_for_dma = vusb_unmap_urb_for_dma,
};

static int ehci_vusb_drv_probe(struct platform_device *pdev)
{        
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_vusb_probe(&ehci_vusb_hc_driver, pdev);
}

static int ehci_vusb_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_vusb_ehci_remove(hcd, pdev);

	return 0;
}

#ifdef	CONFIG_PM   
static int ehci_vusb_drv_suspend(struct platform_device * pdev, pm_message_t state)
{
    
	DBK("Enter EHCI suspend\n");

	VUSB_HCD_ACTIVE_CNT--;
	if(VUSB_HCD_ACTIVE_CNT == 0)
	{
		u32 temp;

		temp = VMMR_READ(XC_USBD_PHY_CTRL);
		temp |= (XC_USBD_PHY_CTRL_PHY_PORT0_RESET_MASK | XC_USBD_PHY_CTRL_PHY_PORT1_RESET_MASK);
		VMMR_WRITE(XC_USBD_PHY_CTRL, temp); 

//		udelay(4000);
		temp = VMMR_READ(XC_USBD_PHY_CLK_CTRL);
		/* select external clock */
		temp |= 0x04;
		VMMR_WRITE(XC_USBD_PHY_CLK_CTRL, temp);
//		udelay(4000);
	}
	return 0;
}

static int ehci_vusb_drv_resume(struct platform_device *pdev)
{
       u32 temp;
    
	DBK("Enter EHCI vusb resume\n");

	if(VUSB_HCD_ACTIVE_CNT == 0)
	{

//  USBD_PHY_CLK_CTRL = 0x00000012   //this is the reference source clock configuration
//  USBD_PHY_CTRL     = 0x14411043   //back to default value
//  USBD_PHY_TEST     = 0x00280000   //back to default value
//  USBD_SOFT_RST     = 0x00007fff   //soft reset USBPHY and USBD control logic
//  WAIT 100
//  USBD_SOFT_RST     = 0x00000000   //USBPHY and USBD control logic out of soft reset

	   //Configure the clock for PHY 
            temp = 0;
#ifdef CONFIG_XC_USBH_USE_CRY_CLK
            temp |= (0<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_OSC_CLK
            temp |= (1<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_12M
            temp |= (0<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_24M
            temp |= (1<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_48M
            temp |= (2<<XC_USBD_PHY_CLK_CTRL_PHY_REFCLKDIV_SHIFT);
#endif
		VMMR_WRITE(XC_USBD_PHY_CLK_CTRL, temp);

            udelay(1000);

            //Select USB PHY in host mode
		temp = VMMR_READ(XC_USBD_PHY_CTRL);
		temp &= ~(XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK|XC_USBD_PHY_CTRL_PHY_PORT1_H_D_MASK);

#if 1 
		//def CONFIG_USB_GADGET_XCODE
		temp |= XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK;
		VMMR_WRITE(XC_USBD_PHY_CTRL, temp);
#else
		temp |= (XC_USBD_PHY_CTRL_PHY_PORT0_H_D_MASK|XC_USBD_PHY_CTRL_PHY_PORT1_H_D_MASK);
		VMMR_WRITE(XC_USBD_PHY_CTRL, temp);
#endif

		udelay(1000);

		VMMR_WRITE(0x00280000, XC_USBD_PHY_TEST);
            udelay(1000);

	}


	VUSB_HCD_ACTIVE_CNT++;
	return 0;
}
#endif

MODULE_ALIAS("vusb-ehci");

static struct platform_driver ehci_vusb_driver = {
    .probe = ehci_vusb_drv_probe,
    .remove = ehci_vusb_drv_remove,
    .shutdown = usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM       
    .suspend = ehci_vusb_drv_suspend,
    .resume = ehci_vusb_drv_resume,
#endif
    .driver = {
       .name = "vusb-ehci",
    },
};
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
static void vusb_ehci_wq_handler(struct work_struct *w)
{
	int error;
	
        if( XC_USBH_Global_Struct_Init()){
		printk("vusb_ehci_init.....................init error\n");
            return;
        }
        
        vusb_ehci_device = platform_device_alloc("vusb-ehci", -1);
	if (!vusb_ehci_device) {
		DBK_LOC;
		return;
	}

//	vusb_ehci_device->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	//memory resources
	vusb_ehci_resources[0].start = (u32) g_XC_USBH_info.mp_physmmr + 0x2000;
	vusb_ehci_resources[0].end = vusb_ehci_resources[0].start + VUSB_REG_SIZE - 1;
	vusb_ehci_resources[0].flags =(u32) IORESOURCE_MEM;

	//irq resources
	vusb_ehci_resources[1].start =  g_XC_USBH_info.m_pci_int_line;
	vusb_ehci_resources[1].end = 0;
	vusb_ehci_resources[1].flags = IORESOURCE_IRQ;

	error = platform_device_add_resources(vusb_ehci_device, vusb_ehci_resources, 2);
	if (error)
		goto err_free_device;

	error = platform_device_add(vusb_ehci_device);
	if (error)
		goto err_free_device;

//	error = platform_driver_register(&ehci_vusb_driver);
//	if (error < 0)
//		goto err_free_device;
	DBK(KERN_INFO "END: SDUH EHCI device registed to platform bus\n");

	xc5_pci_dev_initialized = 4;
	vusb_ohci_init_wakeup();
        kfree(w);
	return ;
err_free_device:
	platform_device_put(vusb_ehci_device);
	/* let OHCI init run */
	xc5_pci_dev_initialized = 4;
	vusb_ohci_init_wakeup();
	DBK_LOC;
        kfree(w);
	return;	

}

static int vusb_ehci_init(void)
{
        int ret=-1;

#ifndef CONFIG_VIRTUAL_XC5_SATA
	spin_lock_init(&g_xcode5_dmab_lock);
	spin_lock_init(&g_xcode5_dmab_lock);
#endif
        struct work_struct* work = ( struct work_struct *)kmalloc(sizeof( struct work_struct), GFP_KERNEL);
	if(work){
		INIT_WORK( (struct work_struct *)work, vusb_ehci_wq_handler );
	    ret = schedule_work((struct work_struct*) work);
	}

        return 0;
}




