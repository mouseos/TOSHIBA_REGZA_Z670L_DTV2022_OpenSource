/*
 *  ahci_vixs.h
 *
 *  Copyright 2001-2009 Vixs Systems, Inc.  All rights reserved.
 *  Copyright 2009 Jerry Wang
 *
 */

#ifndef __AHCI_VIXS_H__
#define __AHCI_VIXS_H__

#ifdef CONFIG_ARCH_XCODE
extern void* VIXS_SATA_REG_BASE;
extern u32 VIXS_SATA_writel(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 value);
extern u32 VIXS_SATA_readl(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writeb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 value);
extern u8 VIXS_SATA_readb(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writew(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16 value);
extern u16 VIXS_SATA_readw(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writesl(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32*pbuf, u32 count);
extern u32 VIXS_SATA_readsl(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 *pbuf, u32 count);
extern u32 VIXS_SATA_writesb(void * SATAC_reg_base, void __iomem * sata_reg_addr, u8* pbuf , u32 count);
extern u32 VIXS_SATA_readsb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 *pbuf, u32 count);
extern u32 VIXS_SATA_writesw(void * SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf , u32 count);
extern u32 VIXS_SATA_readsw(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf, u32 count);




#define VIXS_SATA_reg_readb(a,b)	    VIXS_SATA_readb(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_readw(a,b)	    VIXS_SATA_readw(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_readl(a,b)	           VIXS_SATA_readl(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_writeb(a,b,c)	    VIXS_SATA_writeb(VIXS_SATA_REG_BASE, b,a)
#define VIXS_SATA_reg_writew(a,b,c)	    VIXS_SATA_writew(VIXS_SATA_REG_BASE, b,a)
#define VIXS_SATA_reg_writel(a,b,c)	    VIXS_SATA_writel(VIXS_SATA_REG_BASE, b,a)
#endif

#define VIXS_SATA_IRQ   XCODE6_IRQ_SATA

#define VIXS_SATA_HOST_SEL_MASK     0x80000000              // bit 31 to select if this is host 0 or host1

#endif /* __AHCI_VIXS_H__ */
