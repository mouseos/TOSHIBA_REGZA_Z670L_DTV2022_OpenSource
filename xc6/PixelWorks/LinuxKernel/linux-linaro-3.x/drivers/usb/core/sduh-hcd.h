#ifndef _SDUH_HCD_H
#define _SDUH_HCD_H

#include <linux/usb/hcd.h>
//********************************************************
//SDUH register definition
//********************************************************
#define SDUH_REG_BASE	(XC_SOC_PROC_MMREG_BASE + 0x2000)
#define SDUH_REG_SIZE	0x40
   

#define SDUH_IAS_REG	0
#define SDUH_EOC_REG	4
#define SDUH_OOC_REG	0xc
#define SDUH_WDATA_REG	0x10
#define SDUH_RDATA_REG		0x14
#define SDUH_OTC_REG	0x18
#define SDUH_STATUS_REG	0x1c
#define SDUH_CONTROL_REG	0x20
#define SDUH_PHY_CONTROL_REG	0x30
#define SDUH_HOST_INT_MASK_REG	0x34
#define SDUH_MIPS_INT_MASK_REG	0x38

#define reg_readl(addr)	 		readl((volatile void __iomem *)(addr))
#define reg_writel(val, addr)	writel((u32)(val), (volatile void __iomem*)(addr))

typedef enum {
	EHCI_REG = 0,
	OHCI_REG = 1,
} SDUH_HC_reg_type;

extern u32 SDUH_HCD_REG_BASE;
extern u32 SDUH_HCD_REF_CNT;
extern u32 SDUH_HCD_ACTIVE_CNT;
extern u32 SDUH_writel(u32 SDUH_reg_base, u32 ehci_reg_addr, u32 value, SDUH_HC_reg_type type);
extern u32 SDUH_readl(u32 SDUH_reg_base, u32 ehci_reg_addr, SDUH_HC_reg_type type);
void init_sduh(struct usb_hcd *hcd);
extern spinlock_t sduh_usb_indirect_reg_lock;


#endif
