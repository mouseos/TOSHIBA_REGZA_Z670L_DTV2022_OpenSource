/*
 *  ahci_vixs.c - AHCI SATA support on Vixs XC4
 *
 *  Maintained by:  Jerry Wang <jlwang@vixs.com>
 *
 *  Copyright 2001-2009 Vixs Systems, Inc.
 *
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <plat/xcodeRegDef.h>
#include "ahci.h"
#include "ahci_vixs.h"


#define VIXS_DRV_NAME	"xcode-ahci"
#define VIXS_DRV_VERSION	"3.0"
#define VIXS_AHCI_HOST0_NAME	"xcode-ahci-host0"
#define VIXS_AHCI_HOST1_NAME	"xcode-ahci-host1"
#define SATA_RETRY_LIMIT 1000

/* Global settings for SATA */
#ifdef CONFIG_PLAT_XCODE64xx
static u32 vixs_ahci_clk_type = 0;  //0: internal clock 1: external clock
#elif CONFIG_PLAT_XCODE68xx
static u32 vixs_ahci_clk_type = 1;  //0: internal clock 1: external clock
#endif

static u32 vixs_ahci_ssmode = 1;	//0: no ss mode	1: ss mode
static u32 vixs_ahci_clk = 100; //100Mhz by default


void __iomem* g_vixs_iomap[2][6] = {{0,0,0,0,0,0}, {0,0,0,0,0,(void*)VIXS_SATA_HOST_SEL_MASK}};
u32 g_next_host_index = 0;   //Since XCode may have multiple AHCI hosts, it is used to determine which host it is.
u32 g_vixs_ahci_active_cnt = 0;


void dump_xc6_sata_register(struct ata_port *ap);
static struct ata_host *vixs_ata_host[2]={NULL, NULL};
static int vixs_ahci_skip_host_reset=0;
module_param_named(vixs_skip_host_reset, vixs_ahci_skip_host_reset, int, 0444);
MODULE_PARM_DESC(vixs_skip_host_reset, "skip global host reset (0=don't skip, 1=skip)");

static int vixs_ahci_set_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			unsigned int hints);
//static int vixs_ahci_enable_alpm(struct ata_port *ap,
//		enum link_pm policy);
//static void vixs_ahci_disable_alpm(struct ata_port *ap);

extern int vixs_ahci_scr_read(struct ata_link *link,  unsigned int sc_reg, u32 *val);
extern int vixs_ahci_scr_write(struct ata_link *link,  unsigned int sc_reg, u32 val);
static unsigned int vixs_ahci_qc_issue(struct ata_queued_cmd *qc);
static bool vixs_ahci_qc_fill_rtf(struct ata_queued_cmd *qc);
static int vixs_ahci_port_start(struct ata_port *ap);
static void vixs_ahci_port_stop(struct ata_port *ap);
static void vixs_ahci_qc_prep(struct ata_queued_cmd *qc);
static void vixs_ahci_freeze(struct ata_port *ap);
static void vixs_ahci_thaw(struct ata_port *ap);
static void vixs_ahci_pmp_attach(struct ata_port *ap);
static void vixs_ahci_pmp_detach(struct ata_port *ap);
static int vixs_ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline);
static int vixs_ahci_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline);
static void vixs_ahci_postreset(struct ata_link *link, unsigned int *class);
static void vixs_ahci_error_handler(struct ata_port *ap);
static void vixs_ahci_post_internal_cmd(struct ata_queued_cmd *qc);
static int vixs_ahci_port_resume(struct ata_port *ap);
static void vixs_ahci_dev_config(struct ata_device *dev);
static unsigned int vixs_ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl);
static void vixs_ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			       u32 opts);
#ifdef CONFIG_PM
static int vixs_ahci_port_suspend(struct ata_port *ap, pm_message_t mesg);
static int vixs_ahci_resume(struct platform_device *pdev);
static int vixs_ahci_suspend(struct platform_device *pdev, pm_message_t mesg);
#endif

static int vixs_ahci_init_one (struct platform_device*pdev);
static int vixs_ahci_remove_one (struct platform_device *pdev);
static void vixs_ahci_init_controller(struct ata_host *host);
static int vixs_ahci_reset_controller(struct ata_host *host);
void SATA_SET_CLOCK(int sata_clock, int mode,int sata0_disable, int sata1_disable);
static int xcode_ahci_host_init(void );


void* VIXS_SATA_REG_BASE = NULL;
static spinlock_t vixs_sata_indirect_reg_lock;


u32 regRead( u32 dwRegByteOffset )
{
    return (readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+dwRegByteOffset)));
}

void regWrite( u32 dwRegByteOffset, u32 dwData )
{
    writel(dwData, (volatile void *)(XC_SOC_PROC_MMREG_BASE+dwRegByteOffset));
}

void RegMaskWrite(u32 dwRegByteOffset, u32 dwData, u32 mask)
{
   unsigned int temp1, temp2;
   temp1 = regRead(dwRegByteOffset);
   temp2 = dwData & mask;
   temp1 = temp1 & (~mask);
   temp2 = temp2 | temp1;
   regWrite(dwRegByteOffset,temp2);
}

int RegPoll(u32 regOffset, u32 regStatus, u32 regMask, u32 loop, u32 usleep)
{
    unsigned int temp1, temp2 = 0;
    unsigned int loop_counter = 0;
    
    while (loop_counter++ < loop) 
    {
        temp1 = regRead(regOffset);
        temp2 = temp1 & regMask;
        if (temp2 == regStatus)
            break;
        msleep(usleep/1000);
    }
    if (temp2 != regStatus){
        VPRINTK("%s:%d timeout on reg:%x\n",__func__,__LINE__,regOffset);
		return -1;
    }
	return 0;
}

u32 VIXS_SATA_writel(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32 value)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}

    writel(value, SATAC_reg_base + 8);

    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80220000;        
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80210000;
    }
    
    writel(regVal, SATAC_reg_base + 0);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}	

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
    return 0;
};

    
u32 VIXS_SATA_readl(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;

	//printk("SATAC_reg_base=0x%08x, sata_reg_addr=0x%08x\n", SATAC_reg_base, sata_reg_addr);
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	


    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00220000;
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00210000;
    }

    writel(regVal, SATAC_reg_base + 0);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	

    if(regVal&0x2) 
    {
        spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);

        return 0;
    };

    regVal = readl(SATAC_reg_base + 0xc);
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);

    return regVal;
};

u32 VIXS_SATA_writeb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 value)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);
		
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}	

    writel(value, SATAC_reg_base + 8);

    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80020000;        
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80010000;
    }


    writel(regVal, SATAC_reg_base + 0);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}	

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
    return 0;
}

u8 VIXS_SATA_readb(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);

	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	

    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00020000;
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00010000;
    }

    writel(regVal, SATAC_reg_base + 0);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	

    if(regVal&0x2) 
    {
        spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
        return 0;
     };

    regVal = (u8)readl(SATAC_reg_base + 0xc);
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);

    return (u8)regVal;
}


u32 VIXS_SATA_writew(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16 value)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);

	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}	


    writel(value, SATAC_reg_base + 8);

    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80120000;        
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x80110000;
    }

    writel(regVal, SATAC_reg_base + 0);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
    return 0;
}


u16 VIXS_SATA_readw(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    spin_lock_irqsave(&vixs_sata_indirect_reg_lock, flags);

	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	


    regVal = ((u32)sata_reg_addr)|0x00110000;
    if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00120000;
    }
    else
    {
        regVal = ((u32)sata_reg_addr&0xffff)|0x00110000;
    }


    writel(regVal, SATAC_reg_base + 0);

	retry  = 0;
	while(((regVal = readl(SATAC_reg_base + 4))&0x1)== 1)
	{
		if(++retry >= SATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);
			return 0;
		}
	}	

    if(regVal&0x2) 
    {
        spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);

        return 0;
    };

    regVal = (u16)readl(SATAC_reg_base + 0xc);
    spin_unlock_irqrestore(&vixs_sata_indirect_reg_lock, flags);

    return (u16)regVal;
}



u32 VIXS_SATA_writesl(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32*pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_SATA_writel(SATAC_reg_base, sata_reg_addr, (u32)*p);
        p++;
    }

    return 0;
}

u32 VIXS_SATA_readsl(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 *pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_SATA_readl(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}
    
u32 VIXS_SATA_writesb(void * SATAC_reg_base, void __iomem * sata_reg_addr, u8* pbuf , u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_SATA_writeb(SATAC_reg_base, sata_reg_addr, (u8)*p);
        p++;
    }

    return 0;
}
    
u32 VIXS_SATA_readsb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 *pbuf, u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_SATA_readb(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}    

u32 VIXS_SATA_writesw(void * SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf , u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_SATA_writew(SATAC_reg_base, sata_reg_addr, (u16)*p);
        p++;
    }

    return 0;
}

u32 VIXS_SATA_readsw(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf, u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_SATA_readw(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}




static struct device_attribute *vixs_ahci_shost_attrs[] = {
	&dev_attr_link_power_management_policy,
	NULL
};

static struct scsi_host_template vixs_ahci_sht = {
	ATA_NCQ_SHT(VIXS_DRV_NAME),
	.can_queue		= AHCI_MAX_CMDS - 1,
	.sg_tablesize		= AHCI_MAX_SG,
	.dma_boundary		= AHCI_DMA_BOUNDARY,
	.shost_attrs		= vixs_ahci_shost_attrs,
};

static struct ata_port_operations vixs_ahci_ops = {
	.inherits		= &sata_pmp_port_ops,

	.qc_defer		= sata_pmp_qc_defer_cmd_switch,
	.qc_prep		= vixs_ahci_qc_prep,
	.qc_issue		= vixs_ahci_qc_issue,
	.qc_fill_rtf		= vixs_ahci_qc_fill_rtf,

	.freeze			= vixs_ahci_freeze,
	.thaw			= vixs_ahci_thaw,
	.softreset		= vixs_ahci_softreset,
	.hardreset		= vixs_ahci_hardreset,
	.postreset		= vixs_ahci_postreset,
	.pmp_softreset		= vixs_ahci_softreset,
	.error_handler		= vixs_ahci_error_handler,
	.post_internal_cmd	= vixs_ahci_post_internal_cmd,
	.dev_config		= vixs_ahci_dev_config,

	.scr_read		= vixs_ahci_scr_read,
	.scr_write		= vixs_ahci_scr_write,
	.pmp_attach		= vixs_ahci_pmp_attach,
	.pmp_detach		= vixs_ahci_pmp_detach,

#if 0
	.enable_pm		= vixs_ahci_enable_alpm,
	.disable_pm		= vixs_ahci_disable_alpm,
#else
	.set_lpm = vixs_ahci_set_lpm,
#endif
#ifdef CONFIG_PM
	.port_suspend	= vixs_ahci_port_suspend,
	.port_resume	= vixs_ahci_port_resume,
#endif
	.port_start		= vixs_ahci_port_start,
	.port_stop		= vixs_ahci_port_stop,
};




static const struct ata_port_info vixs_ahci_port_info[] = {
	/* host #1 */
	{
//		AHCI_HFLAGS	(AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &vixs_ahci_ops,
	},
	
	/* host #2 (if available) */
	{
//		AHCI_HFLAGS	(AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &vixs_ahci_ops,
	}
};



static struct platform_driver vixs_ahci_driver0 = {
    .probe = vixs_ahci_init_one,
    .remove = vixs_ahci_remove_one,
    .shutdown = NULL,
#ifdef CONFIG_PM   
    .suspend = vixs_ahci_suspend,
    .resume = vixs_ahci_resume,
#endif
    .driver = {
        .name = VIXS_AHCI_HOST0_NAME,
    },
};

static struct platform_driver vixs_ahci_driver1 = {
    .probe = vixs_ahci_init_one,
    .remove = vixs_ahci_remove_one,
    .shutdown = NULL,
#ifdef CONFIG_PM   
        .suspend = vixs_ahci_suspend,
        .resume = vixs_ahci_resume,
#endif
    .driver = {
        .name = VIXS_AHCI_HOST1_NAME,
    },
};

static void vixs_ahci_enable_ahci(void __iomem *mmio)
{
	int i;
	u32 tmp;
	/* turn on AHCI_EN */
	tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
	if (tmp & HOST_AHCI_EN)
		return;

	/* Some controllers need AHCI_EN to be written multiple times.
	 * Try a few times before giving up.
	 */
	for (i = 0; i < 5; i++) {
		tmp |= HOST_AHCI_EN;
		VIXS_SATA_reg_writel(tmp, mmio + HOST_CTL, 0);
		tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);	/* flush && sanity check */
		if (tmp & HOST_AHCI_EN)
			return;
		msleep(10);
	}

    #ifndef CONFIG_FPGA_BUILD
	WARN_ON(1);
    #endif
}

/**
 *	vixs_ahci_save_initial_config - Save and fixup initial config values
 *	@pdev: target PCI device
 *	@hpriv: host private area to store config values
 *
 *	Some registers containing configuration info might be setup by
 *	BIOS and might be cleared on reset.  This function saves the
 *	initial values of those registers into @hpriv such that they
 *	can be restored after controller reset.
 *
 *	If inconsistent, config values are fixed up by this function.
 *
 *	LOCKING:
 *	None.
 */
static void vixs_ahci_save_initial_config(struct platform_device *pdev,
				     struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = g_vixs_iomap[hpriv->index][AHCI_PCI_BAR];    
	u32 cap, port_map;
	int i;
	/* make sure AHCI mode is enabled before accessing CAP */
	vixs_ahci_enable_ahci(mmio);

	/* Values prefixed with saved_ are written back to host after
	 * reset.  Values without are used for driver operation.
	 */
	hpriv->saved_cap = cap = VIXS_SATA_reg_readl(mmio + HOST_CAP, 0);
	hpriv->saved_port_map = port_map = VIXS_SATA_reg_readl(mmio + HOST_PORTS_IMPL, 0);

	/* some chips have errata preventing 64bit use */
	if ((cap & HOST_CAP_64) && (hpriv->flags & AHCI_HFLAG_32BIT_ONLY)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do 64bit DMA, forcing 32bit\n");
		cap &= ~HOST_CAP_64;
	}

	if ((cap & HOST_CAP_NCQ) && (hpriv->flags & AHCI_HFLAG_NO_NCQ)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do NCQ, turning off CAP_NCQ\n");
		cap &= ~HOST_CAP_NCQ;
	}

	if (!(cap & HOST_CAP_NCQ) && (hpriv->flags & AHCI_HFLAG_YES_NCQ)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can do NCQ, turning on CAP_NCQ\n");
		cap |= HOST_CAP_NCQ;
	}

	if ((cap & HOST_CAP_PMP) && (hpriv->flags & AHCI_HFLAG_NO_PMP)) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "controller can't do PMP, turning off CAP_PMP\n");
		cap &= ~HOST_CAP_PMP;
	}

	/* cross check port_map and cap.n_ports */
	if (port_map) {
		int map_ports = 0;

		for (i = 0; i < AHCI_MAX_PORTS; i++)
			if (port_map & (1 << i))
				map_ports++;

		/* If PI has more ports than n_ports, whine, clear
		 * port_map and let it be generated from n_ports.
		 */
		if (map_ports > ahci_nr_ports(cap)) {
			dev_printk(KERN_WARNING, &pdev->dev,
				   "implemented port map (0x%x) contains more "
				   "ports than nr_ports (%u), using nr_ports\n",
				   port_map, ahci_nr_ports(cap));
			port_map = 0;
		}
	}

	/* fabricate port_map from cap.nr_ports */
	if (!port_map) {
		port_map = (1 << ahci_nr_ports(cap)) - 1;
		dev_printk(KERN_WARNING, &pdev->dev,
			   "forcing PORTS_IMPL to 0x%x\n", port_map);

		/* write the fixed up value to the PI register */
		hpriv->saved_port_map = port_map;
	}

	/* record values to use during operation */
	hpriv->cap = cap;
	hpriv->port_map = port_map;
}

/**
 *	vixs_ahci_restore_initial_config - Restore initial config
 *	@host: target ATA host
 *
 *	Restore initial config stored by ahci_save_initial_config().
 *
 *	LOCKING:
 *	None.
 */
static void vixs_ahci_restore_initial_config(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	VIXS_SATA_reg_writel(hpriv->saved_cap, mmio + HOST_CAP, 0);
	VIXS_SATA_reg_writel(hpriv->saved_port_map, mmio + HOST_PORTS_IMPL, 0);
	(void) VIXS_SATA_reg_readl(mmio + HOST_PORTS_IMPL, 0);	/* flush */
}

//extern int vixs_ahci_scr_read( struct ata_link *link, unsigned int sc_reg, u32 *val);
//extern int vixs_ahci_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val);

static void vixs_ahci_start_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	/* start DMA */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD,0);
	tmp |= PORT_CMD_START;
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);
	VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0); /* flush */
}


u32 vixs_ata_wait_register(void __iomem *reg, u32 mask, u32 val,
		      unsigned long interval_msec,
		      unsigned long timeout_msec)
{
	unsigned long timeout;
	u32 tmp;

	tmp = VIXS_SATA_reg_readl(reg, 0);

	/* Calculate timeout _after_ the first read to make sure
	 * preceding writes reach the controller before starting to
	 * eat away the timeout.
	 */
	timeout = jiffies + (timeout_msec * HZ) / 1000;

	while ((tmp & mask) == val && time_before(jiffies, timeout)) {
		msleep(interval_msec);
		tmp = VIXS_SATA_reg_readl(reg, 0);
	}

	return tmp;
}

static int vixs_ahci_stop_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

	/* check if the HBA is idle */
	if ((tmp & (PORT_CMD_START | PORT_CMD_LIST_ON)) == 0)
		return 0;

	/* setting HBA to idle */
	tmp &= ~PORT_CMD_START;
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

	/* wait for engine to stop. This could be as long as 500 msec */
	tmp = vixs_ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_LIST_ON, PORT_CMD_LIST_ON, 1, 500);
	if (tmp & PORT_CMD_LIST_ON)
		return -EIO;

	return 0;
}

static void vixs_ahci_start_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	u32 tmp;
	/* set FIS registers */
	if (hpriv->cap & HOST_CAP_64)
		VIXS_SATA_reg_writel((pp->cmd_slot_dma >> 16) >> 16,
		       port_mmio + PORT_LST_ADDR_HI, 0);
	VIXS_SATA_reg_writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR, 0);

	if (hpriv->cap & HOST_CAP_64)
		VIXS_SATA_reg_writel((pp->rx_fis_dma >> 16) >> 16,
		       port_mmio + PORT_FIS_ADDR_HI, 0);
	VIXS_SATA_reg_writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR, 0);

	/* enable FIS reception */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp |= PORT_CMD_FIS_RX;
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

	/* flush */
	VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
}

static int vixs_ahci_stop_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	/* disable FIS reception */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp &= ~PORT_CMD_FIS_RX;
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);
	/* wait for completion, spec says 500ms, give it 1000 */
	tmp = vixs_ata_wait_register(port_mmio + PORT_CMD, PORT_CMD_FIS_ON,
				PORT_CMD_FIS_ON, 10, 1000);
	if (tmp & PORT_CMD_FIS_ON)
		return -EBUSY;
	return 0;
}

static void vixs_ahci_power_up(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0) & ~PORT_CMD_ICC_MASK;

	/* spin up device */
	if (hpriv->cap & HOST_CAP_SSS) {
		cmd |= PORT_CMD_SPIN_UP;
		VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
	}

	/* wake up link */
	VIXS_SATA_reg_writel(cmd | PORT_CMD_ICC_ACTIVE, port_mmio + PORT_CMD, 0);
}

#if 1
static int vixs_ahci_set_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			unsigned int hints)
{
	struct ata_port *ap = link->ap;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);

	if (policy != ATA_LPM_MAX_POWER) {
		/*
		 * Disable interrupts on Phy Ready. This keeps us from
		 * getting woken up due to spurious phy ready
		 * interrupts.
		 */
		pp->intr_mask &= ~PORT_IRQ_PHYRDY;
		VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);

		sata_link_scr_lpm(link, policy, false);
	}

	if (hpriv->cap & HOST_CAP_ALPM) {
		u32 cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

		if (policy == ATA_LPM_MAX_POWER || !(hints & ATA_LPM_HIPM)) {
			cmd &= ~(PORT_CMD_ASP | PORT_CMD_ALPE);
			cmd |= PORT_CMD_ICC_ACTIVE;

			VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
			VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

			/* wait 10ms to be sure we've come out of LPM state */
			ata_msleep(ap, 10);
		} else {
			cmd |= PORT_CMD_ALPE;
			if (policy == ATA_LPM_MIN_POWER)
				cmd |= PORT_CMD_ASP;

			/* write out new cmd value */
			VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
		}
	}

	if (policy == ATA_LPM_MAX_POWER) {
		sata_link_scr_lpm(link, policy, false);

		/* turn PHYRDY IRQ back on */
		pp->intr_mask |= PORT_IRQ_PHYRDY;
		VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
	}

	return 0;
}
#else
static void vixs_ahci_disable_alpm(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;
	struct ahci_port_priv *pp = ap->private_data;

	/* IPM bits should be disabled by libata-core */
	/* get the existing command bits */
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

	/* disable ALPM and ASP */
	cmd &= ~PORT_CMD_ASP;
	cmd &= ~PORT_CMD_ALPE;

	/* force the interface back to active */
	cmd |= PORT_CMD_ICC_ACTIVE;

	/* write out new cmd value */
	VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

	/* wait 10ms to be sure we've come out of any low power state */
	msleep(10);

	/* clear out any PhyRdy stuff from interrupt status */
	VIXS_SATA_reg_writel(PORT_IRQ_PHYRDY, port_mmio + PORT_IRQ_STAT, 0);

	/* go ahead and clean out PhyRdy Change from Serror too */
	vixs_ahci_scr_write(&ap->link, SCR_ERROR, ((1 << 16) | (1 << 18)));

	/*
 	 * Clear flag to indicate that we should ignore all PhyRdy
 	 * state changes
 	 */
	hpriv->flags &= ~AHCI_HFLAG_NO_HOTPLUG;

	/*
 	 * Enable interrupts on Phy Ready.
 	 */
	pp->intr_mask |= PORT_IRQ_PHYRDY;
	VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);

	/*
 	 * don't change the link pm policy - we can be called
 	 * just to turn of link pm temporarily
 	 */
}

static int vixs_ahci_enable_alpm(struct ata_port *ap,
	enum link_pm policy)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;
	struct ahci_port_priv *pp = ap->private_data;
	u32 asp;
	/* Make sure the host is capable of link power management */
	if (!(hpriv->cap & HOST_CAP_ALPM))
		return -EINVAL;

	switch (policy) {
	case MAX_PERFORMANCE:
	case NOT_AVAILABLE:
		/*
 		 * if we came here with NOT_AVAILABLE,
 		 * it just means this is the first time we
 		 * have tried to enable - default to max performance,
 		 * and let the user go to lower power modes on request.
 		 */
		vixs_ahci_disable_alpm(ap);
		return 0;
	case MIN_POWER:
		/* configure HBA to enter SLUMBER */
		asp = PORT_CMD_ASP;
		break;
	case MEDIUM_POWER:
		/* configure HBA to enter PARTIAL */
		asp = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
 	 * Disable interrupts on Phy Ready. This keeps us from
 	 * getting woken up due to spurious phy ready interrupts
	 * TBD - Hot plug should be done via polling now, is
	 * that even supported?
 	 */
	pp->intr_mask &= ~PORT_IRQ_PHYRDY;
	VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);

	/*
 	 * Set a flag to indicate that we should ignore all PhyRdy
 	 * state changes since these can happen now whenever we
 	 * change link state
 	 */
	hpriv->flags |= AHCI_HFLAG_NO_HOTPLUG;

	/* get the existing command bits */
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

	/*
 	 * Set ASP based on Policy
 	 */
	cmd |= asp;

	/*
 	 * Setting this bit will instruct the HBA to aggressively
 	 * enter a lower power link state when it's appropriate and
 	 * based on the value set above for ASP
 	 */
	cmd |= PORT_CMD_ALPE;

	/* write out new cmd value */
	writel(cmd, port_mmio + PORT_CMD);
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);

	/* IPM bits should be set by libata-core */
	return 0;
}
#endif
#if 0
static void start_clock(void)
{
    u32 temp;

    //Enable SATA clock
#ifdef CONFIG_PLAT_XCODE64xx

    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));
    temp &= ~ACC_BLK_STOP0_SATA_BLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));

    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));
    temp &= ~CG1_CLK_STOP0_SATACLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));

#else

    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));
    temp &= ~ACC_BLK_STOP0_MOCA_BLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));

    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));
    temp &= ~CG1_CLK_STOP0_MOCACLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));

#endif
}
#endif

#ifdef CONFIG_PM
#if 0
static void stop_clock(void)
{
    u32 temp;

    //Disable SATA clock
#ifdef CONFIG_PLAT_XCODE64xx
    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));
	temp |= ACC_BLK_STOP0_SATA_BLK_STOP_MASK;
	writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));

	temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));
	temp |= CG1_CLK_STOP0_SATACLK_STOP_MASK;
	writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));

#else
    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));
    temp |= ACC_BLK_STOP0_MOCA_BLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0));

    temp = readl((const volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));
    temp |= CG1_CLK_STOP0_MOCACLK_STOP_MASK;
    writel(temp, (volatile void *)(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0));

#endif

}
#endif
static void vixs_ahci_power_down(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd, scontrol;
	if (!(hpriv->cap & HOST_CAP_SSS))
		return;

	/* put device into listen mode, first set PxSCTL.DET to 0 */
	scontrol = VIXS_SATA_reg_readl(port_mmio + PORT_SCR_CTL, 0);
	scontrol &= ~0xf;
	VIXS_SATA_reg_writel(scontrol, port_mmio + PORT_SCR_CTL, 0);

	/* then set PxCMD.SUD to 0 */
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0) & ~PORT_CMD_ICC_MASK;
	cmd &= ~PORT_CMD_SPIN_UP;
	VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
}

//the disk has been stoped by SCSI SD layer, do nothing here
static void vixs_ahci_device_suspend(void){
    //ata_do_simple_cmd(struct ata_device *dev, ATA_CMD_SLEEP);
}


static void vixs_ahci_device_resume(void){
    //TODO: how to get ata_device?
    //ata_do_simple_cmd(struct ata_device *dev, ATA_CMD_SLEEP);
}

static void vixs_ahci_phy_suspend(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
    u32 ctl, i;
    //First check slumber capability:CAP.SSC
    //TODO can be done through hpriv->cap?
    ctl = VIXS_SATA_reg_readl(mmio + HOST_CAP, 0);
    if(!(ctl & HOST_CAP_SSC))
        return;

    //Set the PxCMD.ICC to 0x6h for all port of this device
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;
        void __iomem *port_mmio; 

		ap = host->ports[i];
		if (ap) {
            port_mmio = ahci_port_base(ap);
            ctl = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0); 
            printk("port cmd before phy suspend: %x\n", ctl);
            ctl &= ~PORT_CMD_ICC_MASK;
            ctl |= PORT_CMD_ICC_SLUMBER;
            /* AHCI spec 1.3 section 8.3.3
             * PxCMD.ST must be cleared to 0 before entry into D3
             */
            ctl &= ~PORT_CMD_START;
            VIXS_SATA_reg_writel(ctl, port_mmio + PORT_CMD, 0);
        }
    }
}

static void vixs_ahci_phy_resume(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	//void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
    u32 ctl, i;
    //Set the PxCMD.ICC to 0x6h for all port of this device
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;
        void __iomem *port_mmio; 

		ap = host->ports[i];
		if (ap) {
            port_mmio = ahci_port_base(ap);
            ctl = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0); 
            printk("port cmd before phy resume: %x\n", ctl);
            ctl &= ~PORT_CMD_ICC_MASK;
            ctl |= PORT_CMD_ICC_ACTIVE;
            ctl |= PORT_CMD_START;
            VIXS_SATA_reg_writel(ctl, port_mmio + PORT_CMD, 0);
        }
    }
}

static void vixs_ahci_hba_suspend(struct platform_device *pdev)
{
    struct ata_host *host = dev_get_drvdata(&pdev->dev);
    void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
    u32 ctl;
    /* AHCI spec rev1.1 section 8.3.3:
     * Software must disable interrupts prior to requesting a
     * transition of the HBA to D3 state.
     */
    ctl = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
    printk("HOST_CTL 0x%x\n", ctl);
    ctl &= ~HOST_IRQ_EN;
    VIXS_SATA_reg_writel(ctl, mmio + HOST_CTL, 0);
    ctl = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
    printk("HOST_CTL 0x%x\n", ctl);

    /* AHCI 1.3 section 8.3.3:
     * This is performed via the PCI power management registers 
     * in configuration space.
     * TODO how to put HBA to D3hot?
     */

}

static void vixs_ahci_hba_resume(struct platform_device *pdev)
{
    struct ata_host *host = dev_get_drvdata(&pdev->dev);
    void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
    u32 ctl;
    //Enable interrupt 
    ctl = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
    printk("HOST_CTL 0x%x\n", ctl);
    ctl |= HOST_IRQ_EN;
    VIXS_SATA_reg_writel(ctl, mmio + HOST_CTL, 0);
    ctl = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
    printk("HOST_CTL 0x%x\n", ctl);

    //Resume HBA
}


/* TODO:
 * Should be as simple as:
 * 1. diable interrupt
 * 2. call ata_host suspend
 * ATA layer will take care of PHY/HBA suspend!
 */
static int vixs_ahci_suspend(struct platform_device *pdev, pm_message_t mesg)
{
    struct ata_host *host = dev_get_drvdata(&pdev->dev);

    printk("Enter %s\n", __FUNCTION__);
    ata_host_suspend(host, mesg);
#if 1
    //Put device to D3
    vixs_ahci_device_suspend();

    //Put PHY into Slumber
    vixs_ahci_phy_suspend(pdev);

    //Put HBA to D3
    if (mesg.event & PM_EVENT_SLEEP) {
        vixs_ahci_hba_suspend(pdev);
    }
#endif

    g_vixs_ahci_active_cnt--;

    if(g_vixs_ahci_active_cnt == 0)
    {	
        #ifdef CONFIG_PLAT_XCODE64xx        
        RegMaskWrite(SATA_SFT_RESETS,0x1,PHY_SFT_RSTN_MASK); //pull SATA phy out of reset
        RegMaskWrite(SATA0_PHY_TXRX_CTRL,0x0,SATA0_PHY_RX_PLL_PWRON_MASK); //POWER DOWN SATA0
        RegMaskWrite(SATA1_PHY_TXRX_CTRL,0x0,SATA1_PHY_RX_PLL_PWRON_MASK); //POWER DOWN SATA1
        RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0,SATA_PHY_MPLL_PWRON_MASK);//turn off mp_ck
        RegMaskWrite(SATA_PHY_MPLL_CTRL,0x1000,SATA_PHY_MPLL_CK_OFF_MASK); //GATE SATA CLOCK TO PHY
        #else
        RegMaskWrite(SATA_SFT_RESETS,0x1,PHY_SFT_RSTN_MASK); //pull SATA phy out of reset
        RegMaskWrite(SATA_PHY_ANALOG_CTRL,0x1, SATA_PHY_ANALOG_CTRL_SATA_PHY_TEST_PDDQ_MASK); //POWER DOWN PHY
        #endif        
    }

    return 0; 
}


static int vixs_ahci_resume(struct platform_device *pdev)
{
    struct ata_host *host = dev_get_drvdata(&pdev->dev);
    int rc;
    printk("Enter %s\n", __FUNCTION__);

    if(g_vixs_ahci_active_cnt == 0)
        SATA_SET_CLOCK(vixs_ahci_clk, 1, 0, 0);

    g_vixs_ahci_active_cnt++;

    rc = vixs_ahci_reset_controller(host);
	if (rc)
		return rc;

	vixs_ahci_init_controller(host);
#if 1
    vixs_ahci_hba_resume(pdev);
    vixs_ahci_phy_resume(pdev);
    vixs_ahci_device_resume();
#endif

    ata_host_resume(host);

    printk("Exit %s\n", __FUNCTION__);
    
    return 0;
}
#endif

static void vixs_ahci_start_port(struct ata_port *ap){
	/* enable FIS reception */
	vixs_ahci_start_fis_rx(ap);

	/* enable DMA */
	vixs_ahci_start_engine(ap);
}

static int vixs_ahci_deinit_port(struct ata_port *ap, const char **emsg)
{
	int rc;
	/* disable DMA */
	rc = vixs_ahci_stop_engine(ap);
	if (rc) {
		*emsg = "failed to stop engine";
		return rc;
	}

	/* disable FIS reception */
	rc = vixs_ahci_stop_fis_rx(ap);
	if (rc) {
		*emsg = "failed stop FIS RX";
		return rc;
	}

	return 0;
}

static int vixs_ahci_reset_controller(struct ata_host *host)
{  
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 tmp;
    int i;
	/* we must be in AHCI mode, before using anything
	 * AHCI-specific, such as HOST_RESET.
	 */
	vixs_ahci_enable_ahci(mmio);
        printk("%s:%d vixs_ahci_skip_host_reset:%d\n",__func__,__LINE__,vixs_ahci_skip_host_reset);
	/* global controller reset */
	if (!vixs_ahci_skip_host_reset) {
		tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
		if ((tmp & HOST_RESET) == 0) {
			VIXS_SATA_reg_writel(tmp | HOST_RESET, mmio + HOST_CTL, 0);
			VIXS_SATA_reg_readl(mmio + HOST_CTL, 0); /* flush */
		}

		/* reset must complete within 1 second, or
		 * the hardware should be considered fried.
		 */

        for (i = 1; i <= 100; i++)
        {
		    msleep(10);

		    tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
		    if (tmp & HOST_RESET) {
                if (i == 20) {
		    	    dev_printk(KERN_ERR, host->dev,
		    	    	   "controller reset failed (0x%x)\n", tmp);
		    	    return -EIO;
                }
		    }
            else
                break;
        }

		/* turn on AHCI mode */
		vixs_ahci_enable_ahci(mmio);

		/* Some registers might be cleared on reset.  Restore
		 * initial values.
		 */
		vixs_ahci_restore_initial_config(host);
	} else
		dev_printk(KERN_INFO, host->dev,
			   "skipping global host reset\n");


	return 0;
}


static void vixs_ahci_dev_config(struct ata_device *dev)
{
	struct ahci_host_priv *hpriv = dev->link->ap->host->private_data;

	if (hpriv->flags & AHCI_HFLAG_SECT255) {
		dev->max_sectors = 255;
		ata_dev_printk(dev, KERN_INFO,
			       "SB600 AHCI: limiting to 255 sectors per cmd\n");
    }
    else
    {
        dev->max_sectors = 1024;
        ata_dev_printk(dev, KERN_INFO, "vixs_ahci_dev_config, set max_sectors to 1024\n");
    }
}

static unsigned int vixs_ahci_dev_classify(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_taskfile tf;
	u32 tmp;

	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_SIG, 0);
	tf.lbah		= (tmp >> 24)	& 0xff;
	tf.lbam		= (tmp >> 16)	& 0xff;
	tf.lbal		= (tmp >> 8)	& 0xff;
	tf.nsect	= (tmp)		& 0xff;

	return ata_dev_classify(&tf);
}
void dump_xc6_sata_cmd_slot(struct ahci_port_priv *pp, unsigned int tag){

		u32 *temp;
              printk("%s:%d tag:%d %x %x %x %x\n",__func__,__LINE__,tag,pp->cmd_slot[tag].opts,
	pp->cmd_slot[tag].status,
	pp->cmd_slot[tag].tbl_addr,
	pp->cmd_slot[tag].tbl_addr_hi);
             temp = __va(pp->cmd_slot[tag].tbl_addr);
	     printk("addr:%p %x %x %x %x %x\n",temp, *temp, *(temp+1),  *(temp+2),*(temp+3),*(temp+4));
	     temp +=32;
     	     printk("addr:%p %x %x %x %x\n", temp,*temp, *(temp+1),  *(temp+2),*(temp+3));

}
EXPORT_SYMBOL_GPL(dump_xc6_sata_cmd_slot);



static void vixs_ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			       u32 opts)
{
	dma_addr_t cmd_tbl_dma;

	cmd_tbl_dma = pp->cmd_tbl_dma + tag * AHCI_CMD_TBL_SZ;

	pp->cmd_slot[tag].opts = cpu_to_le32(opts);
	pp->cmd_slot[tag].status = 0;
	pp->cmd_slot[tag].tbl_addr = cpu_to_le32(cmd_tbl_dma & 0xffffffff);
	pp->cmd_slot[tag].tbl_addr_hi = cpu_to_le32((cmd_tbl_dma >> 16) >> 16);
}

static int vixs_ahci_kick_engine(struct ata_port *ap, int force_restart)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u8 status = VIXS_SATA_reg_readl(port_mmio + PORT_TFDATA, 0) & 0xFF;
	u32 tmp;
	int busy, rc;
	/* do we need to kick the port? */
	busy = status & (ATA_BUSY | ATA_DRQ);
	if (!busy && !force_restart)
		return 0;

	/* stop engine */
	rc = vixs_ahci_stop_engine(ap);
	if (rc)
		goto out_restart;

	/* need to do CLO? */
	if (!busy) {
		rc = 0;
		goto out_restart;
	}

	if (!(hpriv->cap & HOST_CAP_CLO)) {
		rc = -EOPNOTSUPP;
		goto out_restart;
	}

	/* perform CLO */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp |= PORT_CMD_CLO;
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

	rc = 0;
	tmp = vixs_ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_CLO, PORT_CMD_CLO, 1, 500);
	if (tmp & PORT_CMD_CLO)
		rc = -EIO;

	/* restart engine */
 out_restart:
	vixs_ahci_start_engine(ap);
	return rc;
}

static int vixs_ahci_exec_polled_cmd(struct ata_port *ap, int pmp,
				struct ata_taskfile *tf, int is_cmd, u16 flags,
				unsigned long timeout_msec)
{
	const u32 cmd_fis_len = 5; /* five dwords */
	struct ahci_port_priv *pp = ap->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u8 *fis = pp->cmd_tbl;
	u32 tmp;
	/* prep the command */
	ata_tf_to_fis(tf, pmp, is_cmd, fis);
	vixs_ahci_fill_cmd_slot(pp, 0, cmd_fis_len | flags | (pmp << 12));

	/* issue & wait */
	VIXS_SATA_reg_writel(1, port_mmio + PORT_CMD_ISSUE, 0);

	if (timeout_msec) {
		tmp = vixs_ata_wait_register(port_mmio + PORT_CMD_ISSUE, 0x1, 0x1,
					1, timeout_msec);
		if (tmp & 0x1) {
			vixs_ahci_kick_engine(ap, 1);
			return -EBUSY;
		}
	} else
		VIXS_SATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);	/* flush */

	return 0;
}

static int vixs_ahci_do_softreset(struct ata_link *link, unsigned int *class,
			     int pmp, unsigned long deadline,
			     int (*check_ready)(struct ata_link *link))
{
	struct ata_port *ap = link->ap;
	const char *reason = NULL;
	unsigned long now, msecs;
	struct ata_taskfile tf;
	int rc;
	DPRINTK("ENTER\n");

	/* prepare for SRST (AHCI-1.1 10.4.1) */
	rc = vixs_ahci_kick_engine(ap, 1);
	if (rc && rc != -EOPNOTSUPP)
		ata_link_printk(link, KERN_WARNING,
				"failed to reset engine (errno=%d)\n", rc);

    DPRINTK("ata_tf_init\n");

	ata_tf_init(link->device, &tf);

	/* issue the first D2H Register FIS */
	msecs = 0;
	now = jiffies;
	if (time_after(now, deadline))
		msecs = jiffies_to_msecs(deadline - now);

    DPRINTK("vixs_ahci_exec_polled_cmd\n");

	tf.ctl |= ATA_SRST;
	if (vixs_ahci_exec_polled_cmd(ap, pmp, &tf, 0,
				 AHCI_CMD_RESET | AHCI_CMD_CLR_BUSY, msecs)) {
		rc = -EIO;
		reason = "1st FIS failed";
		goto fail;
	}

	/* spec says at least 5us, but be generous and sleep for 1ms */
    udelay(5);
    //msleep(1);

	/* issue the second D2H Register FIS */
	tf.ctl &= ~ATA_SRST;
	vixs_ahci_exec_polled_cmd(ap, pmp, &tf, 0, 0, 0);

    DPRINTK("ata_wait_after_reset\n");

	/* wait for link to become ready */
	rc = ata_wait_after_reset(link, deadline, check_ready);
	/* link occupied, -ENODEV too is an error */
	if (rc) {
#if 0
            //JLWANG: A workaround to fix AHCI core bug "PMP Field Checked when CMD.PMA=0"
            if(pmp&&sata_pmp_supported(link->ap) && ata_is_host_link(link))
             {
                pmp = 0;
                goto retry;
             }
#endif
		reason = "device not ready";
		goto fail;
	}

    DPRINTK("vixs_ahci_dev_classify\n");
	*class = vixs_ahci_dev_classify(ap);

	DPRINTK("EXIT, class=%u\n", *class);
	return 0;

 fail:
	ata_link_printk(link, KERN_ERR, "softreset failed (%s)\n", reason);
	return rc;
}

static int vixs_ahci_check_ready(struct ata_link *link)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	u8 status = VIXS_SATA_reg_readl(port_mmio + PORT_TFDATA, 0) & 0xFF;

	return ata_check_ready(status);
}

static int vixs_ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	int pmp = sata_srst_pmp(link);
	DPRINTK("ENTER\n");

	return vixs_ahci_do_softreset(link, class, pmp, deadline, vixs_ahci_check_ready);
}


static int vixs_ahci_hardreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	struct ata_port *ap = link->ap;
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	struct ata_taskfile tf;
	bool online;
	int rc;
	printk("%s:%d :%s\n",__func__,__LINE__,"reset sata host controller");	
	DPRINTK("ENTER\n");
	vixs_ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = 0x80;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);
   

    DPRINTK("sata_link_hardreset\n");

	rc = sata_link_hardreset(link, timing, deadline, &online,
				 vixs_ahci_check_ready);
    
    DPRINTK("vixs_ahci_start_engine\n");

	vixs_ahci_start_engine(ap);

    DPRINTK("vixs_ahci_dev_classify\n");

	if (online)
		*class = vixs_ahci_dev_classify(ap);

	DPRINTK("EXIT, rc=%d, class=%u\n", rc, *class);
	return rc;
}


static void vixs_ahci_postreset(struct ata_link *link, unsigned int *class)
{
	struct ata_port *ap = link->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 new_tmp, tmp;
	ata_std_postreset(link, class);

	/* Make sure port's ATAPI bit is set appropriately */
	new_tmp = tmp = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	if (*class == ATA_DEV_ATAPI)
		new_tmp |= PORT_CMD_ATAPI;
	else
		new_tmp &= ~PORT_CMD_ATAPI;
	if (new_tmp != tmp) {
		VIXS_SATA_reg_writel(new_tmp, port_mmio + PORT_CMD, 0);
		VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0); /* flush */
	}
}

static unsigned int vixs_ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl)
{
	struct scatterlist *sg;
	struct ahci_sg *ahci_sg = cmd_tbl + AHCI_CMD_TBL_HDR_SZ;
	unsigned int si;

	VPRINTK("ENTER\n");
	/*
	 * Next, the S/G list.
	 */
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		dma_addr_t addr = sg_dma_address(sg);
		u32 sg_len = sg_dma_len(sg);

		ahci_sg[si].addr = cpu_to_le32(addr & 0xffffffff);
		ahci_sg[si].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		ahci_sg[si].flags_size = cpu_to_le32(sg_len - 1);
	}

	return si;
}

static void vixs_ahci_qc_prep(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_port_priv *pp = ap->private_data;
	int is_atapi = ata_is_atapi(qc->tf.protocol);
	void *cmd_tbl;
	u32 opts;
	const u32 cmd_fis_len = 5; /* five dwords */
	unsigned int n_elem;

	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	cmd_tbl = pp->cmd_tbl + qc->tag * AHCI_CMD_TBL_SZ;

	ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, cmd_tbl);
	if (is_atapi) {
		memset(cmd_tbl + AHCI_CMD_TBL_CDB, 0, 32);
		memcpy(cmd_tbl + AHCI_CMD_TBL_CDB, qc->cdb, qc->dev->cdb_len);
	}

	n_elem = 0;
	if (qc->flags & ATA_QCFLAG_DMAMAP)
		n_elem = vixs_ahci_fill_sg(qc, cmd_tbl);

	/*
	 * Fill in command slot information.
	 */
	opts = cmd_fis_len | n_elem << 16 | (qc->dev->link->pmp << 12);
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;
	if (is_atapi)
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;

	vixs_ahci_fill_cmd_slot(pp, qc->tag, opts);
}

static void vixs_ahci_error_intr(struct ata_port *ap, u32 irq_stat)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	struct ata_eh_info *host_ehi = &ap->link.eh_info;
	struct ata_link *link = NULL;
	struct ata_queued_cmd *active_qc;
	struct ata_eh_info *active_ehi;
	u32 serror;

	/* determine active link */
	ata_for_each_link(link, ap, EDGE)
		if (ata_link_active(link))
			break;
	if (!link)
		link = &ap->link;

	active_qc = ata_qc_from_tag(ap, link->active_tag);
	active_ehi = &link->eh_info;

	/* record irq stat */
	ata_ehi_clear_desc(host_ehi);
	ata_ehi_push_desc(host_ehi, "irq_stat 0x%08x", irq_stat);

	/* AHCI needs SError cleared; otherwise, it might lock up */
	vixs_ahci_scr_read(&ap->link, SCR_ERROR, &serror);
	vixs_ahci_scr_write(&ap->link, SCR_ERROR, serror);
	host_ehi->serror |= serror;

	/* some controllers set IRQ_IF_ERR on device errors, ignore it */
	if (hpriv->flags & AHCI_HFLAG_IGN_IRQ_IF_ERR)
		irq_stat &= ~PORT_IRQ_IF_ERR;

	if (irq_stat & PORT_IRQ_TF_ERR) {
		/* If qc is active, charge it; otherwise, the active
		 * link.  There's no active qc on NCQ errors.  It will
		 * be determined by EH by reading log page 10h.
		 */
		if (active_qc)
			active_qc->err_mask |= AC_ERR_DEV;
		else
			active_ehi->err_mask |= AC_ERR_DEV;

		if (hpriv->flags & AHCI_HFLAG_IGN_SERR_INTERNAL)
			host_ehi->serror &= ~SERR_INTERNAL;
	}

	if (irq_stat & PORT_IRQ_UNK_FIS) {
		u32 *unk = (u32 *)(pp->rx_fis + RX_FIS_UNK);

		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(active_ehi,
				  "unknown FIS %08x %08x %08x %08x" ,
				  unk[0], unk[1], unk[2], unk[3]);
	}

	if (sata_pmp_attached(ap) && (irq_stat & PORT_IRQ_BAD_PMP)) {
		active_ehi->err_mask |= AC_ERR_HSM;
		active_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(active_ehi, "incorrect PMP");
	}

	if (irq_stat & (PORT_IRQ_HBUS_ERR | PORT_IRQ_HBUS_DATA_ERR)) {
		host_ehi->err_mask |= AC_ERR_HOST_BUS;
		host_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(host_ehi, "host bus error");
	}

	if (irq_stat & PORT_IRQ_IF_ERR) {
		host_ehi->err_mask |= AC_ERR_ATA_BUS;
		host_ehi->action |= ATA_EH_RESET;
		ata_ehi_push_desc(host_ehi, "interface fatal error");
	}

	if (irq_stat & (PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)) {
		ata_ehi_hotplugged(host_ehi);
		ata_ehi_push_desc(host_ehi, "%s",
			irq_stat & PORT_IRQ_CONNECT ?
			"connection status changed" : "PHY RDY changed");
	}

	/* okay, let's hand over to EH */

	if (irq_stat & PORT_IRQ_FREEZE)
		ata_port_freeze(ap);
	else
		ata_port_abort(ap);
}

static void vixs_ahci_port_intr(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_eh_info *ehi = &ap->link.eh_info;
	struct ahci_port_priv *pp = ap->private_data;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	int resetting = !!(ap->pflags & ATA_PFLAG_RESETTING);
	u32 status, qc_active;
	int rc;

	status = VIXS_SATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VIXS_SATA_reg_writel(status, port_mmio + PORT_IRQ_STAT, 0);

	/* ignore BAD_PMP while resetting */
	if (unlikely(resetting))
		status &= ~PORT_IRQ_BAD_PMP;

	/* If we are getting PhyRdy, this is
 	 * just a power state change, we should
 	 * clear out this, plus the PhyRdy/Comm
 	 * Wake bits from Serror
 	 */
	if ((hpriv->flags & AHCI_HFLAG_NO_HOTPLUG) &&
		(status & PORT_IRQ_PHYRDY)) {
		status &= ~PORT_IRQ_PHYRDY;
		vixs_ahci_scr_write(&ap->link, SCR_ERROR, ((1 << 16) | (1 << 18)));
	}

	if (unlikely(status & PORT_IRQ_ERROR)) {
		vixs_ahci_error_intr(ap, status);
		return;
	}

	if (status & PORT_IRQ_SDB_FIS) {
		/* If SNotification is available, leave notification
		 * handling to sata_async_notification().  If not,
		 * emulate it by snooping SDB FIS RX area.
		 *
		 * Snooping FIS RX area is probably cheaper than
		 * poking SNotification but some constrollers which
		 * implement SNotification, ICH9 for example, don't
		 * store AN SDB FIS into receive area.
		 */
		if (hpriv->cap & HOST_CAP_SNTF)
			sata_async_notification(ap);
		else {
			/* If the 'N' bit in word 0 of the FIS is set,
			 * we just received asynchronous notification.
			 * Tell libata about it.
			 */
			const __le32 *f = pp->rx_fis + RX_FIS_SDB;
			u32 f0 = le32_to_cpu(f[0]);

			if (f0 & (1 << 15))
				sata_async_notification(ap);
		}
	}

	/* pp->active_link is valid iff any command is in flight */
	if (ap->qc_active && pp->active_link->sactive)
		qc_active = VIXS_SATA_reg_readl(port_mmio + PORT_SCR_ACT, 0);
	else
		qc_active = VIXS_SATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);

	rc = ata_qc_complete_multiple(ap, qc_active);

	/* while resetting, invalid completions are expected */
	if (unlikely(rc < 0 && !resetting)) {
		ehi->err_mask |= AC_ERR_HSM;
		ehi->action |= ATA_EH_RESET;
		ata_port_freeze(ap);
	}
}

static irqreturn_t vixs_ahci_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host;
	struct ahci_host_priv *hpriv;
	unsigned int i, handled = 0;
	void __iomem *mmio;
	u32 irq_stat, irq_masked;
#ifdef USE_IIA
	/* sigh.  0xffffffff is a valid return from h/w */
	if(!IIALocalReadInt(IIA_SATA_INT))
	    return IRQ_NONE;
#endif

	host = vixs_ata_host[0];
	hpriv = host->private_data;
	mmio = host->iomap[AHCI_PCI_BAR];

	irq_stat = VIXS_SATA_reg_readl(mmio + HOST_IRQ_STAT, 0);
	if (!irq_stat)
	{
		if(!vixs_ata_host[1])
			return IRQ_NONE;

		host = vixs_ata_host[1];
		hpriv = host->private_data;
		mmio = host->iomap[AHCI_PCI_BAR];
		

		irq_stat = VIXS_SATA_reg_readl(mmio + HOST_IRQ_STAT, 0);
		if(!irq_stat)
			return IRQ_NONE;
	}

	VPRINTK("ENTER\n");
	irq_masked = irq_stat & hpriv->port_map;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		if (!(irq_masked & (1 << i)))
			continue;

		ap = host->ports[i];
		if (ap) {
			vixs_ahci_port_intr(ap);
			VPRINTK("port %u\n", i);
		} else {
			VPRINTK("port %u (no irq)\n", i);
			if (ata_ratelimit())
				dev_printk(KERN_WARNING, host->dev,
					"interrupt on disabled port %u\n", i);
		}

		handled = 1;
	}

	/* HOST_IRQ_STAT behaves as level triggered latch meaning that
	 * it should be cleared after all the port events are cleared;
	 * otherwise, it will raise a spurious interrupt after each
	 * valid one.  Please read section 10.6.2 of ahci 1.1 for more
	 * information.
	 *
	 * Also, use the unmasked value to clear interrupt as spurious
	 * pending event on a dummy port might cause screaming IRQ.
	 */
	VIXS_SATA_reg_writel(irq_stat, mmio + HOST_IRQ_STAT, 0);

#ifdef CONFIG_PLAT_XCODE64xx
    writel(hpriv->index?SATA_INT_STATUS_SATA1_MASK:SATA_INT_STATUS_SATA0_MASK, (void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS));
#else
    writel(SATA_INT_STATUS_SATA0_MASK, (void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS));
#endif

	spin_unlock(&host->lock);

	VPRINTK("EXIT\n");

	return IRQ_RETVAL(handled);
}

static unsigned int vixs_ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	/* Keep track of the currently active link.  It will be used
	 * in completion path to determine whether NCQ phase is in
	 * progress.
	 */
	pp->active_link = qc->dev->link;

	if (qc->tf.protocol == ATA_PROT_NCQ)
		VIXS_SATA_reg_writel(1 << qc->tag, port_mmio + PORT_SCR_ACT, 0);
	VIXS_SATA_reg_writel(1 << qc->tag, port_mmio + PORT_CMD_ISSUE, 0);
	VIXS_SATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);	/* flush */

	return 0;
}

static bool vixs_ahci_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct ahci_port_priv *pp = qc->ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	ata_tf_from_fis(d2h_fis, &qc->result_tf);
	return true;
}

static void vixs_ahci_freeze(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	/* turn IRQ off */
	VIXS_SATA_reg_writel(0, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_thaw(struct ata_port *ap)
{
	void __iomem *mmio = ap->host->iomap[AHCI_PCI_BAR];
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	struct ahci_port_priv *pp = ap->private_data;
	/* clear IRQ */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_IRQ_STAT, 0);
	VIXS_SATA_reg_writel(1 << ap->port_no, mmio + HOST_IRQ_STAT, 0);

	/* turn IRQ back on */
	VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_error_handler(struct ata_port *ap){
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		vixs_ahci_stop_engine(ap);
		vixs_ahci_start_engine(ap);
	}

	sata_pmp_error_handler(ap);
}

static void vixs_ahci_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	/* make DMA engine forget about the failed command */
	if (qc->flags & ATA_QCFLAG_FAILED)
		vixs_ahci_kick_engine(ap, 1);
}

static void vixs_ahci_pmp_attach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	cmd |= PORT_CMD_PMP;
	VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);

	pp->intr_mask |= PORT_IRQ_BAD_PMP;
	VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_pmp_detach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;
	cmd = VIXS_SATA_reg_readl(port_mmio + PORT_CMD, 0);
	cmd &= ~PORT_CMD_PMP;
	VIXS_SATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
	pp->intr_mask &= ~PORT_IRQ_BAD_PMP;
	VIXS_SATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static int vixs_ahci_port_resume(struct ata_port *ap)
{
	vixs_ahci_power_up(ap);

	vixs_ahci_start_port(ap);

	if (sata_pmp_attached(ap))
		vixs_ahci_pmp_attach(ap);
	else
		vixs_ahci_pmp_detach(ap);

	return 0;
}

#ifdef CONFIG_PM
static int vixs_ahci_port_suspend(struct ata_port *ap, pm_message_t mesg)
{
	const char *emsg = NULL;
	int rc;
	rc = vixs_ahci_deinit_port(ap, &emsg);
	if (rc == 0)
		vixs_ahci_power_down(ap);
	else {
		ata_port_printk(ap, KERN_ERR, "%s (%d)\n", emsg, rc);
		vixs_ahci_start_port(ap);
	}

	return rc;
}
#endif

static int vixs_ahci_port_start(struct ata_port *ap)
{
	struct device *dev = ap->host->dev;
	struct ahci_port_priv *pp;
	void *mem;
	dma_addr_t mem_dma;
	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	mem = dmam_alloc_coherent(dev, AHCI_PORT_PRIV_DMA_SZ, &mem_dma,
				  GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	memset(mem, 0, AHCI_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot = mem;
	pp->cmd_slot_dma = mem_dma;

	mem += AHCI_CMD_SLOT_SZ;
	mem_dma += AHCI_CMD_SLOT_SZ;

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = mem;
	pp->rx_fis_dma = mem_dma;

	mem += AHCI_RX_FIS_SZ;
	mem_dma += AHCI_RX_FIS_SZ;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = mem;
	pp->cmd_tbl_dma = mem_dma;

	/*
	 * Save off initial list of interrupts to be enabled.
	 * This could be changed later
	 */
	pp->intr_mask = DEF_PORT_IRQ;

	ap->private_data = pp;

	/* engage engines, captain */
	return vixs_ahci_port_resume(ap);
}

static void vixs_ahci_port_stop(struct ata_port *ap)
{
	const char *emsg = NULL;
	int rc;
	/* de-initialize port */
	rc = vixs_ahci_deinit_port(ap, &emsg);
	if (rc)
		ata_port_printk(ap, KERN_WARNING, "%s (%d)\n", emsg, rc);
}

#ifdef CONFIG_SATA_RXPN_SWAP_FIX
#error a
static void SATAWritePHYReg(int addr, int data)
{
	writel(addr|0x10000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("1");
	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("2");

	writel(data|0x20000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("3");
	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("4");

	writel(0x40000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("3");
	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	mdelay(100);
	if(readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK)
		panic("4");
}
#endif

#define POWER_OFF_RXPLL_SATA 0x2335400b
#define POWER_ON_RXPLL_SATA  0x2f35400b
#define RESET_PHY 0x1
#define RESET_SATA0 0x3f
#define RESET_SATA1 0xcf
#define RESET_ALL   0xff

#define SATA_EXTERNAL_CLOCK  0
#define SATA_INTERNAL_CLOCK_25  25
#define SATA_INTERNAL_CLOCK_50  50
#define SATA_INTERNAL_CLOCK_62_5  62
#define SATA_INTERNAL_CLOCK_75  75
#define SATA_INTERNAL_CLOCK_100  100
#define SATA_INTERNAL_CLOCK_125  125

void SATA_SET_CLOCK(int sata_clock, int mode,int sata0_disable, int sata1_disable)
{
#ifdef CONFIG_PLAT_XCODE64xx    
	switch(sata_clock)
	{
    case SATA_INTERNAL_CLOCK_100:
		RegMaskWrite(CG1_CLK_SRC_SEL3, 3 << CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0464,0x07f6);//SATA clock frequency: 100MHZ
		break;
    case SATA_INTERNAL_CLOCK_50:
		RegMaskWrite(CG1_CLK_SRC_SEL3, 1 << CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0460,0x07f6);//SATA clock frequency: 50MHZ
		break;
	default: //SATA_INTERNAL_CLOCK_100
		RegMaskWrite(CG1_CLK_SRC_SEL3, 3 << CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_SATACLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0464,0x07f6);//SATA clock frequency: 100MHZ
		break;
	}

	if(vixs_ahci_clk_type == 0) // internal clock
	{
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x2000,0x2000);//Set to internal clock frequency
	}
	else if(vixs_ahci_clk_type == 1) // external clock
	{
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0000,0x2000);//Set to external clock frequency
	}
    else // default: internal clock
    {
		RegMaskWrite(SATA_PHY_MPLL_CTRL,0x2000,0x2000);//Set to internal clock frequency
	}      
    
	RegMaskWrite(SATA_PHY_MPLL_CTRL,0x1000,0x1000);//turn off mp_ck
	RegMaskWrite(SATA_PHY_MPLL_CTRL,0x0000,0x1000);//turn on mp_ck
	RegMaskWrite(SATA_PHY_ANALOG_CTRL,0x14,0x1f);
	regWrite(SATA0_PHY_TXRX_CTRL,POWER_OFF_RXPLL_SATA);
	regWrite(SATA1_PHY_TXRX_CTRL,POWER_OFF_RXPLL_SATA);
	if(sata0_disable == 0)
		RegMaskWrite(SATA_SFT_RESETS,RESET_SATA0,RESET_SATA0 );
	if(sata1_disable == 0)
		RegMaskWrite(SATA_SFT_RESETS,RESET_SATA1,RESET_SATA1 );



	//if need be, change the clocks here using phy registers

	if(mode == 1)//manual reset for SATA
		udelay (160);
	//turn on PHYs!!!!

	RegMaskWrite(SATA_PHY_MPLL_CTRL,0x1,0x1);

	if(mode == 1)//manual reset for SATA
	{
		udelay(160) ;
		regWrite(SATA0_PHY_TXRX_CTRL,POWER_ON_RXPLL_SATA);
		regWrite(SATA1_PHY_TXRX_CTRL,POWER_ON_RXPLL_SATA);
		udelay(500) ;//This is for the fpga, it can be changed later...
	}
#else
    if(vixs_ahci_clk_type == 0) // internal clock
    {
        RegMaskWrite(SATA_PHY6G_CTRL0, 0 << SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_MASK);
    }
    else if(vixs_ahci_clk_type == 1) // external clock
    {
        RegMaskWrite(SATA_PHY6G_CTRL0, 1 << SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_MASK);
    }
    
    switch(sata_clock)
    {
    case SATA_INTERNAL_CLOCK_50:
        RegMaskWrite(CG1_CLK_SRC_SEL3, 0 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
        RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
        RegMaskWrite(SATA_PHY6G_CTRL0, 0x3C << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
        break;
    case SATA_INTERNAL_CLOCK_100:
	RegMaskWrite(SATA_PHY6G_CTRL0, 0x1E << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
        RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
        RegMaskWrite(SATA_PHY6G_CTRL1, 0  << SATA_PHY6G_CTRL1_PHY_SS_REF_CLK_SEL_SHIFT, SATA_PHY6G_CTRL1_PHY_SS_REF_CLK_SEL_MASK);
	 if(vixs_ahci_clk_type == 0) // internal clock
	   {
	   udelay(100);
        RegMaskWrite(CG1_CLK_SRC_SEL3, 1 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
	    }

        break;
    case SATA_INTERNAL_CLOCK_25:
        RegMaskWrite(CG1_CLK_SRC_SEL3, 2 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
        RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
        RegMaskWrite(SATA_PHY6G_CTRL0, 0x78 << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
        break;
    default: //SATA_INTERNAL_CLOCK_100
        RegMaskWrite(CG1_CLK_SRC_SEL3, 1 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
        RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
        RegMaskWrite(SATA_PHY6G_CTRL0, 0x1E << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
        break;
    }

 #if 0
    if(sata0_disable == 0)
        RegMaskWrite(SATA_SFT_RESETS,RESET_SATA0,RESET_SATA0 );
    if(sata1_disable == 0)
        RegMaskWrite(SATA_SFT_RESETS,RESET_SATA1,RESET_SATA1 );

    RegPoll(SATA_PHY_COMMON_STATUS, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, 10, 10000);
    RegMaskWrite(SATA_SFT_RESETS,RESET_ALL,RESET_ALL );
#endif
#endif
}

static int xcode_ahci_host_init(void )
{
#ifdef CONFIG_PLAT_XCODE64xx
    u32 temp;

    /* Reset SATA */
    temp = readl(XC_SOC_PROC_MMREG_BASE+ ACC_RESET_REG0);
    temp |= SATA_RESET_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE+ ACC_RESET_REG0);
    udelay(1000);

	//Enable SATA clock
	temp = readl(XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0);
	temp &= ~ACC_BLK_STOP0_SATA_BLK_STOP_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE+ ACC_BLK_STOP0);

	temp = readl(XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0);
	temp &= ~CG1_CLK_STOP0_SATACLK_STOP_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE+ CG1_CLK_STOP0);

	/* Take SATA out of reset */
	temp = readl(XC_SOC_PROC_MMREG_BASE+ ACC_RESET_REG0);
	temp &= ~SATA_RESET_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE+ ACC_RESET_REG0);
	udelay(1000);

	/* start to SW reset */
	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_SFT_RESETS);

    SATA_SET_CLOCK(vixs_ahci_clk, 1, 0, 0);

    RegMaskWrite(SATA_SFT_RESETS, RESET_SATA0,RESET_SATA0);
    RegMaskWrite(SATA_SFT_RESETS, RESET_SATA1,RESET_SATA1);
    udelay(500) ;//FIXME, waiting too long??

    //set mpll frequency
    temp = readl((void*)(XC_SOC_PROC_MMREG_BASE + SATA_PHY_MPLL_CTRL));

	if(vixs_ahci_ssmode)
	{
		temp |= SATA_PHY_MPLL_SS_EN_MASK;
	}
	else
	{
		temp &= ~SATA_PHY_MPLL_SS_EN_MASK;
	}
    writel(temp, (void*)(XC_SOC_PROC_MMREG_BASE + SATA_PHY_MPLL_CTRL));
#else


    u32  sata0_speed = 2; // 1 for Gen1, 2 for Gen2
    if(vixs_ahci_clk_type == 0) // internal clock 
    {
       RegMaskWrite(CG1_CLK_SRC_EN0, CG1_CLK_SRC_EN0_MOCA_REFCLK_SRC_EN_MASK,CG1_CLK_SRC_EN0_MOCA_REFCLK_SRC_EN_MASK );     // MOCA is needed for RBM
       RegMaskWrite(CG1_CLK_STOP0, 0,CG1_CLK_STOP0_MOCA_REFCLK_STOP_MASK );     // MOCA is needed for RBM 
       DPRINTK("%s:%d internal clock type\n",__func__,__LINE__);
    } 
    /* CG Reset SATA */
    xcode_setval(1, PADU_CTRL_MOCA, RBM_PADU_CTRL);     // MOCA is needed for RBM
    udelay(100);

    if(RegPoll(RBM_PADU_CTRL, RBM_PADU_CTRL_PADU_CTRL_MOCA_MASK, RBM_PADU_CTRL_PADU_CTRL_MOCA_MASK, 10, 1000)){
        DPRINTK(":%d RBM_PADU_CTRL:%x \n", __LINE__,regRead( RBM_PADU_CTRL));
 	    return -1;
    }
    udelay(100);

    xcode_setval(1, MOCA_RESET, ACC_RESET_REG0);
    xcode_setval(1, SATA_RESET, ACC_RESET_REG0);

    xcode_setval(0, MOCA_BLK_STOP, ACC_BLK_STOP0);
    xcode_setval(0, MOCACLK_STOP, CG1_CLK_STOP0);
    xcode_setval(0, MOCA_RESET, ACC_RESET_REG0);
    xcode_setval(0, SATA_RESET, ACC_RESET_REG0);
    udelay(100);

    /* start to SW reset */
    xcode_writel(0, SATA_SFT_RESETS);
    udelay(100);
    xcode_setval(0, SATA_RESET_MODE, SATA_CONTROL);

    #define MOCA_DUMMY_REG_0 0x3160
    
    if (sata0_speed == 1) 
    {
        // Gen1
        xcode_setval(0, SATA0_PHY_SPDMODE, SATA_CONTROL);

        #ifdef CONFIG_FPGA_BUILD
        xcode_writel(0x100, MOCA_DUMMY_REG_0);
        #endif
    }
    else    
    {
        // Gen2
        xcode_setval(1, SATA0_PHY_SPDMODE, SATA_CONTROL);
        xcode_setval(0, SATA0_LOCAL_CLK_GATE, SATA_CONTROL);
        #ifdef CONFIG_FPGA_BUILD
        xcode_writel(0x102, MOCA_DUMMY_REG_0);
        #endif
    }

    SATA_SET_CLOCK(vixs_ahci_clk, 1, 0, 0);
   
    xcode_setval(0x9,SATA_PHY_LOS_LVL,SATA_PHY_ANALOG_CTRL);
    xcode_setval(0x2,PHY_LOS_BIAS,SATA_PHY6G_CTRL0);
    xcode_writel(0x231818, SATA_PHY6G_PRMPH);
    
   
   DPRINTK("new SATA_PHY_ANALOG_CTRL:%x,SATA_PHY6G_CTRL0:%x\n",   SATA_PHY_ANALOG_CTRL,SATA_PHY6G_CTRL0);
   DPRINTK("SATA_PHY_ANALOG_CTRL:%x,SATA_PHY6G_CTRL0:%x\n",   xcode_readl(SATA_PHY_ANALOG_CTRL),xcode_readl(SATA_PHY6G_CTRL0));
   DPRINTK("SATA_PHY6G_PRMPH:%x\n",   xcode_readl(SATA_PHY6G_PRMPH));

    xcode_writel(1, SATA_SFT_RESETS);

    DPRINTK(":%d SATA_PHY_COMMON_STATUS:%x \n", __LINE__,regRead( SATA_PHY_COMMON_STATUS));
    if (RegPoll(SATA_PHY_COMMON_STATUS, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, 10, 10000)) {
        DPRINTK(":%d SATA_PHY_COMMON_STATUS:%x \n", __LINE__,regRead( SATA_PHY_COMMON_STATUS));
		return -1;
    }
	xcode_writel(0x40000037, SATA_SFT_RESETS);
#endif

#ifdef CONFIG_SATA_RXPN_SWAP_FIX
	SATAWritePHYReg(0x2107, 0x8);
#endif
	return 0;
}

static void vixs_ahci_set_oob(void __iomem *mmio, u32 clock)
{
        //set up OOB register here for CLK setting
    printk("vixs_ahci_set_oob, clock=%d\n", clock);
        
#ifdef CONFIG_FPGA_BUILD

    VIXS_SATA_reg_writel(0x84101730, mmio + HOST_OOB, 0);
    VIXS_SATA_reg_writel(0x84101730, mmio + HOST_OOB, 0);

#else

#ifdef CONFIG_PLAT_XCODE64xx

    switch(clock)
    {
        case 25:
        case 100:
        case 50:
            VIXS_SATA_reg_writel(0x82060b13, mmio + HOST_OOB, 0);
            VIXS_SATA_reg_writel(0x82060b13, mmio + HOST_OOB, 0);
            break;
        case 75:
            VIXS_SATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
            VIXS_SATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
            break;
    }
#else

    switch(clock)
    {
        case 25:
        case 100:
        case 50:
            VIXS_SATA_reg_writel(0x870E192B, mmio + HOST_OOB, 0);
            VIXS_SATA_reg_writel(0x870E192B, mmio + HOST_OOB, 0);
            break;
        case 75:
            VIXS_SATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
            VIXS_SATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
            break;
    }

#endif

#endif
}


static void vixs_ahci_port_init(struct platform_device *pdev, struct ata_port *ap,
			   int port_no, void __iomem *mmio,
			   void __iomem *port_mmio)
{
	const char *emsg = NULL;
	int rc;
	u32 tmp;

	/* make sure port is not active */
	rc = vixs_ahci_deinit_port(ap, &emsg);
	if (rc)
		dev_printk(KERN_WARNING, &pdev->dev,
			   "%s (%d)\n", emsg, rc);

	/* clear SError */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_SCR_ERR, 0);
	VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
	VIXS_SATA_reg_writel(tmp, port_mmio + PORT_SCR_ERR, 0);

	/* clear port IRQ */
	tmp = VIXS_SATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
	if (tmp)
		VIXS_SATA_reg_writel(tmp, port_mmio + PORT_IRQ_STAT, 0);

	VIXS_SATA_reg_writel(1 << port_no, mmio + HOST_IRQ_STAT, 0);
}

static void vixs_ahci_init_controller(struct ata_host *host)
{
	struct platform_device *pdev = to_platform_device(host->dev);
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	int i;
	void __iomem *port_mmio;
	u32 tmp;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		port_mmio = ahci_port_base(ap);
		if (ata_port_is_dummy(ap))
			continue;

		vixs_ahci_port_init(pdev, ap, i, mmio, port_mmio);
	}

	tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
	VIXS_SATA_reg_writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL, 0);
	tmp = VIXS_SATA_reg_readl(mmio + HOST_CTL, 0);
	VPRINTK("HOST_CTL 0x%x\n", tmp);
}

static void vixs_ahci_print_info(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	struct platform_device *pdev = to_platform_device(host->dev);        
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 vers, cap, impl, speed;
	const char *speed_s;
	const char *scc_s;

	vers = VIXS_SATA_reg_readl(mmio + HOST_VERSION, 0);
	cap = hpriv->cap;
	impl = hpriv->port_map;

	speed = (cap >> 20) & 0xf;
	if (speed == 1)
		speed_s = "1.5";
	else if (speed == 2)
		speed_s = "3";
	else
		speed_s = "?";

	scc_s = "SATA";

	dev_printk(KERN_INFO, &pdev->dev,
		"AHCI %02x%02x.%02x%02x "
		"%u slots %u ports %s Gbps 0x%x impl %s mode\n"
		,

		(vers >> 24) & 0xff,
		(vers >> 16) & 0xff,
		(vers >> 8) & 0xff,
		vers & 0xff,

		((cap >> 8) & 0x1f) + 1,
		(cap & 0x1f) + 1,
		speed_s,
		impl,
		scc_s);

	dev_printk(KERN_INFO, &pdev->dev,
		"flags: "
		"%s%s%s%s%s%s%s"
		"%s%s%s%s%s%s%s\n"
		,

		cap & (1 << 31) ? "64bit " : "",
		cap & (1 << 30) ? "ncq " : "",
		cap & (1 << 29) ? "sntf " : "",
		cap & (1 << 28) ? "ilck " : "",
		cap & (1 << 27) ? "stag " : "",
		cap & (1 << 26) ? "pm " : "",
		cap & (1 << 25) ? "led " : "",

		cap & (1 << 24) ? "clo " : "",
		cap & (1 << 19) ? "nz " : "",
		cap & (1 << 18) ? "only " : "",
		cap & (1 << 17) ? "pmp " : "",
		cap & (1 << 15) ? "pio " : "",
		cap & (1 << 14) ? "slum " : "",
		cap & (1 << 13) ? "part " : ""
		);
}

/**
 *	vixs_ata_host_activate - start host, request IRQ and register it
 *	@host: target ATA host
 *	@irq: IRQ to request
 *	@irq_handler: irq_handler used when requesting IRQ
 *	@irq_flags: irq_flags used when requesting IRQ
 *	@sht: scsi_host_template to use when registering the host
 *
 *	After allocating an ATA host and initializing it, most libata
 *	LLDs perform three steps to activate the host - start host,
 *	request IRQ and register it.  This helper takes necessasry
 *	arguments and performs the three steps in one go.
 *
 *	An invalid IRQ skips the IRQ registration and expects the host to
 *	have set polling mode on the port. In this case, @irq_handler
 *	should be NULL.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int vixs_ata_host_activate(struct ata_host *host, int irq,
		      irq_handler_t irq_handler, unsigned long irq_flags,
		      struct scsi_host_template *sht)
{
	int i, rc;
	int irq_requested=0;

	rc = ata_host_start(host);
	if (rc)
		return rc;

	/* Special case for polling mode */
	if (!irq) {
		WARN_ON(irq_handler);
		return ata_host_register(host, sht);
	}

//	rc = devm_request_irq(host->dev, irq, irq_handler, irq_flags,
//			      dev_driver_string(host->dev), host);

	//As the SATA0/1 are sharing IRQ, we don't request twice
	if(g_next_host_index==0)
	{
		printk("Request irq %d %lx\n", irq, irq_flags);
		rc = request_irq(irq, irq_handler, irq_flags,
				      dev_driver_string(host->dev), vixs_ata_host);
		if (rc)
		{
			printk("Request irq failed\n");
			return rc;
		}

		irq_requested=1;
	}

       /********************************************/
       /* JLWANG: Enable interrupt here                                          */
       /********************************************/
       writel(readl((void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS)), (void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS));
       writel(readl((void*)(XC_SOC_PROC_MMREG_BASE + SATA_HOST_INT_MASK))|(8<<(g_next_host_index*16)), (void*)(XC_SOC_PROC_MMREG_BASE + SATA_HOST_INT_MASK));
#ifdef USE_IIA
       IIALocalSetMask(IIA_SATA_INT);
#endif
   

	for (i = 0; i < host->n_ports; i++)
		ata_port_desc(host->ports[i], "irq %d", irq);

	rc = ata_host_register(host, sht);
	/* if failed, just free the IRQ and leave ports alone */
	if (rc && irq_requested==1)
//		devm_free_irq(host->dev, irq, host);
		free_irq(irq, host);

	return rc;
}
    
static int vixs_ahci_init_one( struct platform_device *pdev)
{
	static int printed_version;
	struct ata_port_info pi = vixs_ahci_port_info[g_next_host_index];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	int n_ports, i, rc;
       struct resource *res;   
	unsigned long base;
       u32 irq;

	VPRINTK("ENTER\n");

	WARN_ON((int)ATA_MAX_QUEUE > (int)AHCI_MAX_CMDS);

       /*********************************************************************/
       /*  Clear interrupt here first to avoid unexpected SATA interrupt during initialization */
       /*********************************************************************/
#ifdef USE_IIA
 	IIALocalClearMask(IIA_SATA_INT);
#endif

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " VIXS_DRV_VERSION "\n");

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;
	hpriv->flags |= (unsigned long)pi.private_data;

        hpriv->index = g_next_host_index;


        //?? JLWANG  : ToDo --> Set the correct base address		
        res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
        if (!res) {
            dev_err(&pdev->dev,
                    "Found XCODE AHCI with no IRQ. Check %s setup!\n",
                    dev_name(&pdev->dev));
                return  -ENODEV;
        }
        irq = res->start;

        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
            dev_err(&pdev->dev,
                    "Found XCODE AHCI with no register addr. Check %s setup!\n",
                    dev_name(&pdev->dev));
            return -ENODEV;        
        }


        //JLWANG: Don't do ioremap for now, since if there are more than one controller, they are share the same 
        //register region.
        //base = (unsigned long)ioremap_nocache( res->start, res->end - res->start +1);
        base = res->start;  
        if (!base) 
        {
            dev_err(&pdev->dev,"ioremap ERROR" );
            return -ENOMEM;
        }

        //store base address in platform_data field
        pdev->dev.platform_data = (void*)irq;
        VIXS_SATA_REG_BASE = (void*)base;

       /*********************************************************/
       /*    Add HW initialization Sequence here                                        */
       /*********************************************************/

        if(!g_next_host_index)
        {
			dev_printk(KERN_INFO, &pdev->dev, "Initialize the host controller...\n");
            if (xcode_ahci_host_init()) {
				dev_err(&pdev->dev, "host init failed, check the clock source\n");
				return -ENODEV;
            }
			dev_printk(KERN_INFO, &pdev->dev, "host init completed\n");
        }

	/* save initial config */
	vixs_ahci_save_initial_config(pdev, hpriv);
    
	/* prepare host */
	if (hpriv->cap & HOST_CAP_NCQ)
		pi.flags |= ATA_FLAG_NCQ;

	if (hpriv->cap & HOST_CAP_PMP)
		pi.flags |= ATA_FLAG_PMP;

	/* CAP.NP sometimes indicate the index of the last enabled
	 * port, at other times, that of the last possible port, so
	 * determining the maximum port number requires looking at
	 * both CAP.NP and port_map.
	 */
	n_ports = max(ahci_nr_ports(hpriv->cap), fls(hpriv->port_map));
    
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host)
		return -ENOMEM;


	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	vixs_ata_host[g_next_host_index]=host;

	host->iomap = g_vixs_iomap[g_next_host_index];        //JLWANG: Set it to 0 now.
	host->private_data = hpriv;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

//		/* set initial link pm policy */
//		ap->pm_policy = NOT_AVAILABLE;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}


	rc = vixs_ahci_reset_controller(host);
	if (rc)
		return rc;

    vixs_ahci_set_oob(host->iomap[AHCI_PCI_BAR], vixs_ahci_clk);

	vixs_ahci_init_controller(host);
	vixs_ahci_print_info(host);


	printk("Activate ahci host\n");
	rc = vixs_ata_host_activate(host, irq, vixs_ahci_interrupt, IRQF_SHARED,
				 &vixs_ahci_sht);


	g_next_host_index++;
    g_vixs_ahci_active_cnt++;

        return rc    ;
}



static int vixs_ahci_remove_one (struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);

    g_vixs_ahci_active_cnt--;
    
        return 0;
}

static struct platform_device *vixs_ahci_device[2] = {NULL, NULL};
static struct resource vixs_ahci_resources[2];



#ifdef CONFIG_SATA_VIXS_BT_OPT
DECLARE_WAIT_QUEUE_HEAD(vixs_sata_wq);
int vixs_sata_wq_flag = 0;

static void sata_wq_handler(struct work_struct *w);


typedef struct {
  struct work_struct my_work;
} my_work_t;



static my_work_t *work;


static int __init vixs_ahci_init(void)
{
    int ret=-1;

        work = (my_work_t *)kmalloc(sizeof(my_work_t), GFP_KERNEL);
	if(work){
 	    INIT_WORK( (struct work_struct *)work, sata_wq_handler );
	    ret = schedule_work((struct work_struct*) work);
	}

    return ret;
}
static int __vixs_ahci_init(void);

static void sata_wq_handler(struct work_struct *w){
    wait_event(vixs_sata_wq,vixs_sata_wq_flag!=0);
    __vixs_ahci_init();
    kfree(w);
}
#endif


#ifdef CONFIG_SATA_VIXS_BT_OPT
static int __vixs_ahci_init(void)
#else
static int __init vixs_ahci_init(void)
#endif
{
    int error;
	
    VPRINTK("START: ViXs AHCI module init\n");

    g_next_host_index = 0;

    vixs_ahci_device[0] = platform_device_alloc(VIXS_AHCI_HOST0_NAME, -1);
    if (!vixs_ahci_device[0]) {
        error = -ENOMEM;
        return error;
    }

    
    //memory resources
    vixs_ahci_resources[0].start = SATA_AHB_REG_CTRL + XC_SOC_PROC_MMREG_BASE;
    vixs_ahci_resources[0].end = vixs_ahci_resources[0].start + 0x400 - 1;
    vixs_ahci_resources[0].flags = IORESOURCE_MEM;

    //irq resources
    vixs_ahci_resources[1].start = VIXS_SATA_IRQ;
    vixs_ahci_resources[1].end = 0;
    vixs_ahci_resources[1].flags = IORESOURCE_IRQ;

    error = platform_device_add_resources(vixs_ahci_device[0], vixs_ahci_resources, 2);
    if (error)
        goto err_free_device0;

    error = platform_device_add(vixs_ahci_device[0]);
    if (error)
        goto err_free_device0;

#ifdef CONFIG_PLAT_XCODE64xx
    vixs_ahci_device[1] = platform_device_alloc(VIXS_AHCI_HOST1_NAME, -1);
    if (!vixs_ahci_device[1]) {
        error = -ENOMEM;
        return error;
    }

    
    //memory resources
    vixs_ahci_resources[0].start = SATA_AHB_REG_CTRL + XC_SOC_PROC_MMREG_BASE;
    vixs_ahci_resources[0].end = vixs_ahci_resources[0].start + 0x400 - 1;
    vixs_ahci_resources[0].flags = IORESOURCE_MEM;

    //irq resources
    vixs_ahci_resources[1].start = VIXS_SATA_IRQ;
    vixs_ahci_resources[1].end = 0;
    vixs_ahci_resources[1].flags = IORESOURCE_IRQ;

    error = platform_device_add_resources(vixs_ahci_device[1], vixs_ahci_resources, 2);
    if (error)
        goto err_free_device1;

    error = platform_device_add(vixs_ahci_device[1]);
    if (error)
        goto err_free_device1;
#endif

    //initialize the DMA mutex

    spin_lock_init(&vixs_sata_indirect_reg_lock);

    error = platform_driver_register(&vixs_ahci_driver0);
    if (error < 0)
        goto err_free_device0;

#ifdef CONFIG_PLAT_XCODE64xx
    error = platform_driver_register(&vixs_ahci_driver1);
    if (error < 0)
        goto err_free_device0;
#endif

    VPRINTK("END: ViXs AHCI Host registed to platform bus\n");

    return 0;
err_free_device0:
    if(vixs_ahci_device[0])
    {
        platform_device_put(vixs_ahci_device[0]);
    }

#ifdef CONFIG_PLAT_XCODE64xx

err_free_device1:
    if(vixs_ahci_device[1])
    {
        platform_device_put(vixs_ahci_device[1]);
    }

#endif

    //If there is any Host working, return 0 here to keep it enabled
    if(vixs_ahci_device[0]||vixs_ahci_device[1])
    {
        return 0;
    }
    else
    {
        return error;	
    }
}

static void __exit vixs_ahci_exit(void)
{
    if(vixs_ahci_device[0] || vixs_ahci_device[1] )
    {
        if(vixs_ahci_device[0])
        {
            platform_device_del(vixs_ahci_device[0]);
            vixs_ahci_device[0] = NULL;
            platform_driver_unregister(&vixs_ahci_driver0);      

        }

        if(vixs_ahci_device[1])
        {
            platform_device_del(vixs_ahci_device[1]);
            vixs_ahci_device[1] = NULL;
            platform_driver_unregister(&vixs_ahci_driver1);      
            
        }

    }
}
MODULE_AUTHOR("Jerry Wang");
MODULE_DESCRIPTION("Vixs AHCI SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VIXS_DRV_VERSION);

module_init(vixs_ahci_init);
module_exit(vixs_ahci_exit);


static int __init xc_set_sata_clk_type(char * str)
{
	sscanf(str, "%d", &vixs_ahci_clk_type);
	printk("Set SATA clock type to %x\n",vixs_ahci_clk_type);

	return 1;
}

static int __init xc_set_sata_clk_freq(char * str)
{
	sscanf(str, "%d", &vixs_ahci_clk);
	printk("Set SATA clock to %d\n",vixs_ahci_clk);

	return 1;
}

static int __init xc_set_sata_ssmode(char * str)
{
	sscanf(str, "%x", &vixs_ahci_ssmode);
	printk("Set SATA ssmode to %x\n",vixs_ahci_ssmode);

	return 1;
}

void dump_xc6_sata_register(struct ata_port *ap){
    void __iomem *port_mmio = ahci_port_base(ap);
    u32 val=0;
    u32 i=0;
      i =0xb4;
        val = VIXS_SATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);
        i =0xb8;
        val = VIXS_SATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);

    for(i=0;i<=0x44;i+=4) {
        val = VIXS_SATA_reg_readl(port_mmio + i, 0);
        printk("%s: port_mmio+i:%p val:%x\n",__func__,port_mmio+i,val);
    }
     
   printk("CG1_CLK_SRC_EN0:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(CG1_CLK_SRC_EN0),xcode_readl(CG1_CLK_STOP0));
   printk("RBM_PADU_CTRL:%x,ACC_RESET_REG0:%x\n",   xcode_readl(RBM_PADU_CTRL),xcode_readl(ACC_RESET_REG0));
   printk("ACC_BLK_STOP0:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(ACC_BLK_STOP0),xcode_readl(CG1_CLK_STOP0));
   printk("ACC_RESET_REG0:%x,SATA_SFT_RESETS:%x\n",   xcode_readl(ACC_RESET_REG0),xcode_readl(SATA_SFT_RESETS));
   printk("SATA_CONTROL:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(SATA_CONTROL),xcode_readl(CG1_CLK_STOP0));
   printk("CG1_CLK_SRC_SEL3:%x,SATA_PHY6G_CTRL0:%x\n",   xcode_readl(CG1_CLK_SRC_SEL3),xcode_readl(SATA_PHY6G_CTRL0));

     for(i=0xd000;i<=0xd07c;i+=4) {
        printk("%x: val:%x\n",i, xcode_readl(i));
    }   
       
if (VIXS_SATA_reg_readl(port_mmio + PORT_IRQ_MASK, 0) == 0){
	printk("enable int mask because it is 0!\n");
   	VIXS_SATA_reg_writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK, 0);
  i =0xb4;
        val = VIXS_SATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);
        i =0xb8;
        val = VIXS_SATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);

    for(i=0;i<=0x44;i+=4) {
        val = VIXS_SATA_reg_readl(port_mmio + i, 0);
        printk("%s: port_mmio+i:%p val:%x\n",__func__,port_mmio+i,val);
    }
     
   printk("CG1_CLK_SRC_EN0:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(CG1_CLK_SRC_EN0),xcode_readl(CG1_CLK_STOP0));
   printk("RBM_PADU_CTRL:%x,ACC_RESET_REG0:%x\n",   xcode_readl(RBM_PADU_CTRL),xcode_readl(ACC_RESET_REG0));
   printk("ACC_BLK_STOP0:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(ACC_BLK_STOP0),xcode_readl(CG1_CLK_STOP0));
   printk("ACC_RESET_REG0:%x,SATA_SFT_RESETS:%x\n",   xcode_readl(ACC_RESET_REG0),xcode_readl(SATA_SFT_RESETS));
   printk("SATA_CONTROL:%x,CG1_CLK_STOP0:%x\n",   xcode_readl(SATA_CONTROL),xcode_readl(CG1_CLK_STOP0));
   printk("CG1_CLK_SRC_SEL3:%x,SATA_PHY6G_CTRL0:%x\n",   xcode_readl(CG1_CLK_SRC_SEL3),xcode_readl(SATA_PHY6G_CTRL0));


  for(i=0xd000;i<=0xd07c;i+=4) {
        printk("%x: val:%x\n",i, xcode_readl(i));
   }   
}

}
EXPORT_SYMBOL_GPL(dump_xc6_sata_register);
__setup("sata_clktype=", xc_set_sata_clk_type);
__setup("sata_clkfreq=", xc_set_sata_clk_freq);
__setup("sata_ssmode=", xc_set_sata_ssmode);



