/*
 * Copyright (C) Freescale Semiconductor, Inc. 2006.
 * Author: Jason Jin<Jason.jin@freescale.com>
 *         Zhang Wei<wei.zhang@freescale.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * with the reference on libata and ahci drvier in kernel
 *
 */
#include <common.h>

#include <command.h>
#include <pci.h>
#include <asm/processor.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <malloc.h>
#include <scsi.h>
#include <ata.h>
#include <linux/ctype.h>
#include <ahci.h>

//#undef debug
//#define debug printf
//#define TR printf("%s, %d\n", __FILE__, __LINE__);

#define TR

#ifdef CONFIG_XCODE
#include <asm/arch/xcodeRegDef.h>

#define VIXS_SATA_MAX_HOST		1
#define VIXS_SATA_HOST_SEL_MASK	0x80000000	// we pick bit 31 to select host. 0: host0, 1: host1

u32 g_xcode_ahci_clk = 100; //100Mhz by default
u32 g_xcode_ahci_clk_type = 1;  //0: internal clock 1: external clock
u32 g_xcode_ahci_ss_mode = 0;	//0: not spread spectrum 1: spred spectrum
u32 g_sata0_limit_speed = 0; /* 0: no limit, 1: limit SATA speed to Gen1 */
u32 g_sata1_limit_speed = 0;
u32 g_sata_hw_inited = 0;

void* SDSH_SATA_REG_BASE = (void *)(XC_SOC_PROC_MMREG_BASE + SATA_AHB_REG_CTRL);
extern u32 SDSH_writel(void* SATA_reg_base, void * sata_reg_addr, u32 value);
extern u32 SDSH_readl(void* SATA_reg_base, void * sata_reg_addr);
extern u32 SDSH_writeb(void* SATA_reg_base, void * sata_reg_addr, u8 value);
extern u8 SDSH_readb(void* SATA_reg_base, void * sata_reg_addr);
extern u32 SDSH_writew(void* SATA_reg_base, void * sata_reg_addr, u16 value);
extern u16 SDSH_readw(void* SATA_reg_base, void * sata_reg_addr);
extern u32 SDSH_writesl(void * SATA_reg_base, void * sata_reg_addr, u32*pbuf, u32 count);
extern u32 SDSH_readsl(void* SATA_reg_base, void * sata_reg_addr, u32 *pbuf, u32 count);
extern u32 SDSH_writesb(void * SATA_reg_base, void * sata_reg_addr, u8* pbuf , u32 count);
extern u32 SDSH_readsb(void* SATA_reg_base, void * sata_reg_addr, u8 *pbuf, u32 count);
extern u32 SDSH_writesw(void * SATA_reg_base, void * sata_reg_addr, u16* pbuf , u32 count);
extern u32 SDSH_readsw(void* SATA_reg_base, void * sata_reg_addr, u16* pbuf, u32 count);


u32 SDSH_writel(void * SATA_reg_base, void * sata_reg_addr, u32 value)
{
	u32 regVal = 0;

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	writel(value, SATA_reg_base + 8);

	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80220000;        
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80210000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) { return 0;};
	return 0;
}

    
u32 SDSH_readl(void* SATA_reg_base, void * sata_reg_addr)
{
	u32 regVal = 0;
		
	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00220000;
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00210000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) 
	{
		return 0;
	}

	regVal = readl(SATA_reg_base + 0xc);

	return regVal;
}

u32 SDSH_writeb(void* SATA_reg_base, void * sata_reg_addr, u8 value)
{
	u32 regVal = 0;

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	writel(value, SATA_reg_base + 8);

	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80020000;        
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80010000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) { return 0;};

	return 0;
}

u8 SDSH_readb(void* SATA_reg_base, void * sata_reg_addr)
{
	u32 regVal = 0;

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00020000;
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00010000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) 
	{
		return 0;
	}

	regVal = (u8)readl(SATA_reg_base + 0xc);

	return (u8)regVal;
}


u32 SDSH_writew(void* SATA_reg_base, void * sata_reg_addr, u16 value)
{
    u32 regVal = 0;

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	writel(value, SATA_reg_base + 8);

	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80120000;        
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x80110000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) { return 0;};

	return 0;
}


u16 SDSH_readw(void* SATA_reg_base, void * sata_reg_addr)
{
	u32 regVal = 0;

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	regVal = ((u32)sata_reg_addr)|0x00110000;
	if(((u32)sata_reg_addr)&VIXS_SATA_HOST_SEL_MASK) //Second Host
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00120000;
	}
	else
	{
		regVal = ((u32)sata_reg_addr&0xffff)|0x00110000;
	}

	writel(regVal, SATA_reg_base + 0);

	while(((regVal = readl(SATA_reg_base + 4))&0x1)== 1){};

	if(regVal&0x2) 
	{
		return 0;
	}

	regVal = (u16)readl(SATA_reg_base + 0xc);

	return (u16)regVal;
}



u32 SDSH_writesl(void * SATA_reg_base, void * sata_reg_addr, u32*pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        SDSH_writel(SATA_reg_base, sata_reg_addr, (u32)*p);
        p++;
    }

    return 0;
}

u32 SDSH_readsl(void* SATA_reg_base, void * sata_reg_addr, u32 *pbuf, u32 count)
{
    u32 i;
    u32 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = SDSH_readl(SATA_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}
    
u32 SDSH_writesb(void * SATA_reg_base, void * sata_reg_addr, u8* pbuf , u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        SDSH_writeb(SATA_reg_base, sata_reg_addr, (u8)*p);
        p++;
    }

    return 0;
}
    
u32 SDSH_readsb(void* SATA_reg_base, void * sata_reg_addr, u8 *pbuf, u32 count)
{
    u32 i;
    u8 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = SDSH_readb(SATA_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}    

u32 SDSH_writesw(void * SATA_reg_base, void * sata_reg_addr, u16* pbuf , u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        SDSH_writew(SATA_reg_base, sata_reg_addr, (u16)*p);
        p++;
    }

    return 0;
}
    
u32 SDSH_readsw(void* SATA_reg_base, void * sata_reg_addr, u16* pbuf, u32 count)
{
    u32 i;
    u16 *p = pbuf;

    for(i = 0; i < count; i++)
    {
        *p = SDSH_readw(SATA_reg_base, sata_reg_addr);
        p++;
    }

    return 0;
}



#define SATA_readb(a)	    SDSH_readb(SDSH_SATA_REG_BASE, (void*)a)
#define SATA_readw(a)	    SDSH_readw(SDSH_SATA_REG_BASE, (void*)a)
#define SATA_readl(a)	    SDSH_readl(SDSH_SATA_REG_BASE, (void*)a)
#define SATA_writeb(a,b)	    SDSH_writeb(SDSH_SATA_REG_BASE, (void*)b,a)
#define SATA_writew(a,b)	    SDSH_writew(SDSH_SATA_REG_BASE, (void*)b,a)
#define SATA_writel(a,b)	    SDSH_writel(SDSH_SATA_REG_BASE, (void*)b,a)

#endif


struct ahci_probe_ent *probe_ent = NULL;
hd_driveid_t *ataid[AHCI_MAX_PORTS];

#define SATA_writel_with_flush(a,b)	do { SATA_writel(a,b); SATA_readl(b); } while (0)
static int ata_io_flush(u8 port);

/*
 * Some controllers limit number of blocks they can read/write at once.
 * Contemporary SSD devices work much faster if the read/write size is aligned
 * to a power of 2.  Let's set default to 128 and allowing to be overwritten if
 * needed.
 */
#ifndef MAX_SATA_BLOCKS_READ_WRITE
#define MAX_SATA_BLOCKS_READ_WRITE	0x80
#endif

/* Maximum timeouts for each event */
#define WAIT_MS_SPINUP	10000
#define WAIT_MS_DATAIO	1500000	// 5000
#define WAIT_MS_FLUSH	5000
#define WAIT_MS_LINKUP	1000		// 4

static inline u32 ahci_port_base(u32 base, u32 port)
{
	return base + 0x100 + (port * 0x80);
}


static void ahci_setup_port(struct ahci_ioports *port, unsigned long base,
			    unsigned int port_idx)
{
	base = ahci_port_base(base, port_idx);

	port->cmd_addr = base;
	port->scr_addr = base + PORT_SCR;
}


#define msleep(a) udelay(a * 1000)
#define ssleep(a) msleep(a * 1000)

#ifdef CONFIG_XCODE

#define POWER_OFF_RXPLL_SATA 0x2335400b
#define POWER_ON_RXPLL_SATA  0x2f35400b
#define RESET_PHY 0x1
#define RESET_SATA0 0x3f
#define RESET_SATA1 0xcf
#define RESET_ALL   0xff


#define SATA_INTERNAL_CLOCK_25  25
#define SATA_INTERNAL_CLOCK_50  50
#define SATA_INTERNAL_CLOCK_100  100

u32 regRead( u32 regOffset )
{
    return (readl(XC_SOC_PROC_MMREG_BASE + regOffset));
}

void regWrite( u32 regOffset, u32 regData )
{
	writel(regData, XC_SOC_PROC_MMREG_BASE + regOffset);
}

void RegMaskWrite(u32 regOffset, u32 regData, u32 regMask)
{
   unsigned int temp1, temp2;
   temp1 = regRead(regOffset);
   temp2 = regData & regMask;
   temp1 = temp1 & (~regMask);
   temp2 = temp2 | temp1;
   regWrite(regOffset,temp2);
}  

void RegPoll(u32 regOffset, u32 regStatus, u32 regMask, u32 loop, u32 delay)
{
	unsigned int temp1, temp2;
	unsigned int loop_counter = 0;

	while(loop_counter++ < loop) {
		temp1 = regRead(regOffset);
		temp2 = temp1 & regMask;
		if (temp2 == regStatus)
			break;
		udelay(1000);
	}
}

void SATA_SET_CLOCK(int sata_clock, int mode,int sata0_disable, int sata1_disable)
{
	switch(sata_clock)
	{
	case SATA_INTERNAL_CLOCK_25:
		RegMaskWrite(CG1_CLK_SRC_SEL3, 2 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
		RegMaskWrite(SATA_PHY6G_CTRL0, 0x78 << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
		break;

    case SATA_INTERNAL_CLOCK_50:
		RegMaskWrite(CG1_CLK_SRC_SEL3, 2 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
		RegMaskWrite(SATA_PHY6G_CTRL0, 0x3C << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
		break;

	case SATA_INTERNAL_CLOCK_100:
	default: //SATA_INTERNAL_CLOCK_100
		RegMaskWrite(CG1_CLK_SRC_SEL3, 1 << CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_SHIFT, CG1_CLK_SRC_SEL3_MOCA_REFCLK_SRC_SEL_MASK); //Select proper freq
		RegMaskWrite(SATA_PHY6G_CTRL0, 0  << SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_CLKDIV2_MASK);
		RegMaskWrite(SATA_PHY6G_CTRL0, 0x1E << SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_SHIFT, SATA_PHY6G_CTRL0_PHY_PLL_MULTIPLIER_MASK);
		break;
	}

	if(g_xcode_ahci_clk_type == 0) // internal clock
	{
		RegMaskWrite(SATA_PHY6G_CTRL0, 0 << SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_MASK);//Set to internal clock frequency
	}
	else if(g_xcode_ahci_clk_type == 1) // external clock
	{
		RegMaskWrite(SATA_PHY6G_CTRL0, 1 << SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_MASK);//Set to external clock frequency
	}
    else // default: internal clock
    {
		RegMaskWrite(SATA_PHY6G_CTRL0, 0 << SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_SHIFT, SATA_PHY6G_CTRL0_PHY_REF_USE_PAD_MASK);//Set to internal clock frequency
	}      

	if(sata0_disable == 0)
		RegMaskWrite(SATA_SFT_RESETS,RESET_SATA0,RESET_SATA0 );
	if(sata1_disable == 0)
		RegMaskWrite(SATA_SFT_RESETS,RESET_SATA1,RESET_SATA1 );

	RegPoll(SATA_PHY_COMMON_STATUS, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, SATA_PHY_COMMON_STATUS_SATA0_PHY_READY_MASK, 10, 10000);
	RegMaskWrite(SATA_SFT_RESETS,RESET_ALL,RESET_ALL);	
}

#ifdef CONFIG_SATA_RXPN_SWAP_FIX
static void SATAWritePHYReg(int addr, int data)
{
	writel(addr | 0x10000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	whilei(!(readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK));

	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	while(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK);

	writel(data|0x20000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	while(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK);

	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	while(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK);

	writel(0x40000, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	while(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK);

	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_OUT);
	udelay(1000);
	while(!readl(XC_SOC_PROC_MMREG_BASE + SATA_PHY_CR_IN) & SATA_PHY_CR_IN_ACK_MASK);
}
#endif

static void xcode_ahci_host_init(void )
{
	u32 temp;
	u32 sata_speed = 2; //1 for gen1, 2 for gen2

	/* configure PADU_CTRL */
	xcode_setval(1, PADU_CTRL_MOCA, RBM_PADU_CTRL);
	udelay(100);
	/* put sata into reset */
	xcode_setval(1, SATA_RESET, ACC_RESET_REG0);
	xcode_setval(1, MOCA_RESET, ACC_RESET_REG0);
	udelay(100);
	/* enable sata clock */
	xcode_setval(0, MOCA_BLK_STOP, ACC_BLK_STOP0);
	xcode_setval(0, MOCACLK_STOP, CG1_CLK_STOP0);
	udelay(100);
	/* take sata out of reset */
	xcode_setval(0, SATA_RESET, ACC_RESET_REG0);
	xcode_setval(0, MOCA_RESET, ACC_RESET_REG0);
	udelay(1000);
	/* soft reset the sata core */
	writel(0, XC_SOC_PROC_MMREG_BASE + SATA_SFT_RESETS);
	xcode_setval(1, SATA_RESET_MODE, SATA_CONTROL);
	/* set up the sata core speed */
	#define MOCA_DUMMY_REG_0 0x3160
	if(sata_speed == 1) {
		xcode_setval(0, SATA0_PHY_SPDMODE, SATA_CONTROL);
		writel(0x100, XC_SOC_PROC_MMREG_BASE + MOCA_DUMMY_REG_0);
	} else {
		xcode_setval(1, SATA0_PHY_SPDMODE, SATA_CONTROL);
		writel(0x102, XC_SOC_PROC_MMREG_BASE + MOCA_DUMMY_REG_0);
	}

	SATA_SET_CLOCK(g_xcode_ahci_clk, 1, 0, 0);
	writel(1, XC_SOC_PROC_MMREG_BASE + SATA_SFT_RESETS);
	temp = readl(XC_SOC_PROC_MMREG_BASE + SATA_SFT_RESETS);
	temp |= 0x3E;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SATA_SFT_RESETS);

	xcode_setval(1, PHY_SFT_RSTN, SATA_SFT_RESETS);
	xcode_setval(0, PHY_SFT_RSTN, SATA_SFT_RESETS);
	xcode_setval(1, PHY_SFT_RSTN, SATA_SFT_RESETS);
	udelay(500);
#ifdef CONFIG_SATA_RXPN_SWAP_FIX
	SATAWritePHYReg(0x2107, 0x8);
#endif
}

static void xcode_ahci_set_oob(void  *mmio, u32 clock)
{
        //set up OOB register here for CLK setting
#ifdef CONFIG_XCODE_FPGA
                SATA_writel(0x84101730, mmio + HOST_OOB);
                SATA_writel(0x84101730, mmio + HOST_OOB);
#else				
        switch(clock)
        {
            case 25:
            case 100:
            case 50:
                SATA_writel(0x870E192B, mmio + HOST_OOB);
                SATA_writel(0x870E192B, mmio + HOST_OOB);
                break;
            case 75:
                SATA_writel(0x840a111e, mmio + HOST_OOB);                
                SATA_writel(0x840a111e, mmio + HOST_OOB);                
                break;
        }
#endif
}

#endif

static void sata_config(void)
{
	if(getenv("sata_clkfreq"))
	{
		printf("Clock Freq is %sMHZ\n", getenv("sata_clkfreq"));
		if(strcmp(getenv("sata_clkfreq"), "100") == 0)
		{
			g_xcode_ahci_clk = 100;
		}
		else if(strcmp(getenv("sata_clkfreq"), "50") == 0)
		{
			g_xcode_ahci_clk = 50;
		}
		else
		{
			printf("Not supported SATA clock, set to default 100MHZ\n");
			g_xcode_ahci_clk = 100;  
		}
	}
	else
	{
		printf("Can't find ENV sata_clkfreq, set it to default clock 100MHZ\n");
		g_xcode_ahci_clk = 100;
	}


	if(getenv("sata_clktype"))
	{
		if(strcmp(getenv("sata_clktype"), "1") == 0)
		{		
		    printf("Set clock type to external clock\n");
			g_xcode_ahci_clk_type = 1;
		}
		else if(strcmp(getenv("sata_clktype"), "0") == 0)
		{
            printf("Set clock type to internal clock\n");
			g_xcode_ahci_clk_type = 0;
		}
		else
		{
			printf("Not supported SATA clock type, set it to default: external clock\n");
			g_xcode_ahci_clk_type = 1;  
		}
	}
	else
	{
		printf("Can't find ENV sata_clktype, set it to default: external clock\n");
		g_xcode_ahci_clk_type = 1;
	}


	if(getenv("sata_ssmode"))
	{
		if(strcmp(getenv("sata_ssmode"), "0") == 0)
		{
			printf("Disable SATA SS mode\n");
			g_xcode_ahci_ss_mode = 0;
		}
		else
		{
			printf("Enable SATA SS mode\n");
			g_xcode_ahci_ss_mode = 1;
		}
	}
	else
	{
		printf("Can't find ENV sata_ssmode, set it to default: disabled\n");
		g_xcode_ahci_ss_mode = 0;
	}

	if(getenv("sata0_limit_speed"))
	{
		/* Limit SATA0 speed to Generation1 */
		if(strcmp(getenv("sata0_limit_speed"), "1") == 0)
			g_sata0_limit_speed = 1;
	}
	if(getenv("sata1_limit_speed"))
	{
		/* Limit SATA1 speed to Generation1 */
		if(strcmp(getenv("sata1_limit_speed"), "1") == 0)
			g_sata1_limit_speed = 1;
	}

}
 
static void ahci_dcache_flush_range(unsigned begin, unsigned len)
{
	const unsigned long start = begin & ~(CONFIG_SYS_CACHELINE_SIZE - 1);
	const unsigned long end = ALIGN_CACHE_SIZE(start + len);

	debug("%s: flush dcache: [%#lx, %#lx)\n", __func__, start, end);
	flush_dcache_range(start, end);
}

/*
 * SATA controller DMAs to physical RAM.  Ensure data from the
 * controller is invalidated from dcache; next access comes from
 * physical RAM.
 */
static void ahci_dcache_invalidate_range(unsigned begin, unsigned len)
{
	const unsigned long start = begin & ~(CONFIG_SYS_CACHELINE_SIZE - 1);
	const unsigned long end = ALIGN_CACHE_SIZE(start + len);

	debug("%s: invalidate dcache: [%#lx, %#lx)\n", __func__, start, end);
	invalidate_dcache_range(start, end);
}

/*
 * Ensure data for SATA controller is flushed out of dcache and
 * written to physical memory.
 */
static void ahci_dcache_flush_sata_cmd(struct ahci_ioports *pp)
{
	ahci_dcache_flush_range((unsigned long)pp->cmd_slot,
				AHCI_PORT_PRIV_DMA_SZ);
}

static int waiting_for_cmd_completed(volatile u8 *offset,
				     int timeout_msec,
				     u32 sign)
{
	int i;
	u32 status;

	for (i = 0; ((status = SATA_readl(offset)) & sign) && i < timeout_msec; i++)
		msleep(1);

	return (i < timeout_msec) ? 0 : -1;
}


static int ahci_host_init(struct ahci_probe_ent *probe_ent)
{
#ifndef CONFIG_SCSI_AHCI_PLAT
	pci_dev_t pdev = probe_ent->dev;
	u16 tmp16;
	unsigned short vendor;
#endif
	volatile u8 *mmio = (volatile u8 *)probe_ent->mmio_base;
	u32 tmp, cap_save, cmd;
	int i, j;
	volatile u8 *port_mmio;

	debug("ahci_host_init: start\n");

	if(!g_sata_hw_inited) {
		sata_config();
		xcode_ahci_host_init();
		g_sata_hw_inited = 1;
	}

	cap_save = SATA_readl(mmio + HOST_CAP);
	cap_save &= ((1 << 28) | (1 << 17));
	cap_save |= (1 << 27);  /* Staggered Spin-up. Not needed. */

	/* global controller reset */
	tmp = SATA_readl(mmio + HOST_CTL);
	if ((tmp & HOST_RESET) == 0)
		SATA_writel_with_flush(tmp | HOST_RESET, mmio + HOST_CTL);

	/* reset must complete within 1 second, or
	 * the hardware should be considered fried.
	 */
	i = 1000;
	do {
		udelay(1000);
		tmp = SATA_readl(mmio + HOST_CTL);
		if (!i--) {
			debug("controller reset failed (0x%x)\n", tmp);
			return -1;
		}
	} while (tmp & HOST_RESET);

	SATA_writel_with_flush(HOST_AHCI_EN, mmio + HOST_CTL);
	SATA_writel(cap_save, mmio + HOST_CAP);
	SATA_writel_with_flush(0xf, mmio + HOST_PORTS_IMPL);

	xcode_ahci_set_oob((void *)mmio, g_xcode_ahci_clk);

#ifndef CONFIG_SCSI_AHCI_PLAT
	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);

	if (vendor == PCI_VENDOR_ID_INTEL) {
		u16 tmp16;
		pci_read_config_word(pdev, 0x92, &tmp16);
		tmp16 |= 0xf;
		pci_write_config_word(pdev, 0x92, tmp16);
	}
#endif
	probe_ent->cap = SATA_readl(mmio + HOST_CAP);
	probe_ent->port_map = SATA_readl(mmio + HOST_PORTS_IMPL);
	probe_ent->n_ports = (probe_ent->cap & 0x1f) + 1;

	debug("cap 0x%x  port_map 0x%x  n_ports %d\n",
	      probe_ent->cap, probe_ent->port_map, probe_ent->n_ports);

	if (probe_ent->n_ports > CONFIG_SYS_SCSI_MAX_SCSI_ID)
		probe_ent->n_ports = CONFIG_SYS_SCSI_MAX_SCSI_ID;

	for (i = 0; i < probe_ent->n_ports; i++) {
		probe_ent->port[i].port_mmio = ahci_port_base((u32) mmio, i);
		port_mmio = (u8 *) probe_ent->port[i].port_mmio;
		ahci_setup_port(&probe_ent->port[i], (unsigned long)mmio, i);

		/* make sure port is not active */
		tmp = SATA_readl(port_mmio + PORT_CMD);
		if (tmp & (PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
			   PORT_CMD_FIS_RX | PORT_CMD_START)) {
			debug("Port %d is active. Deactivating.\n", i);
			tmp &= ~(PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
				 PORT_CMD_FIS_RX | PORT_CMD_START);
			SATA_writel_with_flush(tmp, port_mmio + PORT_CMD);

			/* spec says 500 msecs for each bit, so
			 * this is slightly incorrect.
			 */
			msleep(500);
		}

		/* Add the spinup command to whatever mode bits may
		 * already be on in the command register.
		 */
		cmd = SATA_readl(port_mmio + PORT_CMD);
		cmd |= PORT_CMD_FIS_RX;
		cmd |= PORT_CMD_SPIN_UP;
		SATA_writel_with_flush(cmd, port_mmio + PORT_CMD);

		/* Bring up SATA link.
		 * SATA link bringup time is usually less than 1 ms; only very
		 * rarely has it taken between 1-2 ms. Never seen it above 2 ms.
		 */
		j = 0;
		while (j < WAIT_MS_LINKUP) {
			tmp = SATA_readl(port_mmio + PORT_SCR_STAT);
			if ((tmp & 0xf) == 0x3)
				break;
			udelay(1000);
			j++;
		}
		if (j == WAIT_MS_LINKUP) {
			printf("SATA link %d timeout.\n", i);
			continue;
		} else {
			debug("SATA link ok.\n");
		}

		/* Clear error status */
		tmp = SATA_readl(port_mmio + PORT_SCR_ERR);
		if (tmp)
			SATA_writel(tmp, port_mmio + PORT_SCR_ERR);

		debug("Spinning up device on SATA port %d... ", i);

		j = 0;
		while (j < WAIT_MS_SPINUP) {			
			tmp = SATA_readl(port_mmio + PORT_TFDATA);

			if (!(tmp & (ATA_STAT_BUSY | ATA_STAT_DRQ)))
				break;
			udelay(1000);
			j++;
		}
		printf("Target spinup took %d ms.\n", j);
		if (j == WAIT_MS_SPINUP)
			debug("timeout.\n");
		else
			debug("ok.\n");

		tmp = SATA_readl(port_mmio + PORT_SCR_ERR);
		debug("PORT_SCR_ERR 0x%x\n", tmp);
		SATA_writel(tmp, port_mmio + PORT_SCR_ERR);

		/* ack any pending irq events for this port */
		tmp = SATA_readl(port_mmio + PORT_IRQ_STAT);
		debug("PORT_IRQ_STAT 0x%x\n", tmp);
		if (tmp)
			SATA_writel(tmp, port_mmio + PORT_IRQ_STAT);

		SATA_writel(1 << i, mmio + HOST_IRQ_STAT);

		/* set irq mask (enables interrupts) */
		SATA_writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK);

		/* register linkup ports */
		tmp = SATA_readl(port_mmio + PORT_SCR_STAT);
		debug("SATA port %d status: 0x%x\n", i, tmp);
		if ((tmp & 0xf) == 0x03)
			probe_ent->link_port_map |= (0x01 << i);
	}

	tmp = SATA_readl(mmio + HOST_CTL);
	debug("HOST_CTL 0x%x\n", tmp);
	SATA_writel(tmp | HOST_IRQ_EN, mmio + HOST_CTL);
	tmp = SATA_readl(mmio + HOST_CTL);
	debug("HOST_CTL 0x%x\n", tmp);
#ifndef CONFIG_SCSI_AHCI_PLAT
	pci_read_config_word(pdev, PCI_COMMAND, &tmp16);
	tmp |= PCI_COMMAND_MASTER;
	pci_write_config_word(pdev, PCI_COMMAND, tmp16);
#endif
	return 0;
}


static void ahci_print_info(struct ahci_probe_ent *probe_ent)
{
#ifndef CONFIG_SCSI_AHCI_PLAT
	pci_dev_t pdev = probe_ent->dev;
	u16 cc;
#endif
	volatile u8 *mmio = (volatile u8 *)probe_ent->mmio_base;
	u32 vers, cap, cap2, impl, speed;
	const char *speed_s;
	const char *scc_s;

	vers = SATA_readl(mmio + HOST_VERSION);
	cap = probe_ent->cap;
	cap2 = SATA_readl(mmio + HOST_CAP2);
	impl = probe_ent->port_map;

	speed = (cap >> 20) & 0xf;
	if (speed == 1)
		speed_s = "1.5";
	else if (speed == 2)
		speed_s = "3";
	else if (speed == 3)
		speed_s = "6";
	else
		speed_s = "?";

#ifdef CONFIG_SCSI_AHCI_PLAT
	scc_s = "SATA";
#else
	pci_read_config_word(pdev, 0x0a, &cc);
	if (cc == 0x0101)
		scc_s = "IDE";
	else if (cc == 0x0106)
		scc_s = "SATA";
	else if (cc == 0x0104)
		scc_s = "RAID";
	else
		scc_s = "unknown";
#endif
	printf("AHCI %02x%02x.%02x%02x "
	       "%u slots %u ports %s Gbps 0x%x impl %s mode\n",
	       (vers >> 24) & 0xff,
	       (vers >> 16) & 0xff,
	       (vers >> 8) & 0xff,
	       vers & 0xff,
	       ((cap >> 8) & 0x1f) + 1, (cap & 0x1f) + 1, speed_s, impl, scc_s);

	printf("flags: "
	       "%s%s%s%s%s%s%s"
	       "%s%s%s%s%s%s%s"
	       "%s%s%s%s%s%s\n",
	       cap & (1 << 31) ? "64bit " : "",
	       cap & (1 << 30) ? "ncq " : "",
	       cap & (1 << 28) ? "ilck " : "",
	       cap & (1 << 27) ? "stag " : "",
	       cap & (1 << 26) ? "pm " : "",
	       cap & (1 << 25) ? "led " : "",
	       cap & (1 << 24) ? "clo " : "",
	       cap & (1 << 19) ? "nz " : "",
	       cap & (1 << 18) ? "only " : "",
	       cap & (1 << 17) ? "pmp " : "",
	       cap & (1 << 16) ? "fbss " : "",
	       cap & (1 << 15) ? "pio " : "",
	       cap & (1 << 14) ? "slum " : "",
	       cap & (1 << 13) ? "part " : "",
	       cap & (1 << 7) ? "ccc " : "",
	       cap & (1 << 6) ? "ems " : "",
	       cap & (1 << 5) ? "sxs " : "",
	       cap2 & (1 << 2) ? "apst " : "",
	       cap2 & (1 << 1) ? "nvmp " : "",
	       cap2 & (1 << 0) ? "boh " : "");
}

#ifndef CONFIG_SCSI_AHCI_PLAT
static int ahci_init_one(pci_dev_t pdev)
{
	u16 vendor;
	int rc;

	memset((void *)ataid, 0, sizeof(hd_driveid_t *) * AHCI_MAX_PORTS);

	probe_ent = malloc(sizeof(struct ahci_probe_ent));
	memset(probe_ent, 0, sizeof(struct ahci_probe_ent));
	probe_ent->dev = pdev;

	probe_ent->host_flags = ATA_FLAG_SATA
				| ATA_FLAG_NO_LEGACY
				| ATA_FLAG_MMIO
				| ATA_FLAG_PIO_DMA
				| ATA_FLAG_NO_ATAPI;
	probe_ent->pio_mask = 0x1f;
	probe_ent->udma_mask = 0x7f;	/*Fixme,assume to support UDMA6 */

	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_5, &probe_ent->mmio_base);
	debug("ahci mmio_base=0x%08x\n", probe_ent->mmio_base);

	/* Take from kernel:
	 * JMicron-specific fixup:
	 * make sure we're in AHCI mode
	 */
	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
	if (vendor == 0x197b)
		pci_write_config_byte(pdev, 0x41, 0xa1);

	/* initialize adapter */
	rc = ahci_host_init(probe_ent);
	if (rc)
		goto err_out;

	ahci_print_info(probe_ent);

	return 0;

      err_out:
	return rc;
}
#endif

#define MAX_DATA_BYTE_COUNT  (4*1024*1024)

static int ahci_fill_sg(u8 port, unsigned char *buf, int buf_len)
{
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	struct ahci_sg *ahci_sg = pp->cmd_tbl_sg;
	u32 sg_count;
	int i;

	sg_count = ((buf_len - 1) / MAX_DATA_BYTE_COUNT) + 1;
	if (sg_count > AHCI_MAX_SG) {
		printf("Error:Too much sg!\n");
		return -1;
	}

	for (i = 0; i < sg_count; i++) {
		ahci_sg->addr =
		    cpu_to_le32((u32) buf + i * MAX_DATA_BYTE_COUNT);
		ahci_sg->addr_hi = 0;
		ahci_sg->flags_size = cpu_to_le32(0x3fffff &
					  (buf_len < MAX_DATA_BYTE_COUNT
					   ? (buf_len - 1)
					   : (MAX_DATA_BYTE_COUNT - 1)));
		ahci_sg++;
		buf_len -= MAX_DATA_BYTE_COUNT;
	}

	return sg_count;
}


static void ahci_fill_cmd_slot(struct ahci_ioports *pp, u32 opts)
{
	pp->cmd_slot->opts = cpu_to_le32(opts);
	pp->cmd_slot->status = 0;
	pp->cmd_slot->tbl_addr = cpu_to_le32(pp->cmd_tbl & 0xffffffff);
	pp->cmd_slot->tbl_addr_hi = 0;
}


#ifdef CONFIG_AHCI_SETFEATURES_XFER
static void ahci_set_feature(u8 port)
{
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 cmd_fis_len = 5;	/* five dwords */
	u8 fis[20];

	/* set feature */
	memset(fis, 0, sizeof(fis));
	fis[0] = 0x27;
	fis[1] = 1 << 7;
	fis[2] = ATA_CMD_SETF;
	fis[3] = SETFEATURES_XFER;
	fis[12] = __ilog2(probe_ent->udma_mask + 1) + 0x40 - 0x01;

	memcpy((unsigned char *)pp->cmd_tbl, fis, sizeof(fis));
	ahci_fill_cmd_slot(pp, cmd_fis_len);
	ahci_dcache_flush_sata_cmd(pp);
        flush_dcache_all(); //flush dcache here since every command will need to call this func
	SATA_writel(1, port_mmio + PORT_CMD_ISSUE);
	SATA_readl(port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE,
				WAIT_MS_DATAIO, 0x1)) {
		printf("set feature error on port %d!\n", port);
	}
}
#endif


static int ahci_port_start(u8 port)
{
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 port_status;
	u32 mem;

	debug("Enter start port: %d\n", port);
	port_status = SATA_readl(port_mmio + PORT_SCR_STAT);
	debug("Port %d status: %x\n", port, port_status);
	if ((port_status & 0xf) != 0x03) {
		printf("No Link on this port!\n");
		return -1;
	}

	mem = (u32) malloc(AHCI_PORT_PRIV_DMA_SZ + 2048);
	if (!mem) {
		free(pp);
		printf("No mem for table!\n");
		return -ENOMEM;
	}

	mem = (mem + 0x800) & (~0x7ff);	/* Aligned to 2048-bytes */
	memset((u8 *) mem, 0, AHCI_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot =
		(struct ahci_cmd_hdr *)(uintptr_t)virt_to_phys((void *)mem);
	debug("cmd_slot = 0x%x\n", (unsigned)pp->cmd_slot);
	mem += (AHCI_CMD_SLOT_SZ + 224);

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = virt_to_phys((void *)mem);
	mem += AHCI_RX_FIS_SZ;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = virt_to_phys((void *)mem);
	debug("cmd_tbl_dma = 0x%x\n", pp->cmd_tbl);

	mem += AHCI_CMD_TBL_HDR;
	pp->cmd_tbl_sg =
			(struct ahci_sg *)(uintptr_t)virt_to_phys((void *)mem);

	SATA_writel_with_flush((u32) pp->cmd_slot, port_mmio + PORT_LST_ADDR);

	SATA_writel_with_flush(pp->rx_fis, port_mmio + PORT_FIS_ADDR);

	SATA_writel_with_flush(PORT_CMD_ICC_ACTIVE | PORT_CMD_FIS_RX |
			  PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP |
			  PORT_CMD_START, port_mmio + PORT_CMD);

	debug("Exit start port %d\n", port);

	return 0;
}


static int ahci_device_data_io(u8 port, u8 *fis, int fis_len, u8 *buf,
				int buf_len, u8 is_write)
{

	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 opts;
	u32 port_status;
	int sg_count;

	debug("Enter %s: for port %d\n", __func__, port);

	if (port > probe_ent->n_ports) {
		printf("Invalid port number %d\n", port);
		return -1;
	}

	port_status = SATA_readl(port_mmio + PORT_SCR_STAT);
	if ((port_status & 0xf) != 0x03) {
		debug("No Link on port %d!\n", port);
		return -1;
	}

	memcpy((unsigned char *)pp->cmd_tbl, fis, fis_len);

	sg_count = ahci_fill_sg(port, buf, buf_len);
	opts = (fis_len >> 2) | (sg_count << 16) | (is_write << 6);
	ahci_fill_cmd_slot(pp, opts);

	ahci_dcache_flush_sata_cmd(pp);
	ahci_dcache_flush_range((unsigned)buf, (unsigned)buf_len);

	SATA_writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE,
				WAIT_MS_DATAIO, 0x1)) {
		printf("timeout exit!\n");
		return -1;
	}

	ahci_dcache_invalidate_range((unsigned)buf, (unsigned)buf_len);
	debug("%s: %d byte transferred.\n", __func__, pp->cmd_slot->status);

	return 0;
}


static char *ata_id_strcpy(u16 *target, u16 *src, int len)
{
	int i;
	for (i = 0; i < len / 2; i++)
		target[i] = swab16(src[i]);
	return (char *)target;
}


static void dump_ataid(hd_driveid_t *ataid)
{
	debug("(49)ataid->capability = 0x%x\n", ataid->capability);
	debug("(53)ataid->field_valid =0x%x\n", ataid->field_valid);
	debug("(63)ataid->dma_mword = 0x%x\n", ataid->dma_mword);
	debug("(64)ataid->eide_pio_modes = 0x%x\n", ataid->eide_pio_modes);
	debug("(75)ataid->queue_depth = 0x%x\n", ataid->queue_depth);
	debug("(80)ataid->major_rev_num = 0x%x\n", ataid->major_rev_num);
	debug("(81)ataid->minor_rev_num = 0x%x\n", ataid->minor_rev_num);
	debug("(82)ataid->command_set_1 = 0x%x\n", ataid->command_set_1);
	debug("(83)ataid->command_set_2 = 0x%x\n", ataid->command_set_2);
	debug("(84)ataid->cfsse = 0x%x\n", ataid->cfsse);
	debug("(85)ataid->cfs_enable_1 = 0x%x\n", ataid->cfs_enable_1);
	debug("(86)ataid->cfs_enable_2 = 0x%x\n", ataid->cfs_enable_2);
	debug("(87)ataid->csf_default = 0x%x\n", ataid->csf_default);
	debug("(88)ataid->dma_ultra = 0x%x\n", ataid->dma_ultra);
	debug("(93)ataid->hw_config = 0x%x\n", ataid->hw_config);
}


/*
 * SCSI INQUIRY command operation.
 */
static int ata_scsiop_inquiry(ccb *pccb)
{
	u8 hdr[] = {
		0,
		0,
		0x5,		/* claim SPC-3 version compatibility */
		2,
		95 - 4,
	};
	u8 fis[20];
	u8 *tmpid;
	u8 port;
TR;
	/* Clean ccb data buffer */
	memset(pccb->pdata, 0, pccb->datalen);
TR
	memcpy(pccb->pdata, hdr, sizeof(hdr));

	if (pccb->datalen <= 35)
		return 0;
TR
	memset(fis, 0, sizeof(fis));
	/* Construct the FIS */
	fis[0] = 0x27;		/* Host to device FIS. */
	fis[1] = 1 << 7;	/* Command FIS. */
	fis[2] = ATA_CMD_IDENT;	/* Command byte. */

	/* Read id from sata */
	port = pccb->target;
	if (!(tmpid = malloc(sizeof(hd_driveid_t))))
		return -ENOMEM;
TR
	if (ahci_device_data_io(port, (u8 *) &fis, sizeof(fis), tmpid,
				sizeof(hd_driveid_t), 0)) {
		debug("scsi_ahci: SCSI inquiry command failure.\n");
		return -EIO;
	}

	if (ataid[port])
		free(ataid[port]);
	ataid[port] = (hd_driveid_t *) tmpid;

	memcpy(&pccb->pdata[8], "ATA     ", 8);
	ata_id_strcpy((u16 *) &pccb->pdata[16], (u16 *)ataid[port]->model, 16);
	ata_id_strcpy((u16 *) &pccb->pdata[32], (u16 *)ataid[port]->fw_rev, 4);

	dump_ataid(ataid[port]);
	return 0;
}


/*
 * SCSI READ10/WRITE10 command operation.
 */
static int ata_scsiop_read_write(ccb *pccb, u8 is_write)
{
	u32 lba = 0;
	u16 blocks = 0;
	u8 fis[20];
	u8 *user_buffer = pccb->pdata;
	u32 user_buffer_size = pccb->datalen;

	/* Retrieve the base LBA number from the ccb structure. */
	memcpy(&lba, pccb->cmd + 2, sizeof(lba));
	lba = be32_to_cpu(lba);

	/*
	 * And the number of blocks.
	 *
	 * For 10-byte and 16-byte SCSI R/W commands, transfer
	 * length 0 means transfer 0 block of data.
	 * However, for ATA R/W commands, sector count 0 means
	 * 256 or 65536 sectors, not 0 sectors as in SCSI.
	 *
	 * WARNING: one or two older ATA drives treat 0 as 0...
	 */
	blocks = (((u16)pccb->cmd[7]) << 8) | ((u16) pccb->cmd[8]);

	debug("scsi_ahci: %s %d blocks starting from lba 0x%x\n",
	      is_write ?  "write" : "read", (unsigned)lba, blocks);

	/* Preset the FIS */
	memset(fis, 0, sizeof(fis));
	fis[0] = 0x27;		 /* Host to device FIS. */
	fis[1] = 1 << 7;	 /* Command FIS. */
	/* Command byte (read/write). */
	fis[2] = is_write ? ATA_CMD_WRITE_EXT : ATA_CMD_READ_EXT;

	while (blocks) {
		u16 now_blocks; /* number of blocks per iteration */
		u32 transfer_size; /* number of bytes per iteration */

		now_blocks = min(MAX_SATA_BLOCKS_READ_WRITE, blocks);

		transfer_size = ATA_BLOCKSIZE * now_blocks;
		if (transfer_size > user_buffer_size) {
			printf("scsi_ahci: Error: buffer too small.\n");
			return -EIO;
		}

		/* LBA48 SATA command but only use 32bit address range within
		 * that. The next smaller command range (28bit) is too small.
		 */
		fis[4] = (lba >> 0) & 0xff;
		fis[5] = (lba >> 8) & 0xff;
		fis[6] = (lba >> 16) & 0xff;
		fis[7] = 1 << 6; /* device reg: set LBA mode */
		fis[8] = ((lba >> 24) & 0xff);
		fis[3] = 0xe0; /* features */

		/* Block (sector) count */
		fis[12] = (now_blocks >> 0) & 0xff;
		fis[13] = (now_blocks >> 8) & 0xff;

		/* Read/Write from ahci */
		if (ahci_device_data_io(pccb->target, (u8 *) &fis, sizeof(fis),
					user_buffer, user_buffer_size,
					is_write)) {
			debug("scsi_ahci: SCSI %s10 command failure.\n",
			      is_write ? "WRITE" : "READ");
			return -EIO;
		}

		/* If this transaction is a write, do a following flush.
		 * Writes in u-boot are so rare, and the logic to know when is
		 * the last write and do a flush only there is sufficiently
		 * difficult. Just do a flush after every write. This incurs,
		 * usually, one extra flush when the rare writes do happen.
		 */
		if (is_write) {
			if (-EIO == ata_io_flush(pccb->target))
				return -EIO;
		}
		user_buffer += transfer_size;
		user_buffer_size -= transfer_size;
		blocks -= now_blocks;
		lba += now_blocks;
	}

	return 0;
}


/*
 * SCSI READ CAPACITY10 command operation.
 */
static int ata_scsiop_read_capacity10(ccb *pccb)
{
	u32 cap;
	u32 block_size;

	if (!ataid[pccb->target]) {
		printf("scsi_ahci: SCSI READ CAPACITY10 command failure. "
		       "\tNo ATA info!\n"
		       "\tPlease run SCSI commmand INQUIRY firstly!\n");
		return -EPERM;
	}

	cap = le32_to_cpu(ataid[pccb->target]->lba_capacity);
	if (cap == 0xfffffff) {
		unsigned short *cap48 = ataid[pccb->target]->lba48_capacity;
		if (cap48[2] || cap48[3]) {
			cap = 0xffffffff;
		} else {
			cap = (le16_to_cpu(cap48[1]) << 16) |
			      (le16_to_cpu(cap48[0]));
		}
	}

	cap = cpu_to_be32(cap);
	memcpy(pccb->pdata, &cap, sizeof(cap));

	block_size = cpu_to_be32((u32)512);
	memcpy(&pccb->pdata[4], &block_size, 4);

	return 0;
}


/*
 * SCSI READ CAPACITY16 command operation.
 */
static int ata_scsiop_read_capacity16(ccb *pccb)
{
	u64 cap;
	u64 block_size;

	if (!ataid[pccb->target]) {
		printf("scsi_ahci: SCSI READ CAPACITY16 command failure. "
		       "\tNo ATA info!\n"
		       "\tPlease run SCSI commmand INQUIRY firstly!\n");
		return -EPERM;
	}

	cap = le32_to_cpu(ataid[pccb->target]->lba_capacity);
	if (cap == 0xfffffff) {
		memcpy(&cap, ataid[pccb->target]->lba48_capacity, sizeof(cap));
		cap = le64_to_cpu(cap);
	}

	cap = cpu_to_be64(cap);
	memcpy(pccb->pdata, &cap, sizeof(cap));

	block_size = cpu_to_be64((u64)512);
	memcpy(&pccb->pdata[8], &block_size, 8);

	return 0;
}


/*
 * SCSI TEST UNIT READY command operation.
 */
static int ata_scsiop_test_unit_ready(ccb *pccb)
{
	return (ataid[pccb->target]) ? 0 : -EPERM;
}


int scsi_exec(ccb *pccb)
{
	int ret;
TR;
	switch (pccb->cmd[0]) {
	case SCSI_READ10:
		TR;ret = ata_scsiop_read_write(pccb, 0);
		break;
	case SCSI_WRITE10:
		TR;ret = ata_scsiop_read_write(pccb, 1);
		break;
	case SCSI_RD_CAPAC10:
		TR;ret = ata_scsiop_read_capacity10(pccb);
		break;
	case SCSI_RD_CAPAC16:
		TR;ret = ata_scsiop_read_capacity16(pccb);
		break;
	case SCSI_TST_U_RDY:
		TR;ret = ata_scsiop_test_unit_ready(pccb);
		break;
	case SCSI_INQUIRY:
	TR;	
	TR;
	ret = ata_scsiop_inquiry(pccb);
		break;
	default:
		printf("Unsupport SCSI command 0x%02x\n", pccb->cmd[0]);
		return FALSE;
	}

	if (ret) {
		debug("SCSI command 0x%02x ret errno %d\n", pccb->cmd[0], ret);
		return FALSE;
	}
	return TRUE;

}


void scsi_low_level_init(int busdevfunc)
{
	int i;
	u32 linkmap;

#ifndef CONFIG_SCSI_AHCI_PLAT
	ahci_init_one(busdevfunc);
#endif

#ifdef CONFIG_SCSI_AHCI_PLAT
	ahci_init(busdevfunc);
#endif

	linkmap = probe_ent->link_port_map;

	for (i = 0; i < CONFIG_SYS_SCSI_MAX_SCSI_ID; i++) {
		if (((linkmap >> i) & 0x01)) {
			if (ahci_port_start((u8) i)) {
				printf("Can not start port %d\n", i);
				continue;
			}
#ifdef CONFIG_AHCI_SETFEATURES_XFER
			ahci_set_feature((u8) i);
#endif
		}
	}
}

#ifdef CONFIG_SCSI_AHCI_PLAT
int ahci_init(u32 base)
{
	int i, rc = 0;
	u32 linkmap;

	memset(ataid, 0, sizeof(ataid));

	probe_ent = malloc(sizeof(struct ahci_probe_ent));
	memset(probe_ent, 0, sizeof(struct ahci_probe_ent));

	probe_ent->host_flags = ATA_FLAG_SATA
				| ATA_FLAG_NO_LEGACY
				| ATA_FLAG_MMIO
				| ATA_FLAG_PIO_DMA
				| ATA_FLAG_NO_ATAPI;
	probe_ent->pio_mask = 0x1f;
	probe_ent->udma_mask = 0x7f;	/*Fixme,assume to support UDMA6 */

	if(base == 0) // host0
		probe_ent->mmio_base = 0;
	else // host1
		probe_ent->mmio_base = VIXS_SATA_HOST_SEL_MASK;

	/* initialize adapter */
	rc = ahci_host_init(probe_ent);
	if (rc)
		goto err_out;

	ahci_print_info(probe_ent);

	linkmap = probe_ent->link_port_map;

	for (i = 0; i < CONFIG_SYS_SCSI_MAX_SCSI_ID; i++) {
		if (((linkmap >> i) & 0x01)) {
			if (ahci_port_start((u8) i)) {
				printf("Can not start port %d\n", i);
				continue;
			}
#ifdef CONFIG_AHCI_SETFEATURES_XFER
			ahci_set_feature((u8) i);
#endif
		}
	}
err_out:
	return rc;
}
#endif

/*
 * In the general case of generic rotating media it makes sense to have a
 * flush capability. It probably even makes sense in the case of SSDs because
 * one cannot always know for sure what kind of internal cache/flush mechanism
 * is embodied therein. At first it was planned to invoke this after the last
 * write to disk and before rebooting. In practice, knowing, a priori, which
 * is the last write is difficult. Because writing to the disk in u-boot is
 * very rare, this flush command will be invoked after every block write.
 */
static int ata_io_flush(u8 port)
{
	u8 fis[20];
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 cmd_fis_len = 5;	/* five dwords */

	/* Preset the FIS */
	memset(fis, 0, 20);
	fis[0] = 0x27;		 /* Host to device FIS. */
	fis[1] = 1 << 7;	 /* Command FIS. */
	fis[2] = ATA_CMD_FLUSH_EXT;

	memcpy((unsigned char *)pp->cmd_tbl, fis, 20);
	ahci_fill_cmd_slot(pp, cmd_fis_len);
	SATA_writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE,
			WAIT_MS_FLUSH, 0x1)) {
		debug("scsi_ahci: flush command timeout on port %d.\n", port);
		return -EIO;
	}

	return 0;
}


void scsi_bus_reset(void)
{
	/*Not implement*/
}


void scsi_print_error(ccb * pccb)
{
	/*The ahci error info can be read in the ahci driver*/
}
