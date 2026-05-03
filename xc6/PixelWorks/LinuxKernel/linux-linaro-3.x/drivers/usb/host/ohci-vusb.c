
#include <linux/module.h>  /* Needed by all modules */
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/platform_device.h>
#include <asm/io.h>
#include <plat/xcode5_reg.h>
#include "../core/vusb-hcd.h"
#include "ohci.h"

/* FIXME: Power Managment is un-ported so temporarily disable it */
//#undef CONFIG_PM
//#define  EHCI_FPGA_BUILD
/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


extern struct usb_hcd *usb_create_hcd(const struct hc_driver *driver,struct device *dev, const char *bus_name);
extern int ohci_init (struct ohci_hcd *ohci);
extern int ohci_run (struct ohci_hcd *ohci);

static struct platform_device *vusb_ohci_device = NULL;
static struct resource vusb_ohci_resources[2];

/* OHCI init is schedukled as a delayed work, after EHCI init */
static void vusb_ohci_wq_handler(struct work_struct *w);
static struct workqueue_struct *vusb_wq = NULL;
static DECLARE_DELAYED_WORK(vusb_ohci_work, vusb_ohci_wq_handler);
extern void vusb_ohci_init_wait(void);
extern volatile unsigned int xc5_pci_dev_initialized;
extern spinlock_t xc5_register_lock;

/* called during probe() after chip reset completes */
static int ohci_vusb_setup(struct usb_hcd *hcd)
{
	unsigned long flags;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int retval;
	volatile u32 temp;	

	DBK("phy register setting end\n");
	spin_lock_irqsave(&xc5_register_lock, flags);
	temp = VMMR_READ(XC_USBH_HOST_INT_MASK);
    temp |= XC_USBH_HOST_INT_MASK_OHCI_INT_STATUS_EN_MASK;
	VMMR_WRITE(XC_USBH_HOST_INT_MASK, temp);
	spin_unlock_irqrestore(&xc5_register_lock, flags);

   	DBK("enable interrupt mask end XC_USBH_HOST_INT_MASK:%x :%x\n",XC_USBH_HOST_INT_MASK,temp);

	ohci->num_ports = 2;
	g_XC_USBH_info.mps_hcd=hcd;
	 DBK_LOC;
	if ((retval = ohci_init(ohci)) < 0){
		DBK("vusb ochi_init() error:%d\n",retval);
		return retval;
	}

	 DBK_LOC;
	if ((retval = ohci_run (ohci)) < 0) {
	 DBK_LOC;
		ohci_stop (hcd);
		return retval;
	}
	 DBK_LOC;
	return 0;

}

static const struct hc_driver ohci_vusb_hc_driver = {
	.description = hcd_name,
	.product_desc = "Vixs VUSB Host Controller",
	.hcd_priv_size = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start = ohci_vusb_setup,

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

static int usb_hcd_vusb_ohci_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	DBK("initializing Xcode5 VUSB Controller pdev:%p\n",pdev);
 if (!pdev->dev.dma_mask)
        pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	

    if (!pdev->dev.coherent_dma_mask)
        pdev->dev.coherent_dma_mask =  g_XC_USBH_info.m_dma_mask;//DMA_BIT_MASK(32);
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	DBK("res:%p\n",res);
	if (!res) {
		DBK(	"Found HC with no IRQ. Check %s setup!\n",
			pdev->dev.id);
		return -ENODEV;
	}
	irq = res->start;

	hcd = usb_create_hcd(driver, &pdev->dev,dev_name(&(pdev->dev)));
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}
       
      // 	g_XC_USBH_info.mps_hcd=hcd;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&(pdev->dev)));
		retval = -ENODEV;
		goto err2;
	}

	hcd->rsrc_start = (resource_size_t) g_XC_USBH_info.mp_mmr+XC_USBH_CSR_STATUS_REG;//res->start;
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
        		DBK("error mapping memory\n");
        		retval = -EFAULT;
        		goto err3;
        	}
        }
       
	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval != 0)
		goto err4;

       VUSB_HCD_ACTIVE_CNT++;
	return retval;

      err4:
//	iounmap((void*)(VUSB_HCD_REG_BASE));
      err3:
//	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
      err2:
	usb_put_hcd(hcd);
      err1:
	DBK("init %s fail, %d\n",      dev_name(&(pdev->dev)),  retval);
	return retval;
}

static int ohci_vusb_drv_probe(struct platform_device *pdev)
{
	DBK_LOC;
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_vusb_ohci_probe(&ohci_vusb_hc_driver, pdev);
}

static struct platform_driver ohci_vusb_driver = {
    .probe = ohci_vusb_drv_probe,
    .driver = {
    	   .name = "vusb-ohci",
    	   },
#if 0
    .remove = ohci_sduh_drv_remove,
    .shutdown = usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM    
    .suspend = ohci_sduh_drv_suspend,
    .resume = ohci_sduh_drv_resume,
#endif

#endif
};

static void vusb_ohci_wq_handler(struct work_struct *w)
{
    int error;

	vusb_ohci_init_wait();

	if(unlikely(xc5_pci_dev_initialized < 4))
		printk("%s, Warning the xc5_pci_dev_init state is %d, should be 4\n", __func__, xc5_pci_dev_initialized);

    vusb_ohci_device = platform_device_alloc("vusb-ohci", -1);
    if (!vusb_ohci_device) {
      printk("vusb_ohci_init.....................no memory\n");
      return ;
    }
    DBK("vusb_ohci_device:%p\n",vusb_ohci_device);

    //memory resources
    vusb_ohci_resources[0].start = (resource_size_t)(g_XC_USBH_info.mp_physmmr + 0x2000);
    vusb_ohci_resources[0].end = vusb_ohci_resources[0].start + VUSB_REG_SIZE - 1;
    vusb_ohci_resources[0].flags = IORESOURCE_MEM;

    //irq resources
    vusb_ohci_resources[1].start = g_XC_USBH_info.m_pci_int_line;;
    vusb_ohci_resources[1].end = 0;
    vusb_ohci_resources[1].flags = IORESOURCE_IRQ;
    
    error = platform_device_add_resources(vusb_ohci_device, vusb_ohci_resources, 2);
    if (error)
	goto err_free_device;

    error = platform_device_add(vusb_ohci_device);
    if (error)
	goto err_free_device;
    
    DBK(KERN_INFO "END: VUSB OHCI device registed to platform bus \n");
	xc5_pci_dev_initialized = 5;
	return;

err_free_device:
	printk("END: vusb_ohci_init error\n");
	xc5_pci_dev_initialized = 5;
	platform_device_put(vusb_ohci_device);
	return;		
}

static int  vusb_ohci_init(void)
{
     if (!vusb_wq)
                vusb_wq = create_singlethread_workqueue("vusb_ohci_wq");

     DBK("vusb_ohci_init vusb_wq:%p\n",vusb_wq);
	if (vusb_wq) {
		queue_delayed_work(vusb_wq, &vusb_ohci_work, msecs_to_jiffies(10));
	} else {
		printk("%s failed, can't allocate delayed work.\n", __func__);
		return -1;		
	}

        return 0;
}
