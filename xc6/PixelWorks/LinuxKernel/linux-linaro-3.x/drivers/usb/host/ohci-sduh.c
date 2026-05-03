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
#include "../core/sduh-hcd.h"

#include <plat/xcodeRegDef.h>

#define dbg printk
#define err printk
/* FIXME: Power Managment is un-ported so temporarily disable it */
//#undef CONFIG_PM
#ifdef CONFIG_FPGA_BUILD
#define  EHCI_FPGA_BUILD
#endif
/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


static struct platform_device *sduh_ohci_device = NULL;
static struct resource sduh_ohci_resources[2];
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
int usb_hcd_sduh_ohci_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	dev_info(&pdev->dev,"initializing Xcode-SOC USB OHCI Controller\n");

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
                //not mapped yet, do it here
#if 0                
        	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
        				driver->description)) {
        		dev_dbg(&pdev->dev, "controller already in use\n");
        		retval = -EBUSY;
        		goto err2;
        	}
#endif

//To bypass ARC's MMU problem, don't use ioremap right now
//        	SDUH_HCD_REG_BASE = (u32)ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
        	SDUH_HCD_REG_BASE = (u32)hcd->rsrc_start;
        	dbg("SDUH_HCD_REG_BASE is  %x \n", SDUH_HCD_REG_BASE);

        	if (SDUH_HCD_REG_BASE == 0) {
        		dev_dbg(&pdev->dev, "error mapping memory\n");
        		retval = -EFAULT;
        		goto err3;
        	}
        }
       
	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval != 0)
		goto err4;

       SDUH_HCD_REF_CNT++;
       SDUH_HCD_ACTIVE_CNT++;
	return retval;

      err4:
	iounmap((void*)(SDUH_HCD_REG_BASE));
       SDUH_HCD_REG_BASE = 0;
      err3:
//	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
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
void usb_hcd_sduh_ohci_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
       SDUH_HCD_REF_CNT--;
       SDUH_HCD_ACTIVE_CNT--;
       if( SDUH_HCD_REF_CNT == 0)
        {
//        	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
        	iounmap((void*)(SDUH_HCD_REG_BASE));
              SDUH_HCD_REG_BASE = 0;
              SDUH_HCD_ACTIVE_CNT=0;
        }
       
	usb_put_hcd(hcd);
}

/* called during probe() after chip reset completes */
static int ohci_sduh_setup(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int retval;
	u32	temp;
        unsigned long flags;

        spin_lock_irqsave(&sduh_usb_indirect_reg_lock, flags);
	temp = reg_readl((u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
	temp |= 0x4;
	reg_writel(temp, (u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
        spin_unlock_irqrestore(&sduh_usb_indirect_reg_lock, flags);
	dbg("enable interrupt mask end");

	ohci->num_ports = 2;
	//[jlwang] Since we are sharing USBH port 1 with USBD, I will hack the port number to 1 here
#ifdef CONFIG_USB_GADGET_XCODE
	ohci->num_ports = 1;
#endif

	if ((retval = ohci_init(ohci)) < 0)
		return retval;

	if ((retval = ohci_run (ohci)) < 0) {
		err ("can't start %s", hcd->self.bus_name);
		ohci_stop (hcd);
		return retval;
	}
	printk("OHCI SDUH setup finished\n");

	return 0;

}


static void ohci_sduh_stop (struct usb_hcd *hcd)
{
       uint temp;
       ohci_stop(hcd);
    	temp = reg_readl((u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
      	temp &= ~0x4;
      	reg_writel(temp, (u32)hcd->rsrc_start + SDUH_HOST_INT_MASK_REG);
      	dbg("disable interrupt mask end");
    
}

#ifdef	CONFIG_PM

static int ohci_sduh_suspend (struct usb_hcd *hcd, bool do_wakeup)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	unsigned long	flags;
	int		rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave (&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	/* make sure snapshot being resumed re-enumerates everything */
	if (do_wakeup)
		ohci_usb_reset(ohci);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
 bail:
	spin_unlock_irqrestore (&ohci->lock, flags);
	return rc;
}


static int ohci_sduh_resume (struct usb_hcd *hcd, bool do_hibernate)
{
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_hcd_resume_root_hub(hcd);
	return 0;
}

#endif	/* CONFIG_PM */


static const struct hc_driver ohci_sduh_hc_driver = {
	.description = hcd_name,
	.product_desc = "Vixs On-Chip OHCI Host Controller",
	.hcd_priv_size = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start = ohci_sduh_setup,

	.stop = ohci_sduh_stop,
	.shutdown = ohci_shutdown,
#ifdef  CONFIG_PM
	.pci_suspend = ohci_sduh_suspend,
	.pci_resume = ohci_sduh_resume,
#endif

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,
        /* Removed in .30 */
//	.hub_irq_enable =	ohci_rhsc_enable,
#ifdef  CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

static int ohci_sduh_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_sduh_ohci_probe(&ohci_sduh_hc_driver, pdev);
}

static int ohci_sduh_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_sduh_ohci_remove(hcd, pdev);

	return 0;
}

#ifdef	CONFIG_PM
static int ohci_sduh_drv_suspend(struct platform_device * pdev, pm_message_t state)
{
    struct usb_hcd *hcd = platform_get_drvdata(pdev);
    u32 temp;
    
    dbg("Enter OHCI suspend\n");
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

static int ohci_sduh_drv_resume(struct platform_device *pdev)
{
    struct usb_hcd *hcd = platform_get_drvdata(pdev);
    u32 temp;
    
    dbg("Enter OHCI resume\n");
    
    if(SDUH_HCD_ACTIVE_CNT == 0)
    {

//  USBD_PHY_CLK_CTRL = 0x00000012   //this is the reference source clock configuration
//  USBD_PHY_CTRL     = 0x14411043   //back to default value
//  USBD_PHY_TEST     = 0x00280000   //back to default value
//  USBD_SOFT_RST     = 0x00007fff   //soft reset USBPHY and USBD control logic
//  WAIT 100
//  USBD_SOFT_RST     = 0x00000000   //USBPHY and USBD control logic out of soft reset


#ifdef CONFIG_PLAT_XCODE64xx

        //Configure the clock for PHY 

            //Configure the clock for PHY 
            temp = 0x292;
#ifdef CONFIG_XC_USBH_USE_CRY_CLK
//            temp |= (0<<PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_OSC_CLK
 //           temp |= (1<<PHY_REFCLKSEL_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_12M
   //         temp |= (0<<PHY_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_24M
     //       temp |= (1<<PHY_REFCLKDIV_SHIFT);
#endif

#ifdef CONFIG_XC_USBH_USE_CLK_48M
       //     temp |= (2<<PHY_REFCLKDIV_SHIFT);
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

MODULE_ALIAS("sduh-ohci");

static struct platform_driver ohci_sduh_driver = {
    .probe = ohci_sduh_drv_probe,
    .remove = ohci_sduh_drv_remove,
    .shutdown = usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM    
    .suspend = ohci_sduh_drv_suspend,
    .resume = ohci_sduh_drv_resume,
#endif

    .driver = {
    	   .name = "sduh-ohci",
    	   },
};


static int  sduh_ohci_init(void)
{
	int error = 0;

	printk(KERN_INFO "Start: SDUH OHCI device registed to platform bus\n");

	sduh_ohci_device = platform_device_alloc("sduh-ohci", -1);
	if (!sduh_ohci_device) {
		printk("%s failed, can't allocate platform device.\n", __func__);
		error = -ENOMEM;
		return error;
	}

	//memory resources
	sduh_ohci_resources[0].start = SDUH_REG_BASE;
	sduh_ohci_resources[0].end = SDUH_REG_BASE + SDUH_REG_SIZE - 1;
	sduh_ohci_resources[0].flags = IORESOURCE_MEM;

	//irq resources
	sduh_ohci_resources[1].start = XCODE6_IRQ_USBH;
	sduh_ohci_resources[1].end = 0;
	sduh_ohci_resources[1].flags = IORESOURCE_IRQ;

	error = platform_device_add_resources(sduh_ohci_device, sduh_ohci_resources, 2);
	if (error)
		goto err_free_device;

	error = platform_device_add(sduh_ohci_device);
	if (error)
		goto err_free_device;

//	error = platform_driver_register(&ohci_sduh_driver);
//	if (error < 0)
//		goto err_free_device;


	printk(KERN_INFO "END: SDUH OHCI device registed to platform bus\n");
/*	if(sduh_ohci_device)
	{
		platform_driver_unregister(&ohci_sduh_driver);
		platform_device_del(sduh_ohci_device);
		sduh_ohci_device = NULL;
	}
*/	
	return 0;
err_free_device:
	printk("%s failed, can't add platform device.\n", __func__);
	platform_device_put(sduh_ohci_device);
	return error;	
}

#if 0
static void  sduh_ohci_exit(void)
{
	if(sduh_ohci_device)
	{
//		platform_driver_unregister(&ohci_sduh_driver);
		platform_device_del(sduh_ohci_device);
		sduh_ohci_device = NULL;
	}
    
}
#endif
