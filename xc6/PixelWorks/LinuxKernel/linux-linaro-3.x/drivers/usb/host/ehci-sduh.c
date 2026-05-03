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
#include "asm/io.h"

#include <plat/xcodeRegDef.h>

#define dbg printk
/* FIXME: Power Managment is un-ported so temporarily disable it */
//#undef CONFIG_PM
#ifdef CONFIG_FPGA_BUILD
#define  EHCI_FPGA_BUILD
#endif
/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


static struct platform_device *sduh_ehci_device = NULL;
static struct resource sduh_ehci_resources[2];
extern spinlock_t sduh_usb_indirect_reg_lock;


/**
 * usb_hcd_sduh_probe - initialize Xcode3-based HCDs
 * @drvier: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
int usb_hcd_sduh_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	dev_info(&pdev->dev,"initializing XCode-SOC USB Controller\n");

    if (!pdev->dev.dma_mask)
        pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
    if (!pdev->dev.coherent_dma_mask)
        pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

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

	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
                        dev_name(&(pdev->dev)));
//			pdev->dev.bus_id);
		retval = -ENODEV;
		goto err2;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = res->end - res->start + 1;

	//indirect register access, set the base regs address to 0
	hcd->regs = 0;//ioremap(hcd->rsrc_start, hcd->rsrc_len);

       if(SDUH_HCD_REG_BASE == 0)
       {
#if 0
        	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
        				driver->description)) {
        		dev_dbg(&pdev->dev, "controller already in use\n");
        		retval = -EBUSY;
        		goto err2;
        	}
#endif

//To bypass ARC's MMU problem, don't use ioremap right now
//        	SDUH_HCD_REG_BASE = (u32)ioremap_nocache((unsigned int)hcd->rsrc_start, hcd->rsrc_len);
        	SDUH_HCD_REG_BASE = (u32)hcd->rsrc_start;
        	dbg("SDUH_HCD_REG_BASE is  %x \n", SDUH_HCD_REG_BASE);

        	if (SDUH_HCD_REG_BASE == 0) {
        		dev_dbg(&pdev->dev, "error mapping memory\n");
        		retval = -EFAULT;
        		goto err3;
        	}
            
       }
	retval = usb_add_hcd(hcd, irq, /*IRQF_DISABLED | */IRQF_SHARED);
	if (retval != 0)
		goto err4;

	printk("%s: SDUH add success\n", __FUNCTION__);
       SDUH_HCD_REF_CNT++;
       SDUH_HCD_ACTIVE_CNT++;
	return retval;

      err4:
	iounmap((void*)(SDUH_HCD_REG_BASE));
       SDUH_HCD_REG_BASE = 0;    
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
void usb_hcd_sduh_ehci_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
       SDUH_HCD_REF_CNT--;
       SDUH_HCD_ACTIVE_CNT--;
       if( SDUH_HCD_REF_CNT == 0)
        {
//        	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
        	iounmap((void*)(SDUH_HCD_REG_BASE));
              SDUH_HCD_REG_BASE = 0;
              SDUH_HCD_ACTIVE_CNT = 0;
        }
    
	usb_put_hcd(hcd);
}

/* called during probe() after chip reset completes */
static int ehci_sduh_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;
	u32			temp;
        unsigned long flags;

	if(SDUH_HCD_REF_CNT == 0)//first time
		init_sduh(hcd);
        
        spin_lock_irqsave(&sduh_usb_indirect_reg_lock, flags);
	temp = reg_readl((u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
	temp |= 0x2;
	reg_writel(temp, (u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
        spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
	dbg("enable interrupt mask end\n");

	/* EHCI registers start at offset 0x100 */
	dbg("try to read ehci cap register\n");

	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs +
	HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));
	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	dbg("try to read ehci params register\n");
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	//[jlwang] Since we are sharing USBH port 1 with USBD, I will hack the port number to 1 here
#ifdef CONFIG_USB_GADGET_XCODE
	ehci->hcs_params &= ~0xf;
	ehci->hcs_params |=1;
#endif


	dbg("try to call ehci_halt\n");
#if 0
	retval = ehci_halt(ehci);
#else

    ehci_writel(ehci, 0, &ehci->regs->intr_enable);

    if (ehci_is_TDI(ehci) && !tdi_in_host_mode(ehci)) {
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

	dbg("try to call ehci_init\n");
	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

//	ehci->is_tdi_rh_tt = 1;

	ehci->sbrn = 0x20;

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
static int ehci_sduh_suspend(struct usb_hcd *hcd, bool do_wakeup)
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

static int ehci_sduh_resume(struct usb_hcd *hcd, bool hibernated)
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


static const struct hc_driver ehci_sduh_hc_driver = {
	.description = hcd_name,
	.product_desc = "Vixs On-Chip EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_sduh_setup,
	.start = ehci_run,
#ifdef	CONFIG_PM
	.pci_suspend =		ehci_sduh_suspend,
	.pci_resume =		ehci_sduh_resume,
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
};

static int ehci_sduh_drv_probe(struct platform_device *pdev)
{        
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_sduh_probe(&ehci_sduh_hc_driver, pdev);
}

static int ehci_sduh_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_sduh_ehci_remove(hcd, pdev);

	return 0;
}

#ifdef	CONFIG_PM   
static int ehci_sduh_drv_suspend(struct platform_device * pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
    u32 temp;
    
	dbg("Enter EHCI suspend\n");

	SDUH_HCD_ACTIVE_CNT--;
	if(SDUH_HCD_ACTIVE_CNT == 0)
	{
		//temp = reg_readl(USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
		//reg_writel(temp | PHY_PORT0_SLEEPM_MASK | PHY_PORT1_SLEEPM_MASK, USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE); 
		//mdelay(4);

        temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		temp |= (USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
		reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
		mdelay(1);
        #ifdef CONFIG_PLAT_XCODE64xx
		temp = reg_readl(USBD_PHY_CLK_CTRL + XC_SOC_PROC_MMREG_BASE);
		reg_writel(0x292, USBD_PHY_CLK_CTRL+ XC_SOC_PROC_MMREG_BASE);
		mdelay(4);
        #endif
	}
	return 0;
}

static int ehci_sduh_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
    u32 temp;
    
	dbg("Enter EHCI resume\n");

	if(SDUH_HCD_ACTIVE_CNT == 0)
	{

//  USBD_PHY_CLK_CTRL = 0x00000012   //this is the reference source clock configuration
//  USBD_PHY_CTRL     = 0x14411043   //back to default value
//  USBD_PHY_TEST     = 0x00280000   //back to default value
//  USBD_SOFT_RST     = 0x00007fff   //soft reset USBPHY and USBD control logic
//  WAIT 100
//  USBD_SOFT_RST     = 0x00000000   //USBPHY and USBD control logic out of soft reset

		//Configure the clock for PHY 
#ifdef CONFIG_PLAT_XCODE64xx

            //Configure the clock for PHY 
            temp = 0x292;
#ifdef CONFIG_XC_USBH_USE_CRY_CLK
  //          temp |= (0<<PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_OSC_CLK
//            temp |= (1<<PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_12M
            temp |= (0<<PHY0_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_24M
            temp |= (1<<PHY0_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_48M
           temp |= (2<<PHY0_REFCLKDIV_SHIFT);
#endif
        reg_writel(temp , USBD_PHY_CLK_CTRL+ XC_SOC_PROC_MMREG_BASE);

        udelay(1000);

            //Select USB PHY in host mode
        temp = reg_readl(USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
		temp &= 0xffffffef;
		temp |= 0x3;

#ifdef CONFIG_USB_GADGET_XCODE
		reg_writel(temp|PHY_PORT0_H_D_MASK, USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
#else
		//writel(temp|(PHY_PORT0_H_D_MASK|PHY_PORT1_H_D_MASK), USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
		reg_writel(temp, USBD_PHY_CTRL + XC_SOC_PROC_MMREG_BASE);
#endif

		udelay(1000);
#endif

        temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
        temp |= (USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
        reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
        udelay(1000);
        temp = reg_readl((u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
        temp &= ~(USBH_PHY_POR_0_MASK|USBH_PHY_POR_1_MASK);
        reg_writel(temp, (u32)hcd->rsrc_start + SDUH_PHY_CONTROL_REG);
        udelay(1000);

        #ifdef CONFIG_PLAT_XCODE64xx
        reg_writel(0x00280000, USBD_PHY_TEST + XC_SOC_PROC_MMREG_BASE);
        udelay(1000);
        #endif

	}

	SDUH_HCD_ACTIVE_CNT++;
	return 0;
}
#endif

MODULE_ALIAS("sduh-ehci");

static struct platform_driver ehci_sduh_driver = {
    .probe = ehci_sduh_drv_probe,
    .remove = ehci_sduh_drv_remove,
    .shutdown = usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM       
    .suspend = ehci_sduh_drv_suspend,
    .resume = ehci_sduh_drv_resume,
#endif
    .driver = {
       .name = "sduh-ehci",
    },
};


static int sduh_ehci_init(void)
{
	int error;

	printk(KERN_INFO "Start: SDUH EHCI device registed to platform bus\n");

	sduh_ehci_device = platform_device_alloc("sduh-ehci", -1);
	if (!sduh_ehci_device) {
		error = -ENOMEM;
		return error;
	}

	sduh_ehci_device->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	//memory resources
	sduh_ehci_resources[0].start = SDUH_REG_BASE;
	sduh_ehci_resources[0].end = SDUH_REG_BASE + SDUH_REG_SIZE - 1;
	sduh_ehci_resources[0].flags = IORESOURCE_MEM;

	//irq resources
	sduh_ehci_resources[1].start = XCODE6_IRQ_USBH;
	sduh_ehci_resources[1].end = 0;
	sduh_ehci_resources[1].flags = IORESOURCE_IRQ;

	error = platform_device_add_resources(sduh_ehci_device, sduh_ehci_resources, 2);
	if (error)
		goto err_free_device;

	error = platform_device_add(sduh_ehci_device);
	if (error)
		goto err_free_device;

//	error = platform_driver_register(&ehci_sduh_driver);
//	if (error < 0)
//		goto err_free_device;

       spin_lock_init(&sduh_usb_indirect_reg_lock);

	printk(KERN_INFO "END: SDUH EHCI device registed to platform bus\n");

	return 0;
err_free_device:
	platform_device_put(sduh_ehci_device);
	return error;	
}

#if 0
static void  sduh_ehci_exit(void)
{
	if(sduh_ehci_device)
	{
		platform_device_del(sduh_ehci_device);
		sduh_ehci_device = NULL;
	}
}
#endif


