/*
 * xhci-plat.c - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * A lot of code borrowed from the Linux xHCI driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "asm/io.h"
#include "xhci.h"
#include "xhci-xcode.h"

#include <plat/xcodeRegDef.h>

static void xhci_plat_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * As of now platform drivers don't provide MSI support so we ensure
	 * here that the generic code does not try to make a pci_dev from our
	 * dev struct in order to setup MSI
	 */
	xhci->quirks |= XHCI_PLAT;
}

/* called during probe() after chip reset completes */
static int xhci_plat_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, xhci_plat_quirks);
}

static const struct hc_driver xhci_plat_xhci_driver = {
	.description =		"xhci-hcd",
	.product_desc =		"xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		xhci_plat_setup,
	.start =		xhci_run,
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/* Root hub support */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,
};

static int xhci_plat_probe(struct platform_device *pdev)
{
	const struct hc_driver	*driver;
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd;
	int			ret;
	int			irq;

	if (usb_disabled())
		return -ENODEV;

	driver = &xhci_plat_xhci_driver;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto put_hcd;
	}

#ifdef CONFIG_PLAT_XCODE64xx
	hcd->regs = 0;
#else
	hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto release_mem_region;
	}
#endif

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto unmap_registers;

	/* USB 2.0 roothub is stored in the platform_device now. */
	hcd = dev_get_drvdata(&pdev->dev);
	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(driver, &pdev->dev,
			dev_name(&pdev->dev), hcd);
	if (!xhci->shared_hcd) {
		ret = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	/*
	 * Set the xHCI pointer before xhci_plat_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (ret)
		goto put_usb3_hcd;

#ifdef CONFIG_PLAT_XCODE64xx
	xcode_xhci_irq_enable();
#endif

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

unmap_registers:
#ifndef CONFIG_PLAT_XCODE64xx
	iounmap(hcd->regs);
#endif

release_mem_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int xhci_plat_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);

	usb_remove_hcd(hcd);
#ifndef CONFIG_PLAT_XCODE64xx
	iounmap(hcd->regs);
#endif
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	kfree(xhci);

	return 0;
}

#ifdef	CONFIG_PM
static int xhci_plat_suspend(struct platform_device * pdev, pm_message_t state)
{
	struct usb_hcd	*hcd = platform_get_drvdata(pdev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
    u32 temp;
    
    printk("Enter XHCI suspend\n");
#ifdef CONFIG_PLAT_XCODE64xx 
    temp=xcode_xhci_readl((__le32 *)0xc200);
    temp|=0x80000000;
    xcode_xhci_writel(temp, (__le32 *)0xc200);

    temp=xcode_xhci_readl((__le32 *)0xc2c0);
    temp|=0x80000000;
    xcode_xhci_writel(temp, (__le32 *)0xc2c0);

    udelay(100);
#endif
    return xhci_suspend(xhci);
}

static int xhci_plat_resume(struct platform_device *pdev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(pdev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
    u32 temp;

    printk("Enter XHCI resume\n");
#ifdef CONFIG_PLAT_XCODE64xx 
    temp=xcode_xhci_readl((__le32 *)0xc200);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc200);

	temp=xcode_xhci_readl((__le32 *)0xc2c0);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc2c0);
    
    udelay(200);
#endif
	return xhci_resume(xhci, true);
}
#endif


static struct platform_driver usb_xhci_driver = {
	.probe	= xhci_plat_probe,
	.remove	= xhci_plat_remove,
#ifdef	CONFIG_PM    
    .suspend = xhci_plat_suspend,
    .resume = xhci_plat_resume,
#endif
	.driver	= {
		.name = "xhci-hcd",
	},
};
MODULE_ALIAS("platform:xhci-hcd");

extern int xcode_xhci_init(void);

int xhci_register_plat(void)
{
printk("%s %d\n", __FUNCTION__, __LINE__);
#ifdef CONFIG_PLAT_XCODE64xx
	xcode_xhci_init();
#endif
	return platform_driver_register(&usb_xhci_driver);
}

void xhci_unregister_plat(void)
{
	platform_driver_unregister(&usb_xhci_driver);
}
