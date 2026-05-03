/* 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <common.h>
#include <mmc.h>
#include <asm/arch-xc6/xcodeRegDef.h>
#include <asm/arch-xc6/xcode_mmc.h>
#include <asm/arch-xc6/security.h>


//#define MMC_DEBUG
#ifdef MMC_DEBUG
#define mmc_debug(fmt, args...) printf(fmt, ##args) 
#else
#define mmc_debug(fmt, args...)
#endif

/* support 2 mmc hosts */
struct mmc mmc_dev[2];
struct mmc_host mmc_host[2] __attribute__((__aligned__(CONFIG_SYS_CACHELINE_SIZE)));

#define readl(addr) (*(volatile unsigned int *) (addr))
#define writel(b,addr) (*(volatile unsigned int *) (addr) = (b))


/*
 * XCode SMCC Block internal register access
 */
static u32 xcsdh_ubusy_wait(u32 core, int time_out_cnt)
{
	u32 csr_status = 0;
	u32 busy = 0, err = 0;
	int i = 0;
	u32 timeout = 0;

    csr_status = readl(XC_SOC_PROC_MMREG_BASE + CORE_REG(core, SMCC_CSR_STATUS_REG));
    busy = (csr_status & SMCC_CSR_STATUS_REG_CSR_BUSY_MASK) >> 
        SMCC_CSR_STATUS_REG_CSR_BUSY_SHIFT;

    err = (csr_status & CSR_ERR_MASK) >> 
        CSR_ERR_SHIFT;

    while (busy || err) {

        udelay(10);
        i++;
        {
            volatile u32 rg;

            rg = readl(XC_SOC_PROC_MMREG_BASE + CORE_REG(core, SMCC_CSR_CTRL_REG));
            mmc_debug("SMCC_CSR_CTRL_REG 0x%08x\n", rg);
        }

        mmc_debug("busy i %d\n", i);
        if (i > time_out_cnt) {
            timeout = 1;
            break;
        }

        csr_status = readl(XC_SOC_PROC_MMREG_BASE + CORE_REG(core, SMCC_CSR_STATUS_REG));
        busy = (csr_status & SMCC_CSR_STATUS_REG_CSR_BUSY_MASK) >> 
            SMCC_CSR_STATUS_REG_CSR_BUSY_SHIFT;

        err = (csr_status & CSR_ERR_MASK) >> 
            CSR_ERR_SHIFT;
    }

	return(timeout);
}

static int xcsdh_access_smcc_reg(u32 core, u32 reg, volatile u32 *rdata, u32 wdata, int write)
{
	u32 beef = 0;
	int time_out_cnt = 100;
	int err = 0;
	u32 csr_ctrl_reg = 0;

	err = xcsdh_ubusy_wait(core, time_out_cnt);
	if (err)
		goto out;

	switch (core) {
		case 0:
			csr_ctrl_reg = (write << SMCC_CSR_CTRL_REG_WOP_SHIFT) | reg;
			break;

		case 1:
			csr_ctrl_reg = (write << SMCC_CSR_CTRL_REG1_WOP1_SHIFT) | reg;
			break;
	}

	if (!write) {
		/* READ data */
		writel(csr_ctrl_reg, XC_SOC_PROC_MMREG_BASE + (CORE_REG(core, SMCC_CSR_CTRL_REG)));
		err = xcsdh_ubusy_wait(core, time_out_cnt);
		if (err)
			goto out;

		/* XXX:
		 * Assume the RDAT is ready instantly
		 */
		udelay(10);
		beef = readl(XC_SOC_PROC_MMREG_BASE + (CORE_REG(core, SMCC_RDAT_REG)));
		*rdata = beef;

	} else {
		/* WRITE data */
		writel(wdata, XC_SOC_PROC_MMREG_BASE + (CORE_REG(core, SMCC_WDAT_REG)));
		writel(csr_ctrl_reg, XC_SOC_PROC_MMREG_BASE + (CORE_REG(core, SMCC_CSR_CTRL_REG)));
		err = xcsdh_ubusy_wait(core, time_out_cnt);
		if (err)
			goto out;
	}

out:
	return (err);
}

/* Check Card present */
static void xcsdh_check_card_present(struct mmc_host *host)
{
	volatile u32 cd_reg = 0;
	u32 err = 0;
	
	/* Before enable irq, lets check do we have a card inserted */
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CDETECT, &cd_reg, 0, 0);
	if (err)
		printf("[%s] access SMCC_CDETECT failed\n", __func__);
	
	mmc_debug("[%s] SMCC_CDETECT:%x\n", __func__, cd_reg);
	
	/* We only have 1 slot. 0 in register means PRESENT... */
	host->card_present = (cd_reg & 0x01) ? 0 : 1;
}

/* Enable internal DMA */
static int xcsdh_enable_dma(struct mmc_host *host)
{
	volatile u32 reg;
	int err;


	/* Software reset the DMA controller internal registers */
	reg = 1 << SMCC_BMOD_SWR_SHIFT;
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_BMOD, NULL, reg, 1);
	if (err)
		goto out;

	reg = 1 << SMCC_BMOD_DE_SHIFT;

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_BMOD, NULL, reg, 1);
	if (err)
		goto out;


	reg = (1 << SMCC_CTRL_IDMAC_SHIFT) | (1 << SMCC_CTRL_INT_EN_SHIFT);	
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CTRL, NULL, reg, 1);
	if (err)
		goto out;

	/* bit8: normal IDMAC irq on 
	 * bit0,1: TI, RI irq on
	 */
	//	reg = 0x00000103;	
	//	reg = 0x00000103 | (1 << 5) | (1 << 4) | (1<<9);
	/* only report us the errors
	 * bit9: Abormal Interrupt Summary
	 * bit5: Card Error Summary
	 * bit4: Descriptor Unavailable
	 * bit2: Fatal Bus Error
	 * */
	reg = (1 << 2) | (1 << 5) | (1 << 4) | (1<<9);
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_IDINTEN, NULL, reg, 1);
	if (err)
		goto out;

	/* fix this! size not correct */
	memset(&(host->desc[0].d0), 0xffff, (4*sizeof(u32)));

	/* nolonger need buf pointer ourself */

out:
	return(err);
}

static int xcsdh_setup_polling_mode(struct mmc_host *host)
{
	int err = 0;
	volatile u32 irq_reg = 0;
	u32 mask;

	/* Core 0 */
	if(host->core_id == 0){
		irq_reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG);
		irq_reg &= ~SMCC_INT_EN_REG_INT_EN_MASK;
		writel(irq_reg, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG);
	}
	/* Core 1 */
	if(host->core_id == 1){
		irq_reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG1);
		irq_reg &= ~SMCC_INT_EN_REG_INT_EN_MASK;
		writel(irq_reg, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG1);
	}

	/* no int mask enabled polling raw int status  */
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_INT_MASK, NULL, 0x0, 1);

	if(err)
		printf(" [%s] failed err = %d\n",__func__, err);

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_INT_MASK, &mask, 0, 0);	
	return (err);
}

/*
 *  Wait for SMCC core takehe last command
 */
static int xcsdh_wait_ciu(u32 core, int time_out_cnt)
{
	u32 reg = 0;
	int err = 0;
	u32 start_cmd_bmask = 1 << SMCC_CMD_START_CMD_SHIFT;
	int i = 0;

	while (i < time_out_cnt) {
		err = xcsdh_access_smcc_reg(core, SMCC_CMD, &reg, 0, 0);
		if (err)
			break;

		/* start_cmd bit will be 0 if taken by CIU */
		if ((reg & start_cmd_bmask) == 0)
			break;
		i++;
		udelay(10);
	}

	/* XXX: set better ERRNO! */
	if (i >= time_out_cnt) {
		return (-1);
	}

	if (err)
		return (-1);

	return(0);

}

static int is_encrypted_datacmd (struct mmc_cmd *cmd)
{
    switch (cmd->cmdidx) {
        case MMC_CMD_READ_SINGLE_BLOCK:
        case MMC_CMD_READ_MULTIPLE_BLOCK:
        case MMC_CMD_WRITE_SINGLE_BLOCK:
        case MMC_CMD_WRITE_MULTIPLE_BLOCK:
            return 1;
        default:
            return 0;
    }
}

static int mmc_prepare_data(struct mmc_host *host, struct mmc_cmd *cmd, struct mmc_data *data)
{
	int err = 0;
	int count = 0;
	int i = 0, j;	

	mmc_debug(" [%s] data->dest: %08X, data->blocks: %u, data->blocksize: %u\n",
			__func__, (u32)data->dest, data->blocks, data->blocksize);

	count = data->blocks * data->blocksize;

	if(count & 0x3){
		printf(" [%s] size requested is not 4 byte aligned\n", __func__);
		err = INVAL_PARAM;
		goto out;
	}

	/* can not handle request data size greater than 512KiB */
	if(count > (MMC_DESC_NUM * DESC_BUF_SIZE)){
		printf(" [%s] requested size %d greater than dma buffer size %d\n",
			__func__, count, MMC_DESC_NUM * DESC_BUF_SIZE);
		err = INVAL_PARAM;
		goto out;
	}
	
	/* Prepare the DMA descriptor list
	 * D0 - 31 OWN; 30 CES; 5 END of RING; 4 CHAINED; 3 First Desc; 2 Last Desc; 1 DIC; 
	 * D1 - 25:13 Buffer2 size not used in chain mode; 12:0 Buffer1 size, 0 will caused discard.
	 * D2 - 31:0 Buffer physical Address Pointer 1, 
	 * D3 -  31:0 Next Desc Physical address
	 */
	j = (count / DESC_BUF_SIZE) + (((count % DESC_BUF_SIZE) == 0) ? 0 : 1);
	for(i = 0; i < j; i++){
		host->desc[i].d0 = (SMCC_DESC0_OWN_MASK | SMCC_DESC0_CH_MASK | SMCC_DESC0_DIC_MASK);
		
		if(i == 0)			
			host->desc[i].d0 |= SMCC_DESC0_FS_MASK;
		
		if(i == (j-1)){
			host->desc[i].d0 &= ~SMCC_DESC0_CH_MASK;
			host->desc[i].d0 |= (SMCC_DESC0_ER_MASK|SMCC_DESC0_LD_MASK);
		}
		
		host->desc[i].d1 = (i < (j - 1)) ? DESC_BUF_SIZE : (count % DESC_BUF_SIZE);
		if(!(count % DESC_BUF_SIZE))
			host->desc[i].d1 = DESC_BUF_SIZE;
		host->desc[i].d2 = ((u32)data->dest + DESC_BUF_SIZE * i);
		if( i == (j - 1))
			host->desc[i].d3 = 0;
		else
			host->desc[i].d3 = ((u32)host->desc + sizeof(host->desc[0]) * (i + 1));

		mmc_debug(" [%s] IDMA desc[%d] at %p, D0= %08x, D1= %08x, D2= %08x, D3= %08x\n",
			__func__, i, &(host->desc[i].d0), host->desc[i].d0, host->desc[i].d1, host->desc[i].d2, host->desc[i].d3);
	}
	do {__asm__ __volatile__ ("dsb" : : : "memory");} while(0);

	/* does read need flush dcache? */
	flush_dcache_range((unsigned)data->src, (unsigned)(data->src + data->blocks * data->blocksize));		
	flush_dcache_range((unsigned)(&host->desc[0]), (unsigned)((u32)host->desc + j * sizeof(struct xcsdh_idmac_desc)));
#ifdef CONFIG_MMC_ENC    
    /* encrypt the data before write to MMC */
    if ((data->flags & MMC_DATA_WRITE) && is_encrypted_datacmd(cmd))
        if (crypto_aes_ecb_128((u8*)data->src, (u8*)data->src, 
                    (u32)(data->blocks * data->blocksize), CRYPT_OP_ENC, CONFIG_MMC_KEY_SLOT)) {
            printf("eMMC write encryption fail\n");
            err = -INVAL_PARAM;
            goto out;
        }    	
#endif	
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_DBADDR, NULL, (u32)host->desc, 1);

out:
	return err;
}


/* 
 * XCode6 support dual SD host
 * On XC68xx, the SD host 1 shares pins with TSI
 * for bring up disable the SD1.
 */
static int xcsdh_need_support_dual_host(void)
{
	u32 board_id;
	volatile u32 tmp;

	board_id = readl(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1);
	switch (board_id) {
	case 0x0030:
	case 0x0031:
	case 0x0032:
	case 0x0033:
		tmp = readl(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_IN) & (1 << 9);
		if(tmp)
			return 1;
		else
			return 0;
	case 0x1200:
	case 0x1201:
		return 1;
	default:
		return 0;
	}
	
}


static void xcsdh_reset_smcc_hw(void)
{
    volatile u32 reg, reg_clk_stop, reg_blk_clk_stop, reg_clk_src_en;
    volatile u32 id;
    int init_core1 = 0;
    u32	temp = 0;

	mmc_debug("[%s] begin\n", __func__);

    /* default enable all hosts
      */
    if (xcsdh_need_support_dual_host()) {
		mmc_debug("smcc1 supported\n");
        init_core1 = 1;
    } else {
        init_core1 = 0;
    }

    /* 
     * Set the 2nd SD host on 
     */
    reg = readl(XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL); 

    if (init_core1) {
        reg |= (PADU_CTRL_SD1_MASK);
    }

    writel(reg, XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);
    udelay(100);

	reg_clk_src_en = readl(XC_SOC_PROC_MMREG_BASE + CG1_CLK_SRC_EN0) | CG1_CLK_SRC_EN0_SMCC0CLK_SRC_EN_MASK;
    reg_clk_stop = readl(XC_SOC_PROC_MMREG_BASE + CG1_CLK_STOP0) & ~SMCC0CLK_STOP_MASK;
    reg_blk_clk_stop = readl(XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0) & ~SMCC0_BLK_STOP_MASK;

    reg = readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    reg |= (SMCC0_RESET_MASK);
    if (init_core1) {
        reg |= (SMCC1_RESET_MASK);
		reg_clk_src_en |= CG1_CLK_SRC_EN0_SMCC1CLK_SRC_EN_MASK;
        reg_clk_stop &= ~SMCC1CLK_STOP_MASK;
        reg_blk_clk_stop &= ~SMCC1_BLK_STOP_MASK;
        printf("XCODE MMC: Init SD core 1\n");
    }

#ifndef CONFIG_FPGA_BUILD
	temp = readl(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG) & 0xFF00;
#endif

	writel(reg_clk_src_en, XC_SOC_PROC_MMREG_BASE + CG1_CLK_SRC_EN0);
	udelay(100);
    writel(reg_clk_stop, XC_SOC_PROC_MMREG_BASE + CG1_CLK_STOP0);
    udelay(100);
    writel(reg_blk_clk_stop, XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);
    udelay(100);

    temp = readl(XC_SOC_PROC_MMREG_BASE + GPIO_C_CTRL);
    writel(temp & ~GPIO_C_CTRL_GPIO_MODE_SEL_MASK, XC_SOC_PROC_MMREG_BASE + GPIO_C_CTRL);
    udelay(100);

    writel(reg, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    udelay(100);
	reg &= ~(SMCC0_RESET_MASK);
    if (init_core1)
        reg &= ~(SMCC1_RESET_MASK);
    writel(reg, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    udelay(100);

#if 0
	/* host 0 */
	reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
	reg |= (SMCC_CTRL_REG_SOFT_RST_MASK);
	writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
	udelay(100);
	
	reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
	reg &= ~(SMCC_CTRL_REG_SOFT_RST_MASK);
	writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
	udelay(100);
	
	xcsdh_access_smcc_reg(0, SMCC_RINTSTS, NULL, 0xffffffff, 1);
	xcsdh_access_smcc_reg(0, SMCC_IDSTS, NULL, 0xffffffff, 1);
	writel(0xffff, XC_SOC_PROC_MMREG_BASE + SMCC_STATUS_REG); /* write to clear */
	writel(0, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG);
	
    if (init_core1){
		/* host 1 */		
		reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		reg |= (SOFT_RST1_MASK);
		writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		udelay(100);

		reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		reg &= ~(SOFT_RST1_MASK);
		writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		udelay(1000);
		
		reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		reg |= (SMCC_CTRL_REG1_PREFETCH_FIX_SIZE_EN1_MASK);
		writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
		udelay(100);
		
		xcsdh_access_smcc_reg(1, SMCC_RINTSTS, NULL, 0xffffffff, 1);
		xcsdh_access_smcc_reg(1, SMCC_IDSTS, NULL, 0xffffffff, 1);
		writel(0xffff, XC_SOC_PROC_MMREG_BASE + SMCC_STATUS_REG1); /* write to clear */
		writel(0, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG1);
	}
#endif

    /* Increasing (Max) SMC Clock to 50 MHz */
    reg = readl(XC_SOC_PROC_MMREG_BASE + CG1_CLK_SRC_SEL4);
    reg &= ~(CG1_CLK_SRC_SEL4_SMCC0CLK_SRC_SEL_MASK | CG1_CLK_SRC_SEL4_SMCC1CLK_SRC_SEL_MASK);
    reg |= ((0x01 << SMCC0CLK_SRC_SEL_SHIFT) | (0x01 << SMCC1CLK_SRC_SEL_SHIFT));	//Setting CLK Src PLL5/20 i.e. 1000MHz/20 = 50MHz
    writel(reg, XC_SOC_PROC_MMREG_BASE + CG1_CLK_SRC_SEL4);

    return;
}

static void xcsdh_reset_smcc_core(struct mmc_host *host)
{
	volatile u32 reg;
	int core_id = host->core_id;

	mmc_debug("[%s] begin\n", __func__);
	
	switch (core_id) {
		case 0:
			/* host 0 */
			reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
			reg |= (SMCC_CTRL_REG_SOFT_RST_MASK);
			writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
			udelay(100);
	
			reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
			reg &= ~(SMCC_CTRL_REG_SOFT_RST_MASK);
			writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG);
			udelay(100);
	
			xcsdh_access_smcc_reg(0, SMCC_RINTSTS, NULL, 0xffffffff, 1);
			xcsdh_access_smcc_reg(0, SMCC_IDSTS, NULL, 0xffffffff, 1);
			writel(0xffff, XC_SOC_PROC_MMREG_BASE + SMCC_STATUS_REG); /* write to clear */
			writel(0, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG);
	
	
			break;
		case 1:
			/* host 1 */		
			reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			reg |= (SOFT_RST1_MASK);
			writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			udelay(100);

			reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			reg &= ~(SOFT_RST1_MASK);
			writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			udelay(1000);
			
			reg = readl(XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			reg |= (SMCC_CTRL_REG1_PREFETCH_FIX_SIZE_EN1_MASK);
			writel(reg, XC_SOC_PROC_MMREG_BASE + SMCC_CTRL_REG1);
			udelay(100);
			
			xcsdh_access_smcc_reg(1, SMCC_RINTSTS, NULL, 0xffffffff, 1);
			xcsdh_access_smcc_reg(1, SMCC_IDSTS, NULL, 0xffffffff, 1);
			writel(0xffff, XC_SOC_PROC_MMREG_BASE + SMCC_STATUS_REG1); /* write to clear */
			writel(0, XC_SOC_PROC_MMREG_BASE + SMCC_INT_EN_REG1);
	
	
			break;
	}

    return;
}

static int mmc_wait_cmd_done(struct mmc *mmc,
			    struct mmc_cmd *cmd,
			    struct mmc_data *data,
			    unsigned int timeout)
{
	volatile u32 rintsts;
	volatile u32 idsts;
	volatile u32 sts;
	volatile u32 dscaddr;
	volatile u32 bufaddr;
	volatile u32 tcbcnt;
	volatile u32 tbbcnt;
	volatile u32 dummy;
	int err = 0, i, count, loop;
	struct mmc_host *host = mmc->priv;

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_RINTSTS, &rintsts, 0, 0);
	if(err)
		goto error;

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_IDSTS, &idsts, 0, 0);
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_STATUS, &sts, 0, 0);
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_DSCADDR, &dscaddr, 0, 0); 
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_BUFADDR, &bufaddr, 0, 0);
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_TCBCNT, &tcbcnt, 0, 0); 
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_TBBCNT, &tbbcnt, 0, 0);

	mmc_debug("core id %d, rintsts 0x%08x, idsts 0x%08x, status 0x%08x DSCADDR 0x%08x, BUFADDR 0x%08x, tcbcnt %d, tbbcnt %d last opcode %d\n",
				host->core_id,
				rintsts, idsts, sts, 
				dscaddr, bufaddr,
				tcbcnt, tbbcnt,
				cmd->cmdidx);
		
	/* wait for cmd done */
	while(!(rintsts & SMCC_RINTSTS_CD_MASK)){
		xcsdh_access_smcc_reg(host->core_id, SMCC_RINTSTS, &rintsts, 0, 0);			
		udelay(1000);
		timeout --;
		if(timeout == 0){
			printf(" [%s] command %d time out\n", __func__, (unsigned)cmd->cmdidx);			
			return TIMEOUT;	
		}		
	}
	mmc_debug(" [%s] core_id %d cmd: %d command done\n", __func__, host->core_id, cmd->cmdidx);

		
	/* Command Done */
	if(data){ 
		/* RW command wait for DTO */
		mmc_debug(" [%s] a data %s command\n", __func__, (data->flags & MMC_DATA_READ) ? "READ" : "WRITE");
		while(!(rintsts & SMCC_RINTSTS_DTO_MASK)){				
			if(rintsts & SMCC_RINTSTS_ALL_ERR_MASK){
				err = COMM_ERR;
				mmc_debug(" [%s]:line:%d rintsts: %x\n", __func__, __LINE__, rintsts);
				goto error;
			}
			
			if(rintsts & SMCC_RINTSTS_CD_MASK){
				rintsts = 1 << SMCC_RINTSTS_CD_SHIFT;
				xcsdh_access_smcc_reg(host->core_id,SMCC_RINTSTS, NULL, rintsts, 1);
			}
			
			/* Card Remove, reset host if card removed */
			if(rintsts & SMCC_RINTSTS_CARDDET_MASK){					
				xcsdh_check_card_present(host);
				if(!host->card_present){
					printf("card removed, reset host controller\n");
					mmc->has_init = 0;
					err = NO_CARD_ERR;
					xcsdh_reset_smcc_core(host);
					goto done;
				}
			}
			xcsdh_access_smcc_reg(host->core_id, SMCC_RINTSTS, &rintsts, 0, 0);	
		}
		
		/* wait for all desc take by HW */			
		count = data->blocks * data->blocksize/DESC_BUF_SIZE;
		if(count % DESC_BUF_SIZE)
			count ++;

		invalidate_dcache_range((unsigned)host->desc, 
		    ALIGN_CACHE_SIZE((unsigned)((u32)host->desc + count * sizeof(struct xcsdh_idmac_desc))));

		for(i = 0; i < count; i++) {
			dummy = host->desc[i].d0;
			loop = 1000;
			while(dummy & SMCC_DESC0_OWN_MASK) {
				loop--;
				invalidate_dcache_range((unsigned)host->desc, 
				    ALIGN_CACHE_SIZE((unsigned)((u32)host->desc + count * sizeof(struct xcsdh_idmac_desc))));
				dummy = host->desc[i].d0;
				if (loop == 0) {
					printf("Warning, the dma descriptor %d is still owned by host when DTO received\n", i);
					break;
				}
			}
		}
		mmc_debug(" [%s] all dma descriptor were taken by controller\n", __func__);

		xcsdh_access_smcc_reg(host->core_id,SMCC_IDSTS, NULL, 0, 1);

		if(data->flags & MMC_DATA_READ){
#ifdef CONFIG_MMC_ENC
            if (is_encrypted_datacmd(cmd))
                if (crypto_aes_ecb_128((u8 *)data->dest,(u8 *)data->dest,
                            (u32)(ALIGN_CACHE_SIZE(data->blocks * data->blocksize)), CRYPT_OP_DEC, CONFIG_MMC_KEY_SLOT)) {
                    printf("mmc read decryption fail!\n");
                    err = INVAL_PARAM;
                    goto error;
                }
#endif
            invalidate_dcache_range((unsigned)data->dest, 
                    ALIGN_CACHE_SIZE((unsigned)(data->dest + data->blocks * data->blocksize)));
		} else {
			mmc_debug(" [%s] write data done\n", __func__);
		}

	} else {
		if(rintsts & SMCC_RINTSTS_ALL_ERR_MASK){
			mmc_debug(" [%s] rintsts error: %x\n", __func__, rintsts);
			if(rintsts & SMCC_RINTSTS_RTO_MASK)
				err=TIMEOUT;
			else
				err=COMM_ERR;
			
			mmc_debug(" [%s]:line:%d rintsts: %x\n", __func__, __LINE__, rintsts);
			goto error;
		}
		
		if(rintsts & SMCC_RINTSTS_DTO_MASK){
			mmc_debug(" [%s] impossible DTO should not be here \n", __func__);
		}

		/* Card Remove, reset host if card removed */
		if(rintsts & SMCC_RINTSTS_CARDDET_MASK){
			mmc_debug(" [%s] Detected card present status change\n", __func__);
			xcsdh_check_card_present(host);			
			if(!host->card_present){
				printf("card removed, reset host controller\n");
				mmc->has_init = 0;
				err = NO_CARD_ERR;
				xcsdh_reset_smcc_core(host);
				goto done;					
			}
		}
	}		


error:
	if(cmd->resp_type & MMC_RSP_PRESENT){
		if(cmd->resp_type & MMC_RSP_136){
			xcsdh_access_smcc_reg(host->core_id,SMCC_RESP0, &cmd->response[3], 0, 0);
			xcsdh_access_smcc_reg(host->core_id,SMCC_RESP1, &cmd->response[2], 0, 0);
			xcsdh_access_smcc_reg(host->core_id,SMCC_RESP2, &cmd->response[1], 0, 0);
			xcsdh_access_smcc_reg(host->core_id,SMCC_RESP3, &cmd->response[0], 0, 0);
		} else {
			xcsdh_access_smcc_reg(host->core_id,SMCC_RESP0, &cmd->response[0], 0, 0);
		}
		mmc_debug(" [%s] cmd: %d response 0=%x 1=%x 2=%x 3=%x\n", __func__, cmd->cmdidx, 
			cmd->response[0], cmd->response[1], cmd->response[2], cmd->response[3]);
	}

done:
	if(err){
		mmc_debug(" [%s] cmd: %d error, errno: %d\n", __func__, cmd->cmdidx, err);
	}
	xcsdh_access_smcc_reg(host->core_id,SMCC_RINTSTS, &rintsts, 0, 0);
	xcsdh_access_smcc_reg(host->core_id,SMCC_RINTSTS, NULL, rintsts, 1);
	mmc_debug(" [%s] exit\n", __func__);
	return err;
}

static int mmc_send_cmd(struct mmc *mmc, struct mmc_cmd *cmd,
			struct mmc_data *data)
{
	struct mmc_host *host = (struct mmc_host *)mmc->priv;
	unsigned int flags = 0, args, reg = 0;
	int err;

	mmc_debug(" mmc_send_cmd called\n");
	mmc_debug("[%s] mmc = %x host = %x\n", __func__, mmc, host);
	
	/* wait for last transaction done */
	err = xcsdh_wait_ciu(host->core_id, 1000);
	if (err){
		mmc_debug("wait for controller receiving command timed out\n");
		return err;
	}
	
	
	if(cmd->cmdidx == MMC_CMD_GO_IDLE_STATE){
		/* CMD0 need init bit */
		reg = (1 << SMCC_CMD_START_CMD_SHIFT) | (1 << SMCC_CMD_SEND_INIT_SHIFT);		
	} else {
		reg = 1 << SMCC_CMD_START_CMD_SHIFT;
	}

	if(cmd->resp_type & MMC_RSP_PRESENT){
		flags = 1 << SMCC_CMD_RSP_EXP_SHIFT;
	}

	if(cmd->resp_type & MMC_RSP_136){
		flags |= (1 << SMCC_CMD_RSP_LEN_SHIFT);
	}
	
	if(cmd->resp_type & MMC_RSP_CRC){
		flags |= (1 << SMCC_CMD_CHK_RSP_CRC_SHIFT);
	}

	if((cmd->cmdidx == 17)||(cmd->cmdidx == 18)||(cmd->cmdidx == 24)||(cmd->cmdidx == 25)){
		xcsdh_access_smcc_reg(host->core_id, SMCC_CTRL, NULL, 0x2000010 | (0x3<<9), 1);
	}
	
	reg |= (flags | (1 << 29) | cmd->cmdidx);
	args = cmd->cmdarg;

	mmc_debug(" [%s] opcode %d args: %08x reg %08x\n", __func__, cmd->cmdidx, cmd->cmdarg, reg);

	if (data){
		/* RW command */
		if(data->flags & MMC_DATA_READ){
			mmc_debug(" [%s] read data command blksize: %08x blks: %08x \n", __func__, data->blocksize, data->blocks);		
		} else if (data->flags & MMC_DATA_WRITE){
			mmc_debug(" [%s] write data command blksize: %08x blks: %08x \n", __func__, data->blocksize, data->blocks);
		} else {
			printf("impossible received data command without any flag\n");
			err = -1;
			goto out;
		}

		/* Set up iDMA Desc & Buffer and do data encryption */
		mmc_prepare_data(host, cmd, data);
		
		/* write */
		//mmc_set_transfer_mode(host, data);

		err = xcsdh_access_smcc_reg(host->core_id, SMCC_BYTCNT, NULL, (data->blocksize * data->blocks), 1);
		if (err)
			goto out;

		err = xcsdh_access_smcc_reg(host->core_id, SMCC_BLKSIZ, NULL, data->blocksize, 1);
		if (err)
			goto out;

#if 0 /* u-boot always send CMD12 */
		if((data->blocks > 1)&&(cmd->cmdidx != MMC_CMD_APP_CMD))
			reg |= 1 << SMCC_CMD_SEND_AUTO_STOP_SHIFT;
#endif

		reg |= 1 << SMCC_CMD_DATA_EXP_SHIFT;

		if(data->flags & MMC_DATA_WRITE)
			reg |= 1 << SMCC_CMD_DATA_RW_SHIFT;
		else
			reg &= ~(1 << SMCC_CMD_DATA_RW_SHIFT);
#if 0
		/* not sure */
		if(mmc->ocr & OCR_HCS){
			mmc_debug(" [%s] detect a HC card mmc->ocr: %08x\n", __func__, mmc->ocr);
			/* SDHC, count with block (512) */
			args = cmd->cmdarg >> 9;
		}
#endif

	} else {
		xcsdh_access_smcc_reg(host->core_id, SMCC_BYTCNT, NULL, 0, 1);		
		xcsdh_access_smcc_reg(host->core_id, SMCC_BLKSIZ, NULL, 0, 1);
	}
	
	/* SEND MMC COMMAND */
	mmc_debug(" [%s] write cmd reg: 0x%08x, opcode: %d, arg: 0x%08x \n",__func__, reg, cmd->cmdidx, args);

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CMDARG, NULL, args, 1);
	if (err)
		goto out;
	
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CMD, NULL, reg, 1);
	if (err)
		goto out;

	err = xcsdh_wait_ciu(host->core_id, 1000);
	if (err)
		goto out;

	/* wait for command done, timeout 10 ms */
	if(!mmc->m_no_wait){
	    err = mmc_wait_cmd_done(mmc, cmd, data, 1000);	    
	}else{
            printf("skip wait for cmd done!\n");
	}

out:
	if(err)
		mmc_debug(" [%s] ====> error!!!\n", __func__);
	else
		mmc_debug(" [%s] success\n\n", __func__);
	return err;	
}

/* DB p177, pre clock command */
static inline int mmc_load_clock_reg(struct mmc_host *host)
{
	int err = 0;	
	volatile u32 reg = 	1 << SMCC_CMD_START_CMD_SHIFT 	| \
						1 << SMCC_CMD_UPDATE_CLK_SHIFT 	| \
						1 << SMCC_CMD_WAIT_PRVDATA_CMP_SHIFT | \
						1 << SMCC_CMD_USE_HOLD_REG_SHIFT;

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CMD, NULL, reg, 1);
	do {__asm__ __volatile__ ("dsb" : : : "memory");} while(0);

	return err;
}

/* take input clock divider, it is increment on the 2 power of clk_div*/
static int mmc_change_clock(struct mmc *mmc, uint clk_div)
{
	volatile u32 reg;
	int err = 0;
	struct mmc_host *host = mmc->priv;

	/* disable clock */
	reg = 0;
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CLKENA, NULL, reg, 1);
	if (err)
		goto out;
	if(mmc_load_clock_reg(host))
		goto out;
	
	/* set clock */
	reg = clk_div;
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CLKDIV, NULL, reg, 1);
	if (err)
		goto out;
	/* Set clock source (always 0) */
	reg = 0;
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CLKSRC, NULL, reg, 1);
	if (err)
		goto out;
	if(mmc_load_clock_reg(host))
		goto out;

	reg = 1;
	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CLKENA, NULL, reg, 1);

	if(mmc_load_clock_reg(host))
		goto out;

	/* Enable clock */
	if (clk_div == 0) 
		host->clock = mmc->f_max;
	else 
		host->clock = mmc->f_max / (clk_div * 2);

#ifdef MMC_DEBUG
	xcsdh_access_smcc_reg(host->core_id, SMCC_CLKDIV, &reg, 0, 0);
	mmc_debug(" [%s] clock divder = %08x\n", __func__, reg);
	xcsdh_access_smcc_reg(host->core_id, SMCC_CLKSRC, &reg, 0, 0);
	mmc_debug(" [%s] clock source = %08x\n", __func__, reg);
#endif
	
	out:
		if(err)
			mmc_debug(" [%s] fail\n", __func__);
		return (err);
}

static int xcsdh_set_bus_width(struct mmc_host *host, u32 w)
{
	volatile u32 reg;
	int err;

	switch(w) {
		case 1:
			reg = 0x0;
			break;
		case 4:
			reg = 0x01;	/* only 1 card... */
			break;
		default:
			reg = 0x0;
			break;
	}

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_CTYPE, NULL, reg, 1);
//	xcsdh_access_smcc_reg(host->core_id, SMCC_CTYPE, &reg, 0, 0);
	return(err);
}

static void mmc_set_ios(struct mmc *mmc)
{
	struct mmc_host *host = mmc->priv;
	unsigned int clk_div = 0;
	
	mmc_debug(" [%s] bus_width: %d, clock: %d\n", __func__, mmc->bus_width, mmc->clock);

	/* Change clock first */
	if (mmc->clock != 0) {
		clk_div = mmc->f_max / mmc->clock;
		clk_div = (int) (clk_div / 2); /* its the clock devider */
		mmc_debug("[%s] requested mmc->clock = %d, clk divider = %d\n",
				__func__, mmc->clock, clk_div);
	
		if(mmc_change_clock(mmc, clk_div))
			printf("mmc set clock fail\n");			
	}
	
	/* Change the bus width */
	if(mmc->bus_width == 8) {
		printf("Host does not support 8 bit bus width\n");
	} else if(mmc->bus_width == 4) {
		if(xcsdh_set_bus_width(host, 4))
			printf("mmc set bus width %d fail\n", mmc->bus_width);
	} else {
		if(xcsdh_set_bus_width(host, 1))
			printf("mmc set bus width %d fail\n", mmc->bus_width);
	}
	
	return;
}

static int mmc_core_init(struct mmc *mmc)
{
	struct mmc_host *host = (struct mmc_host *)mmc->priv;
	unsigned int version = 0;
	int err;

	mmc_debug("[%s] begin\n", __func__);

	err = xcsdh_access_smcc_reg(host->core_id, SMCC_VERID, &version, 0, 0);
	if(err)
		goto init_fail;
	mmc_debug("[%s] mmc host controller version: %x \n", __func__, version);

	xcsdh_check_card_present(host);

	mmc_debug(" [%s] detect card in slot %d\n", __func__, host->core_id);

	if(xcsdh_enable_dma(host))
		goto init_fail;

	if(xcsdh_setup_polling_mode(host))
		goto init_fail;

	return 0;

init_fail:
	mmc_debug(" [%s] failed!!!\n", __func__);
	return -1;
}

static int xcode_mmc_getcd(struct mmc *mmc)
{
	struct mmc_host *host = (struct mmc_host *)mmc->priv;	

	xcsdh_check_card_present(host);		

	return (host->card_present);
}

/*
  * dev_index	- Host Controller index
  * bus_width	- support 1/4 bits data bus width
  * pwr_gpio 	- Output: Host slot power control gpio, on SDK board is always on
  * oc_gpio	- Input: Over Current detect gpio, on SDK board is discard
  */
int xcode_mmc_init(int dev_index, int bus_width, int pwr_gpio, int oc_gpio)
{
	struct mmc_host *host;
	struct mmc *mmc;

	mmc_debug(" [%s]: index %d, bus width %d "
		"pwr_gpio %d oc_gpio %d\n",
		__func__, dev_index, bus_width, pwr_gpio, oc_gpio);

	host = &mmc_host[dev_index];
	mmc = &mmc_dev[dev_index];
	memset((void *)host, 0, sizeof(struct mmc_host));
	memset((void *)mmc, 0, sizeof(struct mmc));
	
	host->core_id = dev_index;

	xcsdh_reset_smcc_core(host);

	sprintf(mmc->name, "xcode-mmc");
	mmc->priv = host;
	mmc->send_cmd = mmc_send_cmd;
	mmc->set_ios = mmc_set_ios;
	mmc->init = mmc_core_init;
	mmc->getcd = xcode_mmc_getcd;


	/* set up voltage */
	mmc->voltages = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36;
	
	/* set up bus width */
	if (bus_width == 4){
		mmc->host_caps = MMC_MODE_4BIT;
	} else if(bus_width == 8) {
		printf("Warning mmc host does not support 8-bit bus\n");
		return -1;
	} else {
		printf("Default mmc host bus width 1-bit\n");
	}
	
	/* setup host capability */
	mmc->host_caps |= (MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_HC);

	/*
	 * setup frequency
	 * min freq is for card identification, it is 400KHz
	 * max freq is 50MHz
	 */
	mmc->f_min = 400 * 1000;
	mmc->f_max = 50 * 1000 * 1000;	

	mmc_debug("register mmc device at %p\n", (void *)mmc);	
	mmc_register(mmc);	
	return 0;
}

int board_mmc_init(bd_t *bis)
{
	int err = 0;

	mmc_debug("[%s] begin\n", __func__);

	xcsdh_reset_smcc_hw();
	
	err = xcode_mmc_init(0, 4, -1, -1);
	if(err){
		puts("board mmc init for smcc0 fail\n");
	    return err;
	}

    if (xcsdh_need_support_dual_host()){
	  	err = xcode_mmc_init(1, 4, -1, -1);
	  	if(err){
	  		puts("board mmc init for smcc1 fail\n");
	  	    return err;
	  	}
    }
}

