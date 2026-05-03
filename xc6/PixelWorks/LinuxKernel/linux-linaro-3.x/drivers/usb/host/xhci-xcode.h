#ifndef XHCI_XCODE_H
#define XHCI_XCODE_H

#include <linux/spinlock.h>
#include <mach/io.h>

extern spinlock_t xhci_xcode_reg_lock;

extern void xcode_xhci_irq_enable(void);
extern int xcode_xhci_init(void);

static inline unsigned int xcode_xhci_readl(__le32 __iomem *regs)
{
				unsigned long flags;
				unsigned long err;

				BUG_ON((u32)regs>=0x10000);
//				printk("%s: regs %x\n", __func__, (unsigned int)regs);

				spin_lock_irqsave(&xhci_xcode_reg_lock, flags);

				while(xcode_getval(BUSY, USBH30_CSR_STATUS)==1);

				xcode_writel((u32)regs, USBH30_CSR_CTRL);

				while(xcode_getval(BUSY, USBH30_CSR_STATUS)==1);

				err=xcode_getval(ERR, USBH30_CSR_STATUS);

				spin_unlock_irqrestore(&xhci_xcode_reg_lock, flags);

				if(err)
					return 0;

				return xcode_readl(USBH30_CSR_RDAT);
}

static inline unsigned int xcode_xhci_writel(const unsigned int val, __le32 __iomem *regs)
{
				unsigned long flags;
				unsigned long err;

				BUG_ON((u32)regs>=0x10000);
//				printk("%s: regs %x val %x\n", __func__, (unsigned int)regs, val);

				spin_lock_irqsave(&xhci_xcode_reg_lock, flags);

				while(xcode_getval(BUSY, USBH30_CSR_STATUS)==1);

				xcode_writel(val, USBH30_CSR_WDAT);

				xcode_writel((u32)regs | 0x80000000, USBH30_CSR_CTRL);

				err=xcode_getval(ERR, USBH30_CSR_STATUS);

				while(xcode_getval(BUSY, USBH30_CSR_STATUS)==1);

				spin_unlock_irqrestore(&xhci_xcode_reg_lock, flags);

				if(err)
					return 0;

				return 0;
}

#endif
