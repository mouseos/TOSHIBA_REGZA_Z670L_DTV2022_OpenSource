#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include "asm/io.h"
#include "xhci.h"
#include "xhci-xcode.h"

#include <plat/xcodeRegDef.h>

static struct platform_device *xcode_xhci_device = NULL;
static struct resource xcode_xhci_resources[2];
spinlock_t xhci_xcode_reg_lock;

void xcode_xhci_irq_enable(void)
{
	xcode_setval(1, IP_INTERRUPT_EN, USBH30_INT_EN);
}

static int xcode_xhci_hw_init(void)
{
	u32 temp;
	u32 board_id;

	xcode_setval(1, USB30CLK_SRC_EN, CG_CLK_SRC_EN1);
	xcode_setval(0, USB30CLK_STOP, CG_CLK_STOP1);
	xcode_setval(0, USB30CLK_SRC_SEL, CG_CLK_SRC_SEL7);
	xcode_setval(0, USB3_ALTREFCLK_STOP, CG_BLK_MISC_CTRL0);
	xcode_setval(0, USBH30_BLK_STOP, ACC_BLK_STOP0);
	xcode_setval(1, USBH30_RESET, ACC_RESET_REG0);
	mdelay(10);
	xcode_setval(0, USBH30_RESET, ACC_RESET_REG0);
	mdelay(100);
	xcode_writel(1, USBH30_CTRL);
	xcode_setval(0, IP_VCC_RESET_N, USBH30_CTRL);
	xcode_setval(0, REF_USE_PAD, USBH30_PHY_CTRL_2);
	xcode_writel(0xe00, USBH30_STRAP);

	board_id = xcode_readl(CG_DUMMY_REG1);
	//GPIO control available only for SDK board
	//GPIO-5 is USB-3.0 Power Enable
	if (((board_id & 0xFF00) == 0x0000) ||	// SDK board
		((board_id & 0xFF00) == 0x1400)) 	// customer2 board 
	{
		xcode_setval(1, GPIO_DEDICATED_OUTEN5, GPIO_DEDICATED_OUTEN);
		xcode_setval(1, GPIO_DEDICATED_OUT5, GPIO_DEDICATED_OUT);
		udelay(100);
		xcode_setval(0, GPIO_DEDICATED_OUT5, GPIO_DEDICATED_OUT);
		mdelay(1000);
	} else if ((board_id & 0xFF00) == 0x1900) {
		xcode_setval(1, GPIO_DEDICATED_OUTEN5, GPIO_DEDICATED_OUTEN);
		xcode_setval(1, GPIO_DEDICATED_OUT5, GPIO_DEDICATED_OUT);
		udelay(100);
	}

	xcode_writel(0, USBH30_CTRL);
	udelay(10);

	xcode_setval(1, IP_VCC_RESET_N, USBH30_CTRL);
	udelay(100);

	temp=xcode_xhci_readl((__le32 *)0xc200);
	temp|=0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc200);
	temp=xcode_xhci_readl((__le32 *)0xc200);
	printk("(__le32 *)0xc200=0x%08x\n", temp);

	temp=xcode_xhci_readl((__le32 *)0xc2c0);
	printk("GUSB3PIPECTL=0x%08x\n", temp);
	temp|=0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc2c0);

	temp=xcode_xhci_readl((__le32 *)0xc110);
	printk("GCTL=0x%08x\n", temp);
	temp|=0x800;
	xcode_xhci_writel(temp, (__le32 *)0xc110);
	mdelay(10);

	temp=xcode_xhci_readl((__le32 *)0xc200);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc200);

	temp=xcode_xhci_readl((__le32 *)0xc2c0);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc2c0);
	udelay(10);

	temp=xcode_xhci_readl((__le32 *)0xc110);
	temp&=~0x800;
	xcode_xhci_writel(temp, (__le32 *)0xc110);
	udelay(100);

	temp=xcode_xhci_readl((__le32 *)0xc200);
	temp|=0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc200);

	temp=xcode_xhci_readl((__le32 *)0xc2c0);
	temp|=0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc2c0);

	temp=xcode_xhci_readl((__le32 *)0xc110);
	temp|=0x800;
	xcode_xhci_writel(temp, (__le32 *)0xc110);
	mdelay(1);

	temp=xcode_xhci_readl((__le32 *)0xc200);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc200);

	temp=xcode_xhci_readl((__le32 *)0xc2c0);
	printk("GUSB3PIPECTL (2) = 0x%08x\n", temp);
	temp&=~0x80000000;
	xcode_xhci_writel(temp, (__le32 *)0xc2c0);
	udelay(100);

	temp=xcode_xhci_readl((__le32 *)0xc110);
	temp&=~0x800;
	xcode_xhci_writel(temp, (__le32 *)0xc110);

	if(((board_id & 0xFF00) == 0x0000) || ((board_id & 0xFF00) == 0x1400))
		xcode_setval(1, GPIO_DEDICATED_OUT5, GPIO_DEDICATED_OUT);
	else if ((board_id & 0xFF00)==0x1900)
		xcode_setval(0, GPIO_DEDICATED_OUT5, GPIO_DEDICATED_OUT);

	mdelay(10);

#if 0
	while(1)
	{
		temp=xcode_xhci_readl((__le32 *)0x430);
		printk("PORTC %x\n", temp);
		mdelay(1000);
	}
#endif
	return 0;
}

int xcode_xhci_init(void)
{
  int error;

  printk(KERN_INFO "Start: SDUH XHCI device registed to platform bus\n");

  spin_lock_init(&xhci_xcode_reg_lock);

	xcode_xhci_hw_init();

  xcode_xhci_device = platform_device_alloc("xhci-hcd", -1);
  if (!xcode_xhci_device) {
    error = -ENOMEM;
    return error;
  }

  xcode_xhci_device->dev.coherent_dma_mask = DMA_BIT_MASK(32);
  xcode_xhci_device->dev.dma_mask = &xcode_xhci_device->dev.coherent_dma_mask;

  //memory resources
  xcode_xhci_resources[0].start = XC_SOC_PROC_MMREG_BASE + 0x12000;
  xcode_xhci_resources[0].end = xcode_xhci_resources[0].start + 0x400 - 1;
  xcode_xhci_resources[0].flags = IORESOURCE_MEM;

  //irq resources
  xcode_xhci_resources[1].start = XCODE6_IRQ_USBH3;
  xcode_xhci_resources[1].end = 0;
  xcode_xhci_resources[1].flags = IORESOURCE_IRQ;

  error = platform_device_add_resources(xcode_xhci_device, xcode_xhci_resources, 2);
  if (error)
    goto err_free_device;

  error = platform_device_add(xcode_xhci_device);
  if (error)
    goto err_free_device;


  printk(KERN_INFO "END: SDUH XHCI device registed to platform bus\n");

  return 0;
err_free_device:
  platform_device_put(xcode_xhci_device);
  return error;

}

