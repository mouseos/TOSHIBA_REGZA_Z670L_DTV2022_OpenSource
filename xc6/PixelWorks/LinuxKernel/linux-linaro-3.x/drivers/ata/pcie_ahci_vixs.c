/*
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
#include <linux/errno.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <plat/xcodeRegDef.h>
#include "ahci.h"
#include "pcie_ahci_vixs.h"

#define VIXS_PCI_DRV_NAME	"xcode-pcie-ahci"
#define VIXS_PCI_DRV_VERSION	"1.0"
#define VIXS_PCI_AHCI_HOST0_NAME	"xcode-pcie-ahci-host0"
#define VIXS_PCI_AHCI_HOST1_NAME	"xcode-pcie-ahci-host1"
#define VSATA_RETRY_LIMIT		(1000)

#define VMMR_READ(reg)          	readl( (volatile u32*)((u32)g_XC4_AHCI_info.mp_pmmr + reg) )
#define VMMR_WRITE(reg, data)       writel( (data), (volatile u32*)((u32)g_XC4_AHCI_info.mp_pmmr + reg) )
#define VMMFB_READ(addr)          	readl( (volatile u8*)((u32)g_XC4_AHCI_info.mp_pmmfb + addr) )
#define VMMFB_WRITE(addr, data)		writel( (data), (volatile u8*)((u32)g_XC4_AHCI_info.mp_pmmfb + addr) )

//#define DMA_PROFILE
#ifdef DMA_PROFILE /* for DMA profiling */
static volatile u32 t1, t2, ms;
#endif

//external function
extern u32 meminit(unsigned int mmr) ;
extern int vsata_ahci_scr_read( struct ata_link *link, unsigned int sc_reg, u32 *val);
extern int vsata_ahci_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val);

extern volatile unsigned int xc5_pci_dev_initialized;
extern spinlock_t xc5_register_lock;

static u32 vixs_ahci_clk = 50; //50Mhz by default
static u32 vixs_ahci_ssmode = 1; //0: no ss mode	1: ss mode
static void __iomem* g_vixs_iomap[2][6] = {{0,0,0,0,0,0}, {0,0,0,0,0,(void*)VIXS_SATA_HOST_SEL_MASK}};
static struct ata_host *vixs_ata_host[2]={NULL, NULL};

static int vixs_ahci_skip_host_reset=0;
static u32 g_vixs_ahci_active_cnt = 0;

void* VIXS_VSATA_REG_BASE = NULL;

/*
 * The DMA throughput is 200MBps, the 64KB transfer take around 320us to complete.
 * When system busy, the DMA eingine can be occupied by firmware around 40ms, 
 * set the wait time as 60ms + Elem * 350us
 */
#define DMA_POLL_INTERVAL	5
#define MAX_DMA_TIMEOUT 	(700)
#define DMA_WAIT_TIME 		(12000)
/*
 * The DMA locks protect the indirect slot 0, direct slot b from other dirvers.
 * The register lock protect the SATA indirect register access.
 */
static spinlock_t vsata_indirect_reg_lock;
spinlock_t g_xcode5_dma0_lock;
spinlock_t g_xcode5_dmab_lock;
EXPORT_SYMBOL(g_xcode5_dma0_lock);
EXPORT_SYMBOL(g_xcode5_dmab_lock);


XC4_AHCI_priv_struct g_XC4_AHCI_info;

//static struct platform_device *gps_pcie_ahci_device[2] = {NULL, NULL};
//static struct resource gs_pcie_ahci_resources[2];
static struct pci_device_id vixs_ahci_pci_tbl[] = {
    {0x1745, 0x5000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
  };

static u32 g_next_host_index = 0;   //Since XCode may have multiple AHCI hosts, it is used to determine which host it is.


//internal function
static int vixs_ahci_stop_engine(struct ata_port *ap);
static void vixs_ahci_start_engine(struct ata_port *ap);
static void vixs_ahci_start_fis_rx(struct ata_port *ap);
static int vixs_ahci_deinit_port(struct ata_port *ap, const char **emsg);
static int vixs_ahci_do_softreset(struct ata_link *link, unsigned int *class,int pmp, unsigned long deadline,int (*check_ready)(struct ata_link *link));
static int vixs_ahci_check_ready(struct ata_link *link);


void vixs_ata_tf_to_fis(const struct ata_taskfile *tf, u8 pmp, int is_cmd, u8 *fis)
{
    u32 tmp;

    tmp = 0x27|(((pmp&0xf)|(1<<7))<<8)|(tf->command<<16)|(tf->feature<<24);
    xc4_ahci_mem_writel(cpu_to_le32(tmp), fis);
    
    tmp = tf->lbal|(tf->lbam<<8)|(tf->lbah<<16)|(tf->device<<24);
    xc4_ahci_mem_writel(cpu_to_le32(tmp), fis+4);    
    
    tmp = tf->hob_lbal|(tf->hob_lbam<<8)|(tf->hob_lbah<<16)|(tf->hob_feature<<24);
    xc4_ahci_mem_writel(cpu_to_le32(tmp), fis+8);   
    
    tmp = tf->nsect|(tf->hob_nsect<<8)|(0<<16)|(tf->ctl<<24);
    xc4_ahci_mem_writel(cpu_to_le32(tmp), fis+12);   
    
    tmp = 0;
    xc4_ahci_mem_writel(cpu_to_le32(tmp), fis+16);   

}
static unsigned int vixs_ahci_fill_sg(struct ata_queued_cmd *qc, void *cmd_tbl)
{
	struct scatterlist *sg;
	struct ahci_sg *ahci_sg = cmd_tbl + AHCI_CMD_TBL_HDR_SZ;
        struct scatterlist *sg_idx = qc->sg;
	unsigned int si;

	VPRINTK("ENTER\n");
	//DBK("qc:%p qc->n_elem:%d\n",qc,qc->n_elem);
	/*
	 * Next, the S/G list.
	 */
	//printk("%s:%d qc:%p n_elem:%d\n", __func__,__LINE__,qc,qc->n_elem);
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		
#ifdef CONFIG_VIRTUAL_XC5_SATA
	dma_addr_t addr;
	u32 sg_len;
//	DBK("qc:%p sg:%p sg+1:%p\n",qc,sg,sg+1);
        sg = sg_idx++;
       addr = sg_dma_address(sg);
      	//DBK_LOC;
	sg_len = sg_dma_len(sg);	
	DBK("addr:%x sg_len:%x :%p\n",addr, sg_len,&ahci_sg[si].addr);
//        printk(" &ahci_sg[si].addr:%p\n", &ahci_sg[si].addr);
        xc4_ahci_mem_writel(cpu_to_le32(addr & 0xffffffff), &ahci_sg[si].addr);
//	DBK("&ahci_sg[si].addr_hi:%p\n", &ahci_sg[si].addr_hi);

        xc4_ahci_mem_writel(cpu_to_le32((addr >> 16) >> 16), &ahci_sg[si].addr_hi);
//	DBK("&ahci_sg[si].flags_size:%p\n", &ahci_sg[si].flags_size);
        xc4_ahci_mem_writel(cpu_to_le32(sg_len - 1), &ahci_sg[si].flags_size);
//	DBK_LOC;
#else
	dma_addr_t addr = sg_dma_address(sg);
	u32 sg_len = sg_dma_len(sg);
		ahci_sg[si].addr = cpu_to_le32(addr & 0xffffffff);
		ahci_sg[si].addr_hi = cpu_to_le32((addr >> 16) >> 16);
		ahci_sg[si].flags_size = cpu_to_le32(sg_len - 1);
#endif
	}
        //DBK_LOC;
	return si;
}
static void vixs_ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			       u32 opts)
{
   dma_addr_t cmd_tbl_dma;
   cmd_tbl_dma = pp->cmd_tbl_dma + tag * AHCI_CMD_TBL_SZ;
#if 0
   pp->cmd_slot[tag].opts = cpu_to_le32(opts);
   pp->cmd_slot[tag].status = 0;
   pp->cmd_slot[tag].tbl_addr = cpu_to_le32(cmd_tbl_dma & 0xffffffff);
   pp->cmd_slot[tag].tbl_addr_hi = cpu_to_le32((cmd_tbl_dma >> 16) >> 16);
#else
    //DBK("&pp->cmd_slot[:%d].opts:%x\n",tag,&pp->cmd_slot[tag].opts);
    //printk("%s:%d tag:%d opts:%x cmd_tbl_dma:%x \n",__func__,__LINE__,tag, cpu_to_le32(opts),cmd_tbl_dma);
    xc4_ahci_mem_writel(cpu_to_le32(opts), &pp->cmd_slot[tag].opts);
    xc4_ahci_mem_writel(0, &pp->cmd_slot[tag].status);
    xc4_ahci_mem_writel(cpu_to_le32(cmd_tbl_dma & 0xffffffff), &pp->cmd_slot[tag].tbl_addr);
    xc4_ahci_mem_writel(cpu_to_le32((cmd_tbl_dma >> 16) >> 16), &pp->cmd_slot[tag].tbl_addr_hi)
#endif
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
	//DBK_LOC;
	/*
	 * Fill in command table information.  First, the header,
	 * a SATA Register - Host to Device command FIS.
	 */
	cmd_tbl = pp->cmd_tbl + qc->tag * AHCI_CMD_TBL_SZ;

	//DBK_LOC;
	vixs_ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, cmd_tbl);
	DBK("is_atapi:%d\n",is_atapi);
	if (is_atapi) {
            memcpy(cmd_tbl + AHCI_CMD_TBL_CDB, qc->cdb, qc->dev->cdb_len);
            memset(cmd_tbl + AHCI_CMD_TBL_CDB+qc->dev->cdb_len, 0, AHCI_CMD_SZ-qc->dev->cdb_len);
	}

	if (qc->flags & ATA_QCFLAG_DMAMAP)
            n_elem = vixs_ahci_fill_sg(qc, cmd_tbl);
	else
            n_elem = 0;

	/*
	 * Fill in command slot information.
	 */
	opts = cmd_fis_len | n_elem << 16 | (qc->dev->link->pmp << 12);
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		opts |= AHCI_CMD_WRITE;
	if (is_atapi)
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;

	//DBK("opts:%x cmd_fix_len:%d n_elem:%d\n",opts,cmd_fis_len,n_elem);
	vixs_ahci_fill_cmd_slot(pp, qc->tag, opts);
	//DBK_LOC;
}

void dump_port_status(void __iomem *port_mmio){
	DBK(" PORT_IRQ_STAT:%x PORT_IRQ_MASK:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0), VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_MASK, 0));
	DBK(" PORT_LST_ADDR:%x PORT_LST_ADDR_HI:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_LST_ADDR, 0),VIXS_VSATA_reg_readl(port_mmio + PORT_LST_ADDR_HI, 0));
	DBK(" PORT_FIS_ADDR:%x PORT_CMD_ISSUE:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_FIS_ADDR, 0),  VIXS_VSATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0));
	DBK(" PORT_SCR_STAT:%x PORT_SCR_CTL:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_STAT, 0),VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_CTL, 0));
	DBK(" PORT_SCR_ERR:%x PORT_SCR_ACT:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_ERR, 0),VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_ACT, 0));
	DBK(" PORT_SCR_NTF:%x PORT_FBS:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_NTF, 0),VIXS_VSATA_reg_readl(port_mmio + PORT_FBS, 0));
	DBK(" PORT_DEVSLP:%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_DEVSLP, 0));



		
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
		VIXS_VSATA_reg_writel(1 << qc->tag, port_mmio + PORT_SCR_ACT, 0);
      
//	dump_port_status(port_mmio);

//	while(VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0) !=0){
  //       u32 tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
    //      DBK("PORT_IRQ_STAT 0x%x\n", tmp);
//	};

	DBK("CMD_ISSUE:%x write:%x \n",VIXS_VSATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0),1 << qc->tag);
	
	VIXS_VSATA_reg_writel(1 << qc->tag, port_mmio + PORT_CMD_ISSUE, 0);
	VIXS_VSATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);	/* flush */

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
	VIXS_VSATA_reg_writel(0, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_thaw(struct ata_port *ap)
{
	void __iomem *mmio = ap->host->iomap[AHCI_PCI_BAR];
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	struct ahci_port_priv *pp = ap->private_data;

	/* clear IRQ */
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_IRQ_STAT, 0);
	VIXS_VSATA_reg_writel(1 << ap->port_no, mmio + HOST_IRQ_STAT, 0);

	/* turn IRQ back on */
	VIXS_VSATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_error_handler(struct ata_port *ap)
{
	if (!(ap->pflags & ATA_PFLAG_FROZEN)) {
		/* restart engine */
		vixs_ahci_stop_engine(ap);
		vixs_ahci_start_engine(ap);
	}

	sata_pmp_error_handler(ap);
}
static u32 vixs_ata_wait_register(void __iomem *reg, u32 mask, u32 val,
		      unsigned long interval_msec,
		      unsigned long timeout_msec)
{
	unsigned long timeout;
	u32 tmp;

	tmp = VIXS_VSATA_reg_readl(reg, 0);

	/* Calculate timeout _after_ the first read to make sure
	 * preceding writes reach the controller before starting to
	 * eat away the timeout.
	 */
	timeout = jiffies + (timeout_msec * HZ) / 1000;

	while ((tmp & mask) == val && time_before(jiffies, timeout)) {
		msleep(interval_msec);
		tmp = VIXS_VSATA_reg_readl(reg, 0);
	}

	return tmp;
}
static int vixs_ahci_kick_engine(struct ata_port *ap, int force_restart)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	u8 status = VIXS_VSATA_reg_readl(port_mmio + PORT_TFDATA, 0) & 0xFF;
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
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp |= PORT_CMD_CLO;
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

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
        DBK_LOC;
	cmd = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	cmd |= PORT_CMD_PMP;
	VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);

	pp->intr_mask |= PORT_IRQ_BAD_PMP;
	VIXS_VSATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_pmp_detach(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_port_priv *pp = ap->private_data;
	u32 cmd;

	cmd = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	cmd &= ~PORT_CMD_PMP;
	VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
	pp->intr_mask &= ~PORT_IRQ_BAD_PMP;
	VIXS_VSATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
}

static void vixs_ahci_power_up(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd;

	cmd = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0) & ~PORT_CMD_ICC_MASK;

	/* spin up device */
	if (hpriv->cap & HOST_CAP_SSS) {
		cmd |= PORT_CMD_SPIN_UP;
		VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
	}

	/* wake up link */
	VIXS_VSATA_reg_writel(cmd | PORT_CMD_ICC_ACTIVE, port_mmio + PORT_CMD, 0);
}
static void vixs_ahci_start_port(struct ata_port *ap)
{
	/* enable FIS reception */
	vixs_ahci_start_fis_rx(ap);

	/* enable DMA */
	vixs_ahci_start_engine(ap);
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

static void vixs_ahci_power_down(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 cmd, scontrol;

	if (!(hpriv->cap & HOST_CAP_SSS))
		return;

	/* put device into listen mode, first set PxSCTL.DET to 0 */
	scontrol = VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_CTL, 0);
	scontrol &= ~0xf;
	VIXS_VSATA_reg_writel(scontrol, port_mmio + PORT_SCR_CTL, 0);

	/* then set PxCMD.SUD to 0 */
	cmd = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0) & ~PORT_CMD_ICC_MASK;
	cmd &= ~PORT_CMD_SPIN_UP;
	VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
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
	u32 i=0;
        DBK_LOC;
	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
#if 0
	mem = dmam_alloc_coherent(dev, AHCI_PORT_PRIV_DMA_SZ, &mem_dma,
				  GFP_KERNEL);
	if (!mem)
		return -ENOMEM;
	memset(mem, 0, AHCI_PORT_PRIV_DMA_SZ);
#else
 //hack it here since this piece of memory should be in XC4 FB
 //fix memory for host1
if(vixs_ata_host[1]==ap->host){
    mem = (g_XC4_AHCI_info.mp_pmmfb + XC4_AHCI_MEM_OFFSET_1);
    mem_dma = g_XC4_AHCI_info.m_ahci_mem_phy = XC4_AHCI_MEM_OFFSET_1;

}
else {
    mem = (void*)g_XC4_AHCI_info.m_ahci_mem_virt;
    mem_dma = g_XC4_AHCI_info.m_ahci_mem_phy;
}


    //clear to 0 to initialize
    for(i= 0; i< AHCI_PORT_PRIV_DMA_SZ/4; i++)
    {
        writel(0, mem + i*4);
    }


#endif
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
	pp->intr_mask = DEF_PORT_IRQ;//|PORT_IRQ_BAD_PMP;
	//pp->intr_mask = DEF_PORT_IRQ|PORT_IRQ_OVERFLOW;

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
	VIXS_VSATA_reg_writel(1, port_mmio + PORT_CMD_ISSUE, 0);

	if (timeout_msec) {
		tmp = vixs_ata_wait_register(port_mmio + PORT_CMD_ISSUE, 0x1, 0x1,
					1, timeout_msec);
		if (tmp & 0x1) {
			vixs_ahci_kick_engine(ap, 1);
			return -EBUSY;
		}
	} else
		VIXS_VSATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);	/* flush */

	return 0;
}

static unsigned int vixs_ahci_dev_classify(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_taskfile tf;
	u32 tmp;

	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_SIG, 0);
	tf.lbah		= (tmp >> 24)	& 0xff;
	tf.lbam		= (tmp >> 16)	& 0xff;
	tf.lbal		= (tmp >> 8)	& 0xff;
	tf.nsect	= (tmp)		& 0xff;
        DBK("tmp:%x\n",tmp);
	return ata_dev_classify(&tf);
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

//retry:
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

static int vixs_ahci_softreset(struct ata_link *link, unsigned int *class,
			  unsigned long deadline)
{
	int pmp = sata_srst_pmp(link);

	DPRINTK("ENTER\n");

	return vixs_ahci_do_softreset(link, class, pmp, deadline, vixs_ahci_check_ready);
}
static int vixs_ahci_check_ready(struct ata_link *link)
{
	void __iomem *port_mmio = ahci_port_base(link->ap);
	u8 status = VIXS_VSATA_reg_readl(port_mmio + PORT_TFDATA, 0) & 0xFF;

	return ata_check_ready(status);
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

	DPRINTK("ENTER\n");

	vixs_ahci_stop_engine(ap);

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = 0x80;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);
    
    DPRINTK("sata_link_hardreset\n");

	rc = sata_link_hardreset(link, timing, deadline, &online,
				 vixs_ahci_check_ready);
    
    DPRINTK("vixs_ahci_start_engine online:%d\n",online);

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
	new_tmp = tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	if (*class == ATA_DEV_ATAPI)
		new_tmp |= PORT_CMD_ATAPI;
	else
		new_tmp &= ~PORT_CMD_ATAPI;
	if (new_tmp != tmp) {
		VIXS_VSATA_reg_writel(new_tmp, port_mmio + PORT_CMD, 0);
		VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0); /* flush */
	}
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
        ata_dev_printk(dev, KERN_INFO, "xc5 vixs_ahci_dev_config, set max_sectors to 1024\n");
    }
}

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
		VIXS_VSATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);

		sata_link_scr_lpm(link, policy, false);
	}

	if (hpriv->cap & HOST_CAP_ALPM) {
		u32 cmd = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);

		if (policy == ATA_LPM_MAX_POWER || !(hints & ATA_LPM_HIPM)) {
			cmd &= ~(PORT_CMD_ASP | PORT_CMD_ALPE);
			cmd |= PORT_CMD_ICC_ACTIVE;

			VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
			VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);

			/* wait 10ms to be sure we've come out of LPM state */
			ata_msleep(ap, 10);
		} else {
			cmd |= PORT_CMD_ALPE;
			if (policy == ATA_LPM_MIN_POWER)
				cmd |= PORT_CMD_ASP;

			/* write out new cmd value */
			VIXS_VSATA_reg_writel(cmd, port_mmio + PORT_CMD, 0);
		}
	}

	if (policy == ATA_LPM_MAX_POWER) {
		sata_link_scr_lpm(link, policy, false);

		/* turn PHYRDY IRQ back on */
		pp->intr_mask |= PORT_IRQ_PHYRDY;
		VIXS_VSATA_reg_writel(pp->intr_mask, port_mmio + PORT_IRQ_MASK, 0);
	}

	return 0;
}
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
	.scr_read		= vsata_ahci_scr_read,
	.scr_write		= vsata_ahci_scr_write,
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
		.flags		= AHCI_FLAG_COMMON|ATA_FLAG_VIXS_PCI,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &vixs_ahci_ops,
	},
	/* host #2 (if available) */
	{
//		AHCI_HFLAGS	(AHCI_HFLAG_NO_PMP),
		.flags		= AHCI_FLAG_COMMON|ATA_FLAG_VIXS_PCI,
		.pio_mask	= 0x1f, /* pio0-4 */
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &vixs_ahci_ops,
	}
};


u32 XC4_AHCI_data_buf_allocate(u32 len)
{
    u32 ret;
    unsigned long  flags;
   if(len <= XC4_AHCI_DATA_BUF_SEG_SIZE) {  
		spin_lock_irqsave(&vsata_indirect_reg_lock, flags);
    if(g_XC4_AHCI_info.ms_data_buf_pool.next == NULL)
    {
        printk("Failed to allocate data buffer g_XC4_AHCI_info.m_data_buf_free_num:%d\n",g_XC4_AHCI_info.m_data_buf_free_num);
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
	return 0;
    }

    ret = g_XC4_AHCI_info.ms_data_buf_pool.next->data_buf_paddr;

    g_XC4_AHCI_info.ms_data_buf_pool.next = g_XC4_AHCI_info.ms_data_buf_pool.next->next;
    g_XC4_AHCI_info.m_data_buf_free_num--;
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
   }else {
    
		spin_lock_irqsave(&vsata_indirect_reg_lock, flags);
    if(g_XC4_AHCI_info.ms_data2_buf_pool.next == NULL)
    {
        printk("Failed to allocate data buffer g_XC4_AHCI_info.m_data2_buf_free_num:%d\n",g_XC4_AHCI_info.m_data2_buf_free_num);
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
	BUG();
        while(1);
    }

    ret = g_XC4_AHCI_info.ms_data2_buf_pool.next->data_buf_paddr;

    g_XC4_AHCI_info.ms_data2_buf_pool.next = g_XC4_AHCI_info.ms_data2_buf_pool.next->next;
    g_XC4_AHCI_info.m_data2_buf_free_num--;
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);


   }
   //DBK("ret:%x\n",ret); 
    return ret;
}


void XC4_AHCI_data_buf_free(u32 paddr,u32 len)
{
    u32 index;
   unsigned long  flags;
   //printk("%s:%d len:%x\n", __func__,__LINE__,len);
  if(len <= XC4_AHCI_DATA_BUF_SEG_SIZE) {  
		spin_lock_irqsave(&vsata_indirect_reg_lock, flags);
    index = (paddr - g_XC4_AHCI_info.ms_data_buf[0].data_buf_paddr)/XC4_AHCI_DATA_BUF_SEG_SIZE;

    g_XC4_AHCI_info.ms_data_buf[index].next = g_XC4_AHCI_info.ms_data_buf_pool.next;
    g_XC4_AHCI_info.ms_data_buf_pool.next = &g_XC4_AHCI_info.ms_data_buf[index];
    g_XC4_AHCI_info.m_data_buf_free_num++;
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
  }else{
		spin_lock_irqsave(&vsata_indirect_reg_lock, flags);
    index = (paddr - g_XC4_AHCI_info.ms_data2_buf[0].data_buf_paddr)/XC4_AHCI_DATA2_BUF_SEG_SIZE;

    g_XC4_AHCI_info.ms_data2_buf[index].next = g_XC4_AHCI_info.ms_data2_buf_pool.next;
    g_XC4_AHCI_info.ms_data2_buf_pool.next = &g_XC4_AHCI_info.ms_data2_buf[index];
    g_XC4_AHCI_info.m_data2_buf_free_num++;
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
  }
}

unsigned int xc_vsata_host_to_fb(u32 src, u32 dest, u32 len){
	unsigned char* ptr=(unsigned char*)phys_to_virt(src);
	dump_packet(ptr,len);
        ptr= (unsigned char* )g_XC4_AHCI_info.mp_pmmfb+dest;
	dump_packet(ptr,len);
	DBK("src:%x g_XC4_AHCI_info.mp_pmmfb+dest:%p len:%x\n",src, g_XC4_AHCI_info.mp_pmmfb+dest,len);
	flush_dcache_range((u32)phys_to_virt(src),(u32)phys_to_virt(src)+len);
	memcpy((void*)(g_XC4_AHCI_info.mp_pmmfb+dest),(void*)phys_to_virt(src),len);
	ptr= (unsigned char*)(g_XC4_AHCI_info.mp_pmmfb+dest);
	dump_packet(ptr,len);
	return len;
}

unsigned int xc_vsata_fb_to_host(u32 src, u32 dest, u32 len){
	unsigned char* ptr=(unsigned char*)((u32)g_XC4_AHCI_info.mp_pmmfb + src);

	DBK("src:%x dest:%p len:%x\n",src,phys_to_virt(dest),len);
	//dump_packet(ptr,len);
        ptr= phys_to_virt(dest);
	//dump_packet(ptr,len);

//	inv_dcache_range(phys_to_virt(dest),phys_to_virt(dest)+len);
	memcpy((void*)phys_to_virt(dest),(void*)g_XC4_AHCI_info.mp_pmmfb + src,len);
	flush_dcache_range((u32)phys_to_virt(dest),(u32)phys_to_virt(dest)+len);
	ptr= phys_to_virt(dest);
	dump_packet(ptr,len);

	return len;
}


#ifdef CONFIG_XC5_VSATA_SGDMA
/*
 * The DMA functions
 *	LOCKING:
 *	g_xcode5_dma0_lock
 */
static inline  void Vixs_Build_64Bit_Info_To_IO(unsigned int * src, unsigned int * dest, unsigned int size)
{
	//printk("%s:%d src:%p dest:%p\n",__func__,__LINE__,src,dest);
#if defined(BIG_ENDIAN)
    if (size == sizeof(unsigned int))
    {
       // writel(0, (unsigned int *)dest);
       // writel(*src, (unsigned int *)dest + 1);
       writel(*src, (unsigned int *)dest);
       writel(0, (unsigned int *)dest+1);
    }
    else
    {
        writel(*src, dest + 1);
        writel(*(src+1), (dest));
    }
#else

    xc4_ahci_mem_writel(*src, dest);
    xc4_ahci_mem_writel(0,(dest+1));
#endif
}

static u32 XC4IndirectDmaHostToFB (struct scatterlist * hostSG, struct scatterlist *xc4SG, unsigned int nElem)
{
	u32 count = 0, i = 0;
	volatile u32 status, command = 0;

    PVIXSDMADESCRIPTORS currentDescriptor = NULL;

	VPRINTK("Enter XC4IndirectDmaFBToHost\n");
    while(count < nElem) {
        currentDescriptor = g_XC4_AHCI_info.mps_descriptor + count;
       
        command = sg_dma_len(hostSG);
		/* most likely the size will not exceed 64KB*/
		if(unlikely(command & ~0xFFFF)) {
			printk("[%s] ERROR: Invalid SG[%d] DMA length %d!\n", __func__, count, command);
			BUG();		
		}
		
        command |= (VIXS_DMA_NOT_EOL|VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_SYSTEM);

        Vixs_Build_64Bit_Info_To_IO(&sg_dma_address(xc4SG), (unsigned int *)&currentDescriptor->dst_addr, sizeof(unsigned int));

        Vixs_Build_64Bit_Info_To_IO(&sg_dma_address(hostSG), (unsigned int *)&currentDescriptor->src_addr, sizeof(unsigned int));

//	printk("src:%p dest:%p len:%d\n",sg_dma_address(hostSG),sg_dma_address(xc4SG),  sg_dma_len(hostSG));
        writel(command, &currentDescriptor->command);
        writel(0, &currentDescriptor->control_word);
		
		
        count++;
        xc4SG++;
        hostSG++;
    }

    //point to the last descriptor
    currentDescriptor = g_XC4_AHCI_info.mps_descriptor + (count -1);
    command = readl(&currentDescriptor->command);
#if 1 /* DMA polling mode */
    command |= (VIXS_DMA_EOL|VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_SYSTEM);
#else	
	/* optimization use DMA interrupt */
    //command |= (VIXS_DMA_EOL|VIXS_DMA_INTHOST|VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_SYSTEM);
#endif
    writel(command, &currentDescriptor->command);

    // read back to ensure descriptor flush into memory before starting dma
    command = readl(&currentDescriptor->command);    
	/* kick off the DMA */
	VMMR_WRITE(VIXS_DMA_DQ_PTR0, g_XC4_AHCI_info.m_descriptor_offset);
#ifdef DMA_PROFILE
	t1 = jiffies;
#endif
	do {
		udelay(DMA_POLL_INTERVAL);
		
		status = VMMR_READ(VIXS_DMA_STATUS);
		if (status & XC_DMA_STATUS_ERR_INDIRECT0_MASK) {
			printk("[%s] ERROR: DMA slot 0 error, status: 0x%x",__func__, status);
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_INDIRECT0_MASK);
			return 0;
		}			

		/* MAX DMA tx time is 60ms x nElem */
		if(unlikely(++i > (nElem * MAX_DMA_TIMEOUT + DMA_WAIT_TIME))) {
			printk("[%s]: DMA slot 0 timeout, status: 0x%x current cmd 0x%x\n", 
					__func__, status, VMMR_READ(XC_DMA_ACTIVE_CMD));
			return 0;
		}
	} while (status & VIXS_DMA_INDIRECT0_MASK);
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms = jiffies_to_msecs(t2 - t1);
	if (ms)
		printk("[%s] INFO: SG DMA complete in %d ms\n", __func__, ms);
#endif	
    return nElem;
}

static u32 XC4IndirectDmaFBToHost (struct scatterlist *xc4SG, struct scatterlist * hostSG, unsigned int nElem)
{
	u32 count = 0, i = 0;
	volatile u32 status, command = 0;
    PVIXSDMADESCRIPTORS currentDescriptor = NULL;

    VPRINTK("Enter XC4IndirectDmaFBToHost\n");
	
    while(count < nElem) {
        currentDescriptor = g_XC4_AHCI_info.mps_descriptor + count;
		command = sg_dma_len(hostSG);
		/* most likely the size will not exceed 64KB*/
		if(unlikely(command & ~0xFFFF)) {
			printk("[%s] ERROR: Invalid SG[%d] DMA length %d!\n", __func__, count, command);
			BUG();
		}
		
        command |= (VIXS_DMA_NOT_EOL|VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME);

#if defined(BIG_ENDIAN)
        command |= VIXS_DMA_SWAPCTRL_32_BIT;
#endif
        VPRINTK("XC4IndirectDmaFBToHost, host addr=0x%08x, fb addr=0x%08x, len=%d\n", sg_dma_address(hostSG), sg_dma_address(xc4SG), sg_dma_len(hostSG));

        Vixs_Build_64Bit_Info_To_IO(&sg_dma_address(hostSG), (unsigned int *)&currentDescriptor->dst_addr, sizeof(unsigned int));
        Vixs_Build_64Bit_Info_To_IO(&sg_dma_address(xc4SG), (unsigned int *)&currentDescriptor->src_addr, sizeof(unsigned int));
		
        writel(command, &currentDescriptor->command);
        writel(0, &currentDescriptor->control_word);
        count++;	
        xc4SG++;
        hostSG++;        
    }

    //point to the last descriptor
    currentDescriptor = g_XC4_AHCI_info.mps_descriptor + (count -1);
    command = readl(&currentDescriptor->command);    
#if 1 /* DMA Polling mode */
    command |= (VIXS_DMA_EOL|VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME);
#else
    command |= (VIXS_DMA_EOL|VIXS_DMA_INTHOST|VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME);
#endif

#if defined(BIG_ENDIAN)
    command |= VIXS_DMA_SWAPCTRL_32_BIT;
#endif
    writel(command, &currentDescriptor->command);

    // read back to ensure descriptor flush into memory before starting dma
    command = readl(&currentDescriptor->command);
	VMMR_WRITE(VIXS_DMA_DQ_PTR0, g_XC4_AHCI_info.m_descriptor_offset);
#ifdef DMA_PROFILE
	t1 = jiffies;
#endif
	do {
		udelay(DMA_POLL_INTERVAL);
		status = VMMR_READ(VIXS_DMA_STATUS);
		if (status & XC_DMA_STATUS_ERR_INDIRECT0_MASK) {
			printk("[%s] ERROR: DMA slot 0 error, status: 0x%x",__func__, status);
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_INDIRECT0_MASK);
			return 0;
		}			

		/* MAX DMA tx time is 60ms x nElem */
		if(unlikely(++i > (nElem * MAX_DMA_TIMEOUT + DMA_WAIT_TIME))) {
			printk("%s: DMA slot 0 timeout, status: 0x%x current cmd 0x%x\n", 
					__func__, status, VMMR_READ(XC_DMA_ACTIVE_CMD));
			return 0;
		}
	} while (status & VIXS_DMA_INDIRECT0_MASK);
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms = jiffies_to_msecs(t2-t1);
	if (ms)
		printk("[%s] INFO: SG DMA complete in %d ms\n", __func__, ms);
#endif
    return nElem;
}

/*
 *
 * The DMA functions
 * LOCKING:
 * spin_lock_irqsave(host lock) *
 * Return 0 failed, nElem succeed 
 */
static u32 VixsIndirectDMA(unsigned int cmd, struct scatterlist *srcSG, struct scatterlist *destSG, unsigned int nElem)
{
    unsigned long flags;
	u32 i, ret = 0;
	volatile u32 status, mask, temp;

	status = VMMR_READ(VIXS_DMA_STATUS);
	if (unlikely(status & VIXS_DMA_ERROR_MASK)) {
		printk("[%s] DMA engine error, status 0x%x\n", __func__, status);
		return 0;
    }

	/* 
	 * poll for other core release the DMA slot 0 
	 * lock first
	 */	
#ifdef DMA_PROFILE	
	t1 = jiffies;
#endif
	/* Holding the lock to do DMA */
	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
	i=0;
	while (1) {		
		temp = VMMR_READ(VIXS_DMA_STATUS);
		temp &= XC_DMA_STATUS_INDIRECT0_MASK;		
		if(temp == 0)
			break;

		/* polling for wait timeout, unlock first, allow schedule */ 
		
		if (unlikely(++i > DMA_WAIT_TIME)) {
			spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);
			printk("[%s] ERROR: DMA slot 0 not idle, status 0x%x!\n", 
				__func__, VMMR_READ(VIXS_DMA_STATUS));			
			return 0;
		}
		udelay(DMA_POLL_INTERVAL);		
	}
#ifdef DMA_PROFILE	
	t2 = jiffies;
	ms = jiffies_to_msecs(t2 - t1);
	if(ms)
		printk("[%s] INFO: DMA idle wait time %d.\n", __func__, ms);
#endif

	/* keep the DMA status */
	status = VMMR_READ(XC_DMA_HOST_INT_STATUS) & XC_DMA_HOST_INT_STATUS_XFER_DONE0_MASK;

	/* switch to polling mode */
	mask = VMMR_READ(XC_HOST_INTERRUPT1_MASK);
	mask &= ~XC_HOST_INTERRUPT1_MASK_DMA_A0_INT_MASK;
	VMMR_WRITE(XC_HOST_INTERRUPT1_MASK, mask);

    switch (cmd) 
    {
    case CMD_DMA_HOST_TO_FB:
        ret = XC4IndirectDmaHostToFB(srcSG, destSG, nElem);
        break;
    case CMD_DMA_FB_TO_HOST:
        ret = XC4IndirectDmaFBToHost(srcSG, destSG, nElem);
        break;
    default:
        printk("<1>""Unknown ioctl cmd (%x).\n", cmd);
        break;
    }

	if (!status)
		VMMR_WRITE(XC_DMA_HOST_INT_STATUS, XC_DMA_HOST_INT_STATUS_XFER_DONE0_MASK);
	mask |= XC_HOST_INTERRUPT1_MASK_DMA_A0_INT_MASK;
	VMMR_WRITE(XC_HOST_INTERRUPT1_MASK, mask);
	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);	
	return ret;
}
#else
static u32 XC4DmaHostToFB (u32 addrSrcHost, u32 addrDestXC4, u32 len)
{
	u32 i, command = 0, mapp_addr = 0;
	volatile u32 temp;	

	#if 0
    if(len > XC4_AHCI_DATA2_BUF_SEG_SIZE)
    {
        printk("ERROR, size is too big %x\n",len);
	BUG();
        return 0;
    }

      flush_dcache_range((u32)phys_to_virt(addrSrcHost),(u32)phys_to_virt(addrSrcHost)+len);
#endif
    DBK("src:%x dest:%x len:%x\n", addrSrcHost,addrDestXC4,len);

	if ((len>0x10000)|| (len==0))
	{
		printk("%s: Invalid DMA request size: %x\n",__func__, len);
		return 0;
	}

	temp = VMMR_READ(VIXS_DMA_STATUS);
	if (temp & (XC_DMA_STATUS_DIRECT_B_MASK | XC_DMA_STATUS_ERR_DIRECT_B_MASK)) {
		printk("%s: DMA slot B error or not idle, status: 0x%x\n",__func__, temp);
		return 0;
	}

    mapp_addr=pci_map_single( g_XC4_AHCI_info.mps_ppcidev,
			phys_to_virt(addrSrcHost), len, PCI_DMA_TODEVICE);

	spin_lock(&g_xcode5_dmab_lock);
    command = len;
    command |= VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_SYSTEM;

#if 0//defined(BIG_ENDIAN)
    command |= VIXS_DMA_SWAPCTRL_32_BIT;
#endif
	VMMR_WRITE(VIXS_DMA_DIRECT_LDST_B, addrDestXC4);
	VMMR_WRITE(VIXS_DMA_DIRECT_LSRC_B, addrSrcHost);
	VMMR_WRITE(VIXS_DMA_DIRECT_UDST_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_USRC_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_DMACMD_B, command);

	i=0;		
	do {
		/* DMA max speed 200MBps, around 5 us per 1KB block */
		udelay(5);
		temp = VMMR_READ(VIXS_DMA_STATUS);		
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK) {
			printk("%s: DMA slot B error, status: 0x%x",__func__, temp);
			len = 0;
			break;
		}			

		/* MAX DMA tx time is 350us, use 3500 us for timeout limit */
		if(unlikely(++i>MAX_DMA_TIMEOUT)) {
			printk("%s: DMA slot B timeout, cmd: 0x%x src: 0x%x dst: 0x%x status: 0x%x\n", 
					__func__, command, addrSrcHost, addrDestXC4, temp);
			len = 0;
			break;
		}
	} while(temp & VIXS_DMA_DIRECT_B_MASK);

	spin_unlock(&g_xcode5_dmab_lock);
    pci_unmap_single(g_XC4_AHCI_info.mps_ppcidev,mapp_addr,len,PCI_DMA_TODEVICE);

    //flush 
    temp = *((u32*)(g_XC4_AHCI_info.mp_pmmfb + 0));
    return len;
}

static u32 XC4DmaFBToHost (u32 addrSrcXC4, u32 addrDestHost, u32 len)
{
	u32 i, command = 0, mapp_addr = 0;	
	volatile u32 temp;	

   #if 0
     if(len > XC4_AHCI_DATA2_BUF_SEG_SIZE)
    {
        printk("ERROR, size is too big %x\n",len);
		BUG();
        return 0;
    }
     DBK("src:%x dest:%x len:%x\n",addrSrcXC4,addrDestHost,len);
	 #endif
	if ((len>0x10000)|| (len==0))
	{
		printk("%s: Invalid DMA request size: %x\n",__func__, len);
		return 0;
	}

	temp = VMMR_READ(VIXS_DMA_STATUS);
	if (temp & (XC_DMA_STATUS_DIRECT_B_MASK | XC_DMA_STATUS_ERR_DIRECT_B_MASK)) {
		printk("%s: DMA slot B error or not idle, status: 0x%x\n",__func__, temp);
		return 0;
	}

   mapp_addr=pci_map_single( g_XC4_AHCI_info.mps_ppcidev,
                                phys_to_virt(addrDestHost),
                                len, PCI_DMA_FROMDEVICE);
	spin_lock(&g_xcode5_dmab_lock);
    
    command = len;
    command |= VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME;
#if 0//defined(BIG_ENDIAN)
    command |= VIXS_DMA_SWAPCTRL_32_BIT;
#endif

	VMMR_WRITE(VIXS_DMA_DIRECT_LDST_B, addrDestHost);
	VMMR_WRITE(VIXS_DMA_DIRECT_LSRC_B, addrSrcXC4);	
	VMMR_WRITE(VIXS_DMA_DIRECT_LDST_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_LSRC_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_DMACMD_B, command);

	i=0;		
	do {
		/* DMA max speed 200MBps, around 5 us per 1KB block */
		udelay(5);
		temp = VMMR_READ(VIXS_DMA_STATUS);		
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK) {
			printk("%s: DMA slot B error, status: 0x%x",__func__, temp);
			len = 0;
			break;
		}			

		/* MAX DMA tx time is 350us, use 3500 us for timeout limit */
		if(unlikely(++i>MAX_DMA_TIMEOUT)) {
			printk("%s: DMA slot B timeout, cmd: 0x%x src: 0x%x dst: 0x%x status: 0x%x\n", 
					__func__, command, addrSrcXC4, addrDestHost, temp);
			len = 0;
			break;
		}
	} while(temp & VIXS_DMA_DIRECT_B_MASK);

	spin_unlock(&g_xcode5_dmab_lock);
     pci_unmap_single(g_XC4_AHCI_info.mps_ppcidev,mapp_addr,len,PCI_DMA_FROMDEVICE);
    //flush
     temp = *((u32*)(g_XC4_AHCI_info.mp_pmmfb + 0));
    return len;
}

#endif

u32 VIXS_VSATA_writel(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32 value)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
    //DBK("base:%x addr:%x value:%x\n",SATAC_reg_base,sata_reg_addr,value);
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};


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
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};

	if(unlikely(regVal&0x2))
		BUG();
	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);	
    return 0;
};

    
u32 VIXS_VSATA_readl(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
    //DBK("base:%x addr:%x\n",SATAC_reg_base,sata_reg_addr);
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

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
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

    if(regVal&0x2) 
    {
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
        return 0;
    };

    regVal = readl(SATAC_reg_base + 0xc);
	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);

    return regVal;
};

u32 VIXS_VSATA_writeb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 value)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};

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
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};

	if(unlikely(regVal&0x2))
		BUG();
	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);	
    return 0;
}

u8 VIXS_VSATA_readb(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

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
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

    if(regVal&0x2) 
    {
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
        return 0;
     };

    regVal = (u8)readl(SATAC_reg_base + 0xc);
	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);

    return (u8)regVal;
}


u32 VIXS_VSATA_writew(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16 value)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};

   DBK("value:%x SATAC_reg_base:%x sata_reg_addr:%x\n",value,SATAC_reg_base,sata_reg_addr);
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
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return -EBUSY;
		}
	};

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
    return 0;
}


u16 VIXS_VSATA_readw(void* SATAC_reg_base, void __iomem * sata_reg_addr)
{
	volatile u32 regVal = 0;
	u32 retry;
    unsigned long flags;
	spin_lock_irqsave(&vsata_indirect_reg_lock, flags);

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

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

	retry = 0;
	while(((regVal = readl(SATAC_reg_base + 4)) & 0x1) == 1)
	{
		if(++retry >= VSATA_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);
			return 0;
		}
	};

    if(regVal&0x2) 
    {
		spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);

        return 0;
    };

    regVal = (u16)readl(SATAC_reg_base + 0xc);
	spin_unlock_irqrestore(&vsata_indirect_reg_lock, flags);

    return (u16)regVal;
}



u32 VIXS_VSATA_writesl(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32*pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_VSATA_writel(SATAC_reg_base, sata_reg_addr, (u32)*p);
        p++;
    }

    return 0;
}

u32 VIXS_VSATA_readsl(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 *pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_VSATA_readl(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}
    
u32 VIXS_VSATA_writesb(void * SATAC_reg_base, void __iomem * sata_reg_addr, u8* pbuf , u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_VSATA_writeb(SATAC_reg_base, sata_reg_addr, (u8)*p);
        p++;
    }

    return 0;
}
    
u32 VIXS_VSATA_readsb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 *pbuf, u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_VSATA_readb(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}    

u32 VIXS_VSATA_writesw(void * SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf , u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        VIXS_VSATA_writew(SATAC_reg_base, sata_reg_addr, (u16)*p);
        p++;
    }

    return 0;
}

u32 VIXS_VSATA_readsw(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf, u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = VIXS_VSATA_readw(SATAC_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}

static int vsata_reg_poll(u32 regOffset, u32 regStatus, u32 regMask, u32 loop, u32 usleep)
{
    unsigned int temp1, temp2;
    unsigned int loop_counter = 0;
    
    while (loop_counter++ < loop) 
    {
		temp1 = VMMR_READ(regOffset);
        temp2 = temp1 & regMask;
        if (temp2 == regStatus)
            break;
        msleep(usleep/1000);
    }
    if (temp2 != regStatus){
        printk("%s:%d timeout on reg:%x\n",__func__,__LINE__,regOffset);
		return -ETIMEDOUT;
    }

	return 0;
}
static int xcode_ahci_host_init(void )
{  
	volatile u32 temp;
   u32 count=0;
	unsigned long flags;
	int ret;

	temp = VMMR_READ(XC_CG_DUMMY_REG);
	printk("%s:%d board id:%x \n",__func__,__LINE__,temp);
	if ((temp & 0xFF00) == 0x3000 ) {
		vixs_ahci_clk = 100;
	}

  do { 
		spin_lock_irqsave(&xc5_register_lock, flags);
    //Enable SATA clock
		temp = VMMR_READ(XC_CG_CLK_STOP1);
    temp &= ~XC_CG_CLK_STOP1_SATACLK_STOP_MASK;
		VMMR_WRITE(XC_CG_CLK_STOP1, temp);

		temp = VMMR_READ(XC_CG_BLK_CLK_STOP2);
    temp &= ~XC_CG_BLK_CLK_STOP2_SATA_MCLK_STOP_MASK;
		VMMR_WRITE(XC_CG_BLK_CLK_STOP2, temp);
    udelay(1000);

    //Take SATA out of reset
		temp = VMMR_READ(XC_CG_RESET_REG);
    temp &= ~XC_CG_RESET_REG_SATA_RESET_MASK;
		VMMR_WRITE(XC_CG_RESET_REG, temp);
		spin_unlock_irqrestore(&xc5_register_lock, flags);

		temp = VMMR_READ( XC_SATA_CONTROL);
//    temp &= ~SATA0_AHBM_MODE_MASK;
    temp &= ~(XC_SATA_CONTROL_SATA0_LOCAL_CLK_GATE_MASK|XC_SATA_CONTROL_SATA1_LOCAL_CLK_GATE_MASK);
		//    VMMR_WRITE(XC_SATA_CONTROL, temp);
		VMMR_WRITE(XC_SATA_CONTROL, 0x11380138);
    //udelay(1000);


		VMMR_WRITE(XC_SATA_SFT_RESETS, 0);
    //udelay(1000);

    //set mpll frequency
		temp = VMMR_READ(XC_SATA_PHY_MPLL_CTRL);
    temp &= ~0x7f6;
    temp |=0x700000;

		if(vixs_ahci_clk == 125)
    {
        printk("XCode AHCI clock 125MHZ\n");
        temp |= 0x54;
		}
		else if(vixs_ahci_clk == 100)
    {
        printk("XCode AHCI clock 100MHZ\n");
        temp |= 0x464; //add 100Mhz support
    }
		else if(vixs_ahci_clk == 50)
    {
        printk("XCode AHCI clock 50MHZ\n");
        temp |= 0x460;
    }
    else
    {
			printk("NOT supported AHCI clock %d MHZ, set to default 125MHZ\n", vixs_ahci_clk);
        temp |= 0x54;
    }

		if(vixs_ahci_ssmode)
	{
		temp |= XC_SATA_PHY_MPLL_CTRL_SATA_PHY_MPLL_SS_EN_MASK;
	}
	else
	{
		temp &= ~XC_SATA_PHY_MPLL_CTRL_SATA_PHY_MPLL_SS_EN_MASK;
	}
		VMMR_WRITE(XC_SATA_PHY_MPLL_CTRL, temp);
    //udelay(1000);

    //set to external clock frequency
		temp = VMMR_READ(XC_SATA_PHY_MPLL_CTRL);
    temp &= ~0x2000;
		VMMR_WRITE(XC_SATA_PHY_MPLL_CTRL, temp);
    //udelay(1000);

    //trun off mp_ck
		temp = VMMR_READ(XC_SATA_PHY_MPLL_CTRL);
    temp |= 0x1000;
		VMMR_WRITE(XC_SATA_PHY_MPLL_CTRL, temp);
    //udelay(1000);

    //trun on mp_ck
		temp = VMMR_READ(XC_SATA_PHY_MPLL_CTRL);
    temp &= ~0x1000;
		VMMR_WRITE(XC_SATA_PHY_MPLL_CTRL, temp);
    //udelay(1000);

#if 0
    //increase tx dac gain

		   temp = VMMR_READ(SATA_PHY_ANALOG_CTRL);
    temp &= ~0x1f;
    temp |= 0x14;
		   VMMR_WRITE(SATA_PHY_ANALOG_CTRL, temp);
#endif		


		temp = VMMR_READ(XC_SATA_PHY_ANALOG_CTRL);
    temp |= 0x52000000;
    temp &= ~0x1fffff;
    temp |= 0x911;
		VMMR_WRITE(XC_SATA_PHY_ANALOG_CTRL, temp);
    //udelay(1000);

		VMMR_WRITE(XC_SATA0_PHY_TXRX_CTRL, 0x23354030);
    udelay(1000);
     DBK("g_XC4_AHCI_info.mp_pmmr:%p XC_SATA0_PHY_TXRX_CTRL:%x\n",g_XC4_AHCI_info.mp_pmmr,XC_SATA0_PHY_TXRX_CTRL);
		VMMR_WRITE(XC_SATA1_PHY_TXRX_CTRL, 0x23354030);
    //udelay(1000);

		VMMR_WRITE(XC_SATA_SFT_RESETS, 0xff);
    //udelay(1000);
    
		ret = vsata_reg_poll(XC_SATA_PHY_COMMON_STATUS, XC_SATA_PHY_COMMON_STATUS_SATA0_PHY_RX_EN_STAT_MASK, XC_SATA_PHY_COMMON_STATUS_SATA0_PHY_RX_EN_STAT_MASK, 10, 100);
		if ((ret != 0) && (++count <= 10))
			continue;

		if (ret != 0) {
			printk("%s failed to initialize the virtual SATA phy interface\n", __func__);
			BUG();
			return ret;
		}

    temp = VIXS_VSATA_reg_readl( (void __iomem *)HOST_CTL, 0);
   // printk("%s:%d HOST_CTRL:%x \n",__func__,__LINE__,temp);
#ifdef CONFIG_SATA_RXPN_SWAP_FIX
	SATAWritePHYReg(0x2107, 0x8);
#endif
	} while (!(temp & HOST_AHCI_EN));

  if(temp & HOST_AHCI_EN){
      printk("%s:%d xc5 init sata success at :%d\n", __func__,__LINE__,count+1);
  }else{
      printk("%s:%d xc5 sata fail at:%d\n", __func__,__LINE__,count+1);
  }

	return 0;
}

int XC4_AHCI_Global_Struct_Init(struct pci_dev *dev)
{
    u32 physMemAddr;
    u32 physMemSize;
	u32 count;
	int i;
	volatile u32 temp;

    
    DBK("Enter XC4_AHCI_Global_Struct_Init\n");

    g_XC4_AHCI_info.mps_ppcidev = dev;
    
    pci_enable_device(dev);

    //step through and get all the BARs
    for (count = 0; count < TotalBAR; count ++)
    {
        temp = pci_resource_flags(dev, count);
        if (temp & IORESOURCE_IO)
        {
            //ignore
        }
        else if(temp & IORESOURCE_MEM)
        {

            physMemAddr = pci_resource_start (dev, (u32) count);
            temp = pci_resource_end (dev, (u32) count);
            physMemSize = temp - physMemAddr + 1;

            if (physMemSize > 0x10000) //64K
            {
                //MMFB
                g_XC4_AHCI_info.mp_physfb = (u32*)physMemAddr;
                g_XC4_AHCI_info.m_fbsize = physMemSize;
                g_XC4_AHCI_info.mp_pmmfb = ioremap_nocache(physMemAddr, physMemSize);

                printk("BAR%x phys addr 0x%08x lin. addr 0x%08x size 0x%08x\n",
                                                    (u32)count,
                                                    (u32)g_XC4_AHCI_info.mp_physfb,
                                                    (u32)g_XC4_AHCI_info.mp_pmmfb,
                                                    (u32)g_XC4_AHCI_info.m_fbsize);
            }
            else
            {
                //MMR
                if(g_XC4_AHCI_info.mp_physmmr)
                {
                    printk("MMR is valid already, ignore this bar\n");
                }
                else
                {
                    g_XC4_AHCI_info.mp_physmmr = (u32*)physMemAddr;
                    g_XC4_AHCI_info.m_mmrsize = physMemSize;
                    g_XC4_AHCI_info.mp_pmmr = ioremap_nocache(physMemAddr, physMemSize);
    				
                    printk("BAR%x phys addr 0x%08x lin. addr 0x%08x size 0x%08x\n",
                                                        (u32)count,
                                                        (u32)g_XC4_AHCI_info.mp_physmmr,
                                                        (u32)g_XC4_AHCI_info.mp_pmmr,
                                                        (u32)g_XC4_AHCI_info.m_mmrsize);
                }
            }
        }
    }


      g_XC4_AHCI_info.m_pci_int_line = dev->irq;

	// Found PCIe device and resource allocated, check the device id for xcode5
	temp = VMMR_READ(XC_RBM_PCI_SUB_CFG);
	if (temp != XCODE5_DEVICE_ID) {
		printk("Can not find valid XCODE5 device, devid=0x%x\n", temp);
		return -1;
   }
    
#if 0 // remover reset, it may have race condition
	// put xcode5 hard blocks into reset if it is warm reboot
	if ((VMMR_READ(XC_CG_RESET_REG) & XC_CG_RESET_REG_MC_RESET_MASK) == 0) {		
		printk("Restore xcode5 registers\n");
		spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
		VMMR_WRITE(XC_CG_RESET_REG, 0xFFFFF9FF);
		VMMR_WRITE(XC_CG_RESET_REG1, 0xFFFFFFFF);
		VMMR_WRITE(XC_RBM_SWAP, 0);

		/* unlock interlock */
		for (i=0; i<8; i++) {
			temp = VMMR_READ(XC_MIPS_INTERLOCK0 + (i<<2));
			if(temp)
				VMMR_WRITE(XC_MIPS_INTERLOCK0 + (i<<2), temp);		
		}

		/*restore GPIO */
		VMMR_WRITE(XC_GPIO_DEDICATED_OUTEN, 0);	
		VMMR_WRITE(XC_GPIO_A_CTRL, XC_GPIO_A_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_A_OE, 0);
		VMMR_WRITE(XC_GPIO_B_CTRL, XC_GPIO_B_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_B_OE, 0);
		VMMR_WRITE(XC_GPIO_C_CTRL, XC_GPIO_C_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_C_OE, 0);
		VMMR_WRITE(XC_GPIO_D_CTRL, XC_GPIO_D_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_D_OE, 0);
		VMMR_WRITE(XC_GPIO_E_CTRL, XC_GPIO_E_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_E_OE, 0);
		VMMR_WRITE(XC_GPIO_F_CTRL, XC_GPIO_F_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_F_OE, 0);
		VMMR_WRITE(XC_GPIO_G_CTRL, XC_GPIO_G_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_G_OE, 0);
		VMMR_WRITE(XC_GPIO_H_CTRL, XC_GPIO_F_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_H_OE, 0);
		VMMR_WRITE(XC_GPIO_I_CTRL, XC_GPIO_I_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_I_OE, 0);
		VMMR_WRITE(XC_GPIO_J_CTRL, XC_GPIO_J_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_J_OE, 0);	
		// GPIO_J is the PCI pins	
		//VMMR_WRITE(XC_GPIO_K_CTRL, XC_GPIO_K_CTRL_DEFAULT_VALUE);
		//VMMR_WRITE(XC_GPIO_K_OE, 0);
		VMMR_WRITE(XC_GPIO_L_CTRL, XC_GPIO_L_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_L_OE, 0);
		spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);			
	}

	/* set default board id for xcode5 */
	VMMR_WRITE(XC_CG_DUMMY_REG, 0x0043);
#endif

	// wait for xcode5 memory init, infinite loop, without xcode5 intialized, vsata does not work	
	i = 0;
	while (xc5_pci_dev_initialized < 2) {
        msleep(100);
		if(i++ > 100) {
			i = 0;
			printk("%s, wait for xcode5 ready\n", __func__);
   }
	}
	/* read dummy reg */
	VMMR_READ(XC_CG_DUMMY_REG);

	spin_lock_init(&vsata_indirect_reg_lock);

    //Inlitialize memory based structures
    g_XC4_AHCI_info.m_ahci_mem_phy= XC4_AHCI_MEM_OFFSET;
    g_XC4_AHCI_info.m_ahci_mem_virt= (u32) (g_XC4_AHCI_info.mp_pmmfb + XC4_AHCI_MEM_OFFSET);
    for(count = 0; count < XC4_AHCI_DATA_BUF_SEG_NUM; count++)
    {
        g_XC4_AHCI_info.ms_data_buf[count].data_buf_paddr = /*0xC0000000 + */XC4_AHCI_DATA_BUF_OFFSET + XC4_AHCI_DATA_BUF_SEG_SIZE*count;
        g_XC4_AHCI_info.ms_data_buf[count].status = 0;
        g_XC4_AHCI_info.ms_data_buf[count].next = &g_XC4_AHCI_info.ms_data_buf[count+1];
    }
    g_XC4_AHCI_info.ms_data_buf[XC4_AHCI_DATA_BUF_SEG_NUM-1].next = NULL;
    DBK("%s:%d\n data:%x end:%x\n",__func__,__LINE__, g_XC4_AHCI_info.ms_data_buf[0].data_buf_paddr,
		     g_XC4_AHCI_info.ms_data_buf[XC4_AHCI_DATA_BUF_SEG_NUM-1].data_buf_paddr);
    g_XC4_AHCI_info.ms_data_buf_pool.next = &g_XC4_AHCI_info.ms_data_buf[0];        
    g_XC4_AHCI_info.m_data_buf_free_num = XC4_AHCI_DATA_BUF_SEG_NUM;


    for(count = 0; count < XC4_AHCI_DATA2_BUF_SEG_NUM; count++)
    {
        g_XC4_AHCI_info.ms_data2_buf[count].data_buf_paddr = /*0xC0000000 + */XC4_AHCI_DATA2_BUF_OFFSET + XC4_AHCI_DATA2_BUF_SEG_SIZE*count;
        g_XC4_AHCI_info.ms_data2_buf[count].status = 0;
        g_XC4_AHCI_info.ms_data2_buf[count].next = &g_XC4_AHCI_info.ms_data2_buf[count+1];
    }
    if(XC4_AHCI_DATA2_BUF_SEG_NUM > 0 ) {
      g_XC4_AHCI_info.ms_data2_buf[XC4_AHCI_DATA2_BUF_SEG_NUM-1].next = NULL;
      DBK("%s:%d\n data2:%x :%x\n",__func__,__LINE__, g_XC4_AHCI_info.ms_data2_buf[0].data_buf_paddr,
      g_XC4_AHCI_info.ms_data2_buf[XC4_AHCI_DATA2_BUF_SEG_NUM-1].data_buf_paddr);
      g_XC4_AHCI_info.ms_data2_buf_pool.next = &g_XC4_AHCI_info.ms_data2_buf[0];        
      g_XC4_AHCI_info.m_data2_buf_free_num = XC4_AHCI_DATA2_BUF_SEG_NUM;
    }

    g_XC4_AHCI_info.m_dma_mask = (u64)0x80000000; //2G, shoudl be enough


    g_XC4_AHCI_info.mps_sg_mempool = mempool_create_kmalloc_pool(32,
    				      sizeof(struct scatterlist)*256);
    g_XC4_AHCI_info.m_max_entry_num_per_sg = 0;
    g_XC4_AHCI_info.m_sg_num_allocated = 0;
    g_XC4_AHCI_info.m_max_sg_num_allocated = 0;


    g_XC4_AHCI_info.mps_data_buf_alloc_func = XC4_AHCI_data_buf_allocate;
    g_XC4_AHCI_info.mps_data_buf_free_func = XC4_AHCI_data_buf_free;

#if 0 
    //take DMA out of reset
	temp = VMMR_READ(VIXS_CG_RESET_REG);
    temp &= ~VIXS_CG_RESET_REG_DMA_RESET_MASK;
	VMMR_WRITE(VIXS_CG_RESET_REG, temp);
#endif

#ifdef CONFIG_XC5_VSATA_SGDMA    
    g_XC4_AHCI_info.m_indirect_dma_func = VixsIndirectDMA;
    g_XC4_AHCI_info.m_descriptor_offset = XC4_AHCI_INDIRECT_DMA_DESC_OFFSET;
    g_XC4_AHCI_info.m_desc_num = XC4_AHCI_INDIRECT_DMA_DESC_SIZE/sizeof(VIXSDMADESCRIPTORS);
    g_XC4_AHCI_info.mps_descriptor = (PVIXSDMADESCRIPTORS) (g_XC4_AHCI_info.mp_pmmfb + XC4_AHCI_INDIRECT_DMA_DESC_OFFSET);
#else
	g_XC4_AHCI_info.mps_dma_fb_to_host_func = XC4DmaFBToHost;
	g_XC4_AHCI_info.mps_dma_host_to_fb_func = XC4DmaHostToFB;
#endif
    
    return 0;
}


static void vixs_ahci_enable_ahci(void __iomem *mmio)
{
	int i;
	u32 tmp;

	/* turn on AHCI_EN */
	tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);
	if (tmp & HOST_AHCI_EN){
		DBK("HOST_AHCI_EN\n");
		return;
	}

	/* Some controllers need AHCI_EN to be written multiple times.
	 * Try a few times before giving up.
	 */
	for (i = 0; i < 5; i++) {
		tmp |= HOST_AHCI_EN;
		VIXS_VSATA_reg_writel(tmp, mmio + HOST_CTL, 0);
		tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);	/* flush && sanity check */
		if (tmp & HOST_AHCI_EN){
		    DBK("HOST_AHCI_EN\n");
		    return;
		}
		msleep(10);
	}

	WARN_ON(1);
}
//static void vixs_ahci_save_initial_config(struct platform_device *pdev,
static void vsata_ahci_save_initial_config(struct pci_dev *pdev,
				     struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = g_vixs_iomap[hpriv->index][AHCI_PCI_BAR];    
	u32 cap, port_map;
	int i;
        DBK("mmio:%p\n",mmio);
	/* make sure AHCI mode is enabled before accessing CAP */
	vixs_ahci_enable_ahci(mmio);

	/* Values prefixed with saved_ are written back to host after
	 * reset.  Values without are used for driver operation.
	 */
	hpriv->saved_cap = cap = VIXS_VSATA_reg_readl(mmio + HOST_CAP, 0);
	hpriv->saved_port_map = port_map = VIXS_VSATA_reg_readl(mmio + HOST_PORTS_IMPL, 0);

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
	VIXS_VSATA_reg_writel(hpriv->saved_cap, mmio + HOST_CAP, 0);
	VIXS_VSATA_reg_writel(hpriv->saved_port_map, mmio + HOST_PORTS_IMPL, 0);
	(void) VIXS_VSATA_reg_readl(mmio + HOST_PORTS_IMPL, 0);	/* flush */
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
	DBK("tmp:%x\n",tmp);

	/* global controller reset */
	if (!vixs_ahci_skip_host_reset) {
	DBK("tmp:%x\n",tmp);
		tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);
	DBK("tmp:%x\n",tmp);
		if ((tmp & HOST_RESET) == 0) {
			VIXS_VSATA_reg_writel(tmp | HOST_RESET, mmio + HOST_CTL, 0);
			VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0); /* flush */
		}

		/* reset must complete within 1 second, or
		 * the hardware should be considered fried.
		 */

        for (i = 1; i <= 100; i++)
        {
		    msleep(10);

		    tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);
		    if (tmp & HOST_RESET) {
                if (i == 20) {
		    	    //dev_printk(KERN_ERR, host->dev,
		    	    //	   "controller reset failed (0x%x)\n", tmp);
			    DBK_LOC;
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
static void vixs_ahci_set_oob(void __iomem *mmio, u32 clock)
{
        //set up OOB register here for CLK setting

        switch(clock)
        {
            case 25:
            case 100:
            case 50:
                VIXS_VSATA_reg_writel(0x82060b13, mmio + HOST_OOB, 0);
                VIXS_VSATA_reg_writel(0x82060b13, mmio + HOST_OOB, 0);
                break;
            case 75:
                VIXS_VSATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
                VIXS_VSATA_reg_writel(0x840a111e, mmio + HOST_OOB, 0);                
                break;
        }

}

static void vixs_ahci_start_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct ahci_port_priv *pp = ap->private_data;
	u32 tmp;

	/* set FIS registers */
	if (hpriv->cap & HOST_CAP_64)
		VIXS_VSATA_reg_writel((pp->cmd_slot_dma >> 16) >> 16,
		       port_mmio + PORT_LST_ADDR_HI, 0);
	VIXS_VSATA_reg_writel(pp->cmd_slot_dma & 0xffffffff, port_mmio + PORT_LST_ADDR, 0);

	if (hpriv->cap & HOST_CAP_64)
		VIXS_VSATA_reg_writel((pp->rx_fis_dma >> 16) >> 16,
		       port_mmio + PORT_FIS_ADDR_HI, 0);
	VIXS_VSATA_reg_writel(pp->rx_fis_dma & 0xffffffff, port_mmio + PORT_FIS_ADDR, 0);

	/* enable FIS reception */
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp |= PORT_CMD_FIS_RX;
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

	/* flush */
	VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
}

static void vixs_ahci_start_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;
	DBK("port_mmio:%p\n",port_mmio);
	/* start DMA */
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD,0);
	tmp |= PORT_CMD_START;
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);
	VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0); /* flush */
}

static int vixs_ahci_stop_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);

	/* check if the HBA is idle */
	if ((tmp & (PORT_CMD_START | PORT_CMD_LIST_ON)) == 0)
		return 0;

	/* setting HBA to idle */
	tmp &= ~PORT_CMD_START;
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);

	/* wait for engine to stop. This could be as long as 500 msec */
	tmp = vixs_ata_wait_register(port_mmio + PORT_CMD,
				PORT_CMD_LIST_ON, PORT_CMD_LIST_ON, 1, 500);
	if (tmp & PORT_CMD_LIST_ON)
		return -EIO;

	return 0;
}
static int vixs_ahci_stop_fis_rx(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	u32 tmp;

	/* disable FIS reception */
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD, 0);
	tmp &= ~PORT_CMD_FIS_RX;
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_CMD, 0);
	/* wait for completion, spec says 500ms, give it 1000 */
	tmp = vixs_ata_wait_register(port_mmio + PORT_CMD, PORT_CMD_FIS_ON,
				PORT_CMD_FIS_ON, 10, 1000);
	if (tmp & PORT_CMD_FIS_ON)
		return -EBUSY;
	return 0;
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
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_ERR, 0);
	VPRINTK("PORT_SCR_ERR 0x%x\n", tmp);
	VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_SCR_ERR, 0);

	/* clear port IRQ */
	tmp = VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VPRINTK("PORT_IRQ_STAT 0x%x\n", tmp);
	if (tmp)
		VIXS_VSATA_reg_writel(tmp, port_mmio + PORT_IRQ_STAT, 0);

	VIXS_VSATA_reg_writel(1 << port_no, mmio + HOST_IRQ_STAT, 0);

	vsata_ahci_scr_read(&ap->link, SCR_STATUS, &tmp);
	DBK("SCR_STATUS:%x\n",tmp);
}
static void vsata_ahci_init_controller(struct ata_host *host)
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

	tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);
	DBK("HOST_CTL 0x%x\n", tmp);
	VIXS_VSATA_reg_writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL, 0);
	tmp = VIXS_VSATA_reg_readl(mmio + HOST_CTL, 0);
	DBK("HOST_CTL 0x%x\n", tmp);
}

static void vixs_ahci_print_info(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;
	//struct platform_device *pdev = to_platform_device(host->dev);        
	void __iomem *mmio = host->iomap[AHCI_PCI_BAR];
	u32 vers, cap, impl, speed;
	const char *speed_s;
	const char *scc_s;

	vers = VIXS_VSATA_reg_readl(mmio + HOST_VERSION, 0);
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

	DBK("AHCI %02x%02x.%02x%02x "
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

	DBK(	"flags: "
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
int vsata_ata_host_activate(struct ata_host *host, int irq,
		      irq_handler_t irq_handler, unsigned long irq_flags,
		      struct scsi_host_template *sht)
{
	XC4_AHCI_priv_struct * pXC4AhciInfo = &g_XC4_AHCI_info;//(XC4_AHCI_priv_struct *)dev->platform_data;
	volatile u32 temp;
	int i, rc;
	int irq_requested=0;
if(vixs_ata_host[1]==host){
	DBK("second host:%p\n",host);
pXC4AhciInfo->m_next_host_index =1;
}
else {
       DBK("1st host:%p\n",host);
       pXC4AhciInfo->m_next_host_index = 0;
}
DBK("host:%p\n",host);
	rc = ata_host_start(host);
DBK("rc:%d\n",rc);
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
		DBK("Request irq %d flags:%x\n", irq,irq_flags );
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
//       writel(readl((void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS)), (void*)(XC_SOC_PROC_MMREG_BASE + SATA_INT_STATUS));
  //     writel(readl((void*)(XC_SOC_PROC_MMREG_BASE + SATA_HOST_INT_MASK))|(8<<(g_next_host_index*16)), (void*)(XC_SOC_PROC_MMREG_BASE + SATA_HOST_INT_MASK));
       DBK("(pXC4AhciInfo->m_next_host_index:%d\n",(pXC4AhciInfo->m_next_host_index));

	temp = VMMR_READ(XC_SATA_INT_STATUS);
	VMMR_WRITE(XC_SATA_INT_STATUS, temp);

	temp = VMMR_READ(XC_SATA_HOST_INT_MASK);
	temp |= (0x8 << (pXC4AhciInfo->m_next_host_index*16));
	VMMR_WRITE(XC_SATA_HOST_INT_MASK, temp);

	temp = VMMR_READ(XC_HOST_INTERRUPT_MASK); 
	temp |= XC_HOST_INTERRUPT_MASK_SATA_INT_MASK;
	VMMR_WRITE(XC_HOST_INTERRUPT_MASK, temp);  
   
	for (i = 0; i < host->n_ports; i++)
		ata_port_desc(host->ports[i], "irq %d", irq);

	rc = ata_host_register(host, sht);
	DBK("rc:%d\n",rc);
	/* if failed, just free the IRQ and leave ports alone */
	if (rc && irq_requested==1)
//		devm_free_irq(host->dev, irq, host);
		free_irq(irq, host);
       DBK_LOC;

	return rc;
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
	vsata_ahci_scr_read(&ap->link, SCR_ERROR, &serror);
	vsata_ahci_scr_write(&ap->link, SCR_ERROR, serror);
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

	status = VIXS_VSATA_reg_readl(port_mmio + PORT_IRQ_STAT, 0);
	VIXS_VSATA_reg_writel(status, port_mmio + PORT_IRQ_STAT, 0);
	if(status & PORT_IRQ_IF_NONFATAL ){
        DBK("PORT_SCR_ERR:%x :%x\n", VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_ERR, 0));
	}
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
		vsata_ahci_scr_write(&ap->link, SCR_ERROR, ((1 << 16) | (1 << 18)));
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
	if (ap->qc_active && pp->active_link->sactive){
		DBK_LOC;
		qc_active = VIXS_VSATA_reg_readl(port_mmio + PORT_SCR_ACT, 0);
	}
	else{
		DBK_LOC;
		qc_active = VIXS_VSATA_reg_readl(port_mmio + PORT_CMD_ISSUE, 0);
	}

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
	/*if(!IIALocalReadInt(IIA_SATA_INT))
	    return IRQ_NONE;*/
#endif

        DBK_LOC;
	host = vixs_ata_host[0];
	hpriv = host->private_data;
	mmio = host->iomap[AHCI_PCI_BAR];

	irq_stat = VIXS_VSATA_reg_readl(mmio + HOST_IRQ_STAT, 0);

	if (!irq_stat)
	{
		if(!vixs_ata_host[1]){
		    //printk("%s:%d:\n",__func__,__LINE__);	
			return IRQ_NONE;
		}

		host = vixs_ata_host[1];
		hpriv = host->private_data;
		mmio = host->iomap[AHCI_PCI_BAR];
		

		irq_stat = VIXS_VSATA_reg_readl(mmio + HOST_IRQ_STAT, 0);
		if(!irq_stat){
		        DBK_LOC;
			return IRQ_NONE;
		}
	}

        DBK("irq_stat:%x\n",irq_stat);
	irq_masked = irq_stat & hpriv->port_map;

	spin_lock(&host->lock);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		if (!(irq_masked & (1 << i)))
			continue;

		ap = host->ports[i];
		if (ap) {
			vixs_ahci_port_intr(ap);
		} else {
			DBK("port %u (no irq)\n", i);
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
	VIXS_VSATA_reg_writel(irq_stat, mmio + HOST_IRQ_STAT, 0);

	VMMR_WRITE(XC_SATA_INT_STATUS, hpriv->index? XC_SATA_INT_STATUS_SATA1_MASK : XC_SATA_INT_STATUS_SATA0_MASK);
	spin_unlock(&host->lock);


	irq_stat = VIXS_VSATA_reg_readl(mmio + HOST_IRQ_STAT, 0);
	return IRQ_RETVAL(handled);
} 

static struct device_attribute *vixs_ahci_shost_attrs[] = {
	&dev_attr_link_power_management_policy,
	NULL
};
static struct scsi_host_template vixs_ahci_sht = {
	ATA_NCQ_SHT(VIXS_PCI_DRV_NAME),
	.can_queue		= AHCI_MAX_CMDS - 1,
	.sg_tablesize		= AHCI_MAX_SG,
	.dma_boundary		= AHCI_DMA_BOUNDARY,
	.shost_attrs		= vixs_ahci_shost_attrs,
};




static void vsata_wq_handler(struct work_struct *w);


typedef struct {
  struct work_struct my_work;
  struct pci_dev *pdev;
} my_work_t;


//static DECLARE_DELAYED_WORK(vsata_work, vsata_wq_handler);

static my_work_t *work;
static int __vixs_ahci_pci_init_one( struct pci_dev *pdev,const struct pci_device_id *id);
static struct pci_driver vixs_ahci_pci_driver1 = {
	.name			= VIXS_PCI_AHCI_HOST1_NAME,
	.id_table		= vixs_ahci_pci_tbl,
	.probe			= __vixs_ahci_pci_init_one,
//	.suspend		= ahci_pci_device_suspend,
//	.resume			= ahci_pci_device_resume,
//	.remove			= vixs_ahci_remove_one,
};

static struct device_dma_parameters __dma_parms ={
     .max_segment_size = 4096,	
};

static int __vixs_ahci_pci_init_one( struct pci_dev *pdev,const struct pci_device_id *id)
{
	static int printed_version;
	struct ata_port_info pi = vixs_ahci_port_info[g_next_host_index];
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ata_host *host;
	int n_ports, i, rc;
        //struct resource *res;   
	unsigned long base;
       u32 irq;
       	DBK_LOC;
	dev->dma_parms= &__dma_parms;

	//WARN_ON(ATA_MAX_QUEUE > AHCI_MAX_CMDS);


       /*********************************************************************/
       /*  Clear interrupt here first to avoid unexpected SATA interrupt during initialization */
       /*********************************************************************/

	if (!printed_version++)
		dev_printk(KERN_DEBUG, &pdev->dev, "version " VIXS_PCI_DRV_VERSION "\n");

	hpriv = devm_kzalloc(dev, sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;
	hpriv->flags |= (unsigned long)pi.private_data;

        hpriv->index = g_next_host_index;

#ifndef CONFIG_VIRTUAL_XC5_SATA

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
#else
   if(!g_next_host_index){
        if(XC4_AHCI_Global_Struct_Init(pdev)) {
			devm_kfree(dev, hpriv);
            return -ENODEV;        
        }
    }
        irq =g_XC4_AHCI_info.m_pci_int_line;

#endif

        //JLWANG: Don't do ioremap for now, since if there are more than one controller, they are share the same 
        //register region.
        //base = (unsigned long)ioremap_nocache( res->start, res->end - res->start +1);
#ifndef CONFIG_VIRTUAL_XC5_SATA
        base = res->start;  
#else
        base = (unsigned long ) (XC_SATA_AHB_REG_CTRL + g_XC4_AHCI_info.mp_pmmr);
#endif
        if (!base) 
        {
            dev_err(&pdev->dev,"ioremap ERROR" );
            return -ENOMEM;
        }

        //store base address in platform_data field
        pdev->dev.platform_data = &g_XC4_AHCI_info;//(void*)irq;
        VIXS_VSATA_REG_BASE = (void*)base;

       /*********************************************************/
       /*    Add HW initialization Sequence here                                        */
       /*********************************************************/

        if(!g_next_host_index)
        {
            DBK("Init host controller\n");
		if (xcode_ahci_host_init()) {
			devm_kfree(dev, hpriv);
			return -ENODEV;
		}	
        }

	/* save initial config */
	vsata_ahci_save_initial_config(pdev, hpriv);
    
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
        DBK("n_ports:%d\n",n_ports);
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	if (!host) {
		devm_kfree(dev, hpriv);
		return -ENOMEM;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	vixs_ata_host[g_next_host_index]=host;

	host->iomap = g_vixs_iomap[g_next_host_index];        //JLWANG: Set it to 0 now.
	host->private_data = hpriv;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

//		/* set initial link pm policy */
		//ap->pm_policy = NOT_AVAILABLE;

		/* disabled/not-implemented port */
		if (!(hpriv->port_map & (1 << i)))
			ap->ops = &ata_dummy_port_ops;
	}


	rc = vixs_ahci_reset_controller(host);
	if (rc)
		return rc;

	vixs_ahci_set_oob(host->iomap[AHCI_PCI_BAR], vixs_ahci_clk);

	vsata_ahci_init_controller(host);
	vixs_ahci_print_info(host);


	DBK("Activate ahci host\n");
	rc = vsata_ata_host_activate(host, irq, vixs_ahci_interrupt, IRQF_SHARED,
				 &vixs_ahci_sht);


	g_next_host_index++;
        g_vixs_ahci_active_cnt++;
        if( g_next_host_index == 1) {
		DBK_LOC;
		pci_register_driver(&vixs_ahci_pci_driver1);
	}
	DBK("rc:%d vixs_ahci_sht.can_queue:%d\n",rc,vixs_ahci_sht.can_queue);
	xc5_pci_dev_initialized = 3;
        return rc    ;
}

static void vsata_wq_handler(struct work_struct *w)
{	
	int ret;
  my_work_t *my_work = (my_work_t *)work;
  DBK("my_work->ppdev:%p\n",my_work->pdev);
  //msleep(1000);
  DBK_LOC;

	ret = __vixs_ahci_pci_init_one(my_work->pdev,NULL);
	if(ret)
		printk("Error!! XCode PCIe SATA interface init fail, error code %d\n", ret);

   kfree(w);
}

static int vixs_ahci_pci_init_one( struct pci_dev *pdev,const struct pci_device_id *id)
{
    int ret=-1;

	spin_lock_init(&g_xcode5_dma0_lock);
	spin_lock_init(&g_xcode5_dmab_lock);

        work = (my_work_t *)kmalloc(sizeof(my_work_t), GFP_KERNEL);
	if(work){
 	    INIT_WORK( (struct work_struct *)work, vsata_wq_handler );
            work->pdev = pdev;
	    ret = schedule_work((struct work_struct*) work);
	}

    return ret;
}
static struct pci_driver vixs_ahci_pci_driver0 = {
	.name			= VIXS_PCI_AHCI_HOST0_NAME,
	.id_table		= vixs_ahci_pci_tbl,
#if 1
	.probe			= vixs_ahci_pci_init_one,
#else
	.probe			= __vixs_ahci_pci_init_one,
#endif
//	.suspend		= ahci_pci_device_suspend,
//	.resume			= ahci_pci_device_resume//,
//	.remove			= vixs_ahci_remove_one,
};



int __init pcie_ahci_init(void){
    return pci_register_driver(&vixs_ahci_pci_driver0);
}


void dump_xc5_sata_register(struct ata_port *ap){
    void __iomem *port_mmio = ahci_port_base(ap);
    u32 val=0;
    u32 i=0;
      i =0xb4;
        val = VIXS_VSATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);
        i =0xb8;
        val = VIXS_VSATA_reg_readl( (void*)i, 0);
        printk("%s: i:%x val:%x\n",__func__,i,val);

    for(i=0;i<=0x44;i+=4) {
        val = VIXS_VSATA_reg_readl(port_mmio + i, 0);
        printk("%s: port_mmio+i:%p val:%x\n",__func__,port_mmio+i,val);
    }
     
     for(i=0xd000;i<=0xd060;i+=4) {
        printk("%x: val:%x\n",i, xcode_readl(i));
    }   
}

MODULE_AUTHOR("Ivan Chan");
MODULE_DESCRIPTION("Vixs XC5 AHCI SATA low-level driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VIXS_PCI_DRV_VERSION);

module_init(pcie_ahci_init);
//module_exit(vixs_ahci_exit);

