/******************************************************************************
 * Copyright Codito Technologies (www.codito.com)
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *****************************************************************************/

/*
 *  uboot/board/aa3/arc_gmac.c
 *
 *  Copyright (C) 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * Authors : Sandeep Patil and Pradeep Sawlani.
 */

/* Amber Lin: modified to run Synopsys GMAC on Eagle using Enhanced Descriptor
 *            structure                                          -- April 2009
 */

#include "ethernet.h"	/* MDIO macros and ARC_EMAC register macros	*/
#include <asm/arch/xcodeRegDef.h>
#include <config.h>		/* ARC700 clock freq.	*/
#include <asm/errno.h>
#include <malloc.h>
#include <common.h>
#include <net.h>
#include <linux/types.h>

#ifndef CONFIG_SYS_SDRAM_NON_CACHE_MIRROR
#error Must define CONFIG_SYS_SDRAM_NON_CACHE_MIRROR
#endif

#define MMREG_BASE XC_SOC_PROC_MMREG_BASE
#ifdef CONFIG_SYS_DCACHE_OFF
#define arc_read_uncached_32(addr)		*(volatile unsigned int *)((addr))
#define arc_write_uncached_32(addr, val)		*(volatile unsigned int *)((addr))=(val)
#else
//#define arc_read_uncached_32(addr)		*(volatile unsigned int *)((addr)+CONFIG_SYS_SDRAM_NON_CACHE_MIRROR_OFFSET)
//#define arc_write_uncached_32(addr, val)		*(volatile unsigned int *)((addr)+CONFIG_SYS_SDRAM_NON_CACHE_MIRROR_OFFSET)=(val)
#define arc_read_uncached_32(addr)		*(volatile unsigned int *)((addr))
#define arc_write_uncached_32(addr, val)		*(volatile unsigned int *)((addr))=(val)
#endif

/* The forced speed, 10Mb, 100Mb, gigabit  */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01

/* 1000BASE-T Control register */
#define ADVERTISE_1000FULL      0x0200  /* Advertise 1000BASE-T full duplex */
#define ADVERTISE_1000HALF      0x0100  /* Advertise 1000BASE-T half duplex */
#define LPA_1000FULL            0x0800  /* Link partner 1000BASE-T full duplex */
#define LPA_1000HALF            0x0400  /* Link partner 1000BASE-T half duplex */

/* Link partner ability register. */
#define LPA_SLCT                0x001f  /* Same as advertise selector  */
#define LPA_10HALF              0x0020  /* Can do 10mbps half-duplex   */
#define LPA_1000XFULL           0x0020  /* Can do 1000BASE-X full-duplex */
#define LPA_10FULL              0x0040  /* Can do 10mbps full-duplex   */
#define LPA_1000XHALF           0x0040  /* Can do 1000BASE-X half-duplex */
#define LPA_100HALF             0x0080  /* Can do 100mbps half-duplex  */
#define LPA_1000XPAUSE          0x0080  /* Can do 1000BASE-X pause     */
#define LPA_100FULL             0x0100  /* Can do 100mbps full-duplex  */
#define LPA_1000XPAUSE_ASYM     0x0100  /* Can do 1000BASE-X pause asym*/
#define LPA_100BASE4            0x0200  /* Can do 100mbps 4k packets   */
#define LPA_PAUSE_CAP           0x0400  /* Can pause                   */
#define LPA_PAUSE_ASYM          0x0800  /* Can pause asymetrically     */
#define LPA_RESV                0x1000  /* Unused...                   */
#define LPA_RFAULT              0x2000  /* Link partner faulted        */
#define LPA_LPACK               0x4000  /* Link partner acked us       */
#define LPA_NPAGE               0x8000  /* Next page bit               */

#define LPA_DUPLEX		(LPA_10FULL | LPA_100FULL)
#define LPA_100			(LPA_100FULL | LPA_100HALF | LPA_100BASE4)


#define ENABLE_LOOPBACK 0

#define BUF_MULTIPLE 16	/* data bus width is 128 bits on eagle */
#define RX_BUF_ALIGN(X) (((X) + (BUF_MULTIPLE - 1)) & ~(BUF_MULTIPLE - 1))

#ifdef CONFIG_CMD_NET

#define GMAC_DESC_ALIGN 0x40
#define GMAC_DESC_MASK  (~(GMAC_DESC_ALIGN - 1))

#define GMAC_DATA_ALIGN 0x40
#define GMAC_DATA_MASK  (~(GMAC_DATA_ALIGN - 1))

#define GMAC_DMA_ADDR(addr) (((unsigned int)(addr)) & 0xffffffff )

#define ALIGNTO(x, align)  (((x)+(align)-1) & ~((align)-1))
/* rx buffer descriptor. We align descriptors to 32 so
 * satisfies that the descriptor addresses must be aligned to the bus-width 
 * used */
DmaDesc *rxbd;

/* tx buffer descriptor */
DmaDesc *txbd;

static unsigned int rxbd_org = 0;
static unsigned int txbd_org = 0;

static int initialised = 0 ;
static struct eth_device eth_netdev;
static unsigned long PhyBase = (unsigned long)DefaultPhyBase;
static int act_eth_port = 0;
static int gmii = 0;
static int rgmii = 1;
static int eth_speed = SPEED_1000;
static int eth_duplex = DUPLEX_FULL;

//DmaDesc rxbd[RX_BDT_LEN] __attribute__((aligned(32)));= {{0}};

/* tx buffer descriptor */
//DmaDesc txbd[TX_BDT_LEN] __attribute__((aligned(32)));//= {{0}};

//#define DEBUG

#ifdef DEBUG
#define PDEBUG(fmt, args...) printf(  fmt, ## args)
#else
#define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#define writel(b,addr) (*(volatile unsigned int *) (addr) = (b))
#define readl(addr) (*(volatile unsigned int *) (addr))

static int txbd_cntr = 0; /* transmit buffer counter */
static int rxbd_cntr = 0; /* receive buffer counter  */

/****************************************************/
/* System-dependant register access functions       */
/****************************************************/


static void dump_packet(char *buffer, int len)
{
#if 0
	int i;

	for(i=0;i<len;i++)
	{
		printf("%02x ", buffer[i]);
		if((i+1)%16==0)
			printf("\n");
	}
	printf("\n");
#endif
}

static unsigned long reg_read(unsigned long addr)
{
	unsigned long data;
	unsigned long reg;

	if(0 == act_eth_port) /* eth0 */
	{
		reg = MMREG_BASE + ETH_STATUS_REG;
		do {
			data = readl(reg);
		} while((data & ETH_STATUS_REG_CSR_BUSY_MASK) != 0);

		reg = MMREG_BASE + ETH_CSR_CTRL_REG;
		writel(addr, reg);

		reg = MMREG_BASE + ETH_STATUS_REG;
		do {
			data = readl(reg);
		} while((data  & ETH_STATUS_REG_CSR_BUSY_MASK) != 0);
        
		reg = MMREG_BASE + ETH_RDAT_REG;
		data = readl(reg);
		return data;
	}
	else if(1 == act_eth_port) /* eth1 */
	{
		printf("NIC eth1 is not exist!\n");
		return 0xffffffff;
	}

	return 0;
}

static void reg_write(unsigned long int data, unsigned long int addr)
{
	unsigned long reg, temp;
    
	if(0 == act_eth_port) /* eth0 */
	{
		reg = MMREG_BASE + ETH_STATUS_REG;
	    do {
		    temp = readl(reg);
	    } while( (temp & ETH_STATUS_REG_CSR_BUSY_MASK) != 0);

	    reg = MMREG_BASE + ETH_WDAT_REG;
	    writel(data, reg);

	    reg = MMREG_BASE + ETH_CSR_CTRL_REG;
	    writel(addr | 0x80000000, reg);

	    reg = MMREG_BASE + ETH_STATUS_REG;
	    do {
		   temp = readl(reg);
	    } while( (temp & ETH_STATUS_REG_CSR_BUSY_MASK) != 0);
	}
	else if(1 == act_eth_port) /* eth1 */
	{
		printf("NIC eth1 is not exist!\n");
	}
}

static unsigned long read_mac_reg(unsigned long reg)
{
	unsigned long data = reg_read(GmacRegisterBase + reg);
	return data;
}

static void write_mac_reg(unsigned long reg, unsigned long data)
{
	reg_write(data,GmacRegisterBase + reg);
}

static void set_mac_reg(unsigned long reg, unsigned long data)
{
	unsigned long addr = GmacRegisterBase + reg;
	unsigned long data_t = reg_read(addr);
	data_t |= data;
	reg_write(data_t, addr);
}

static void clear_mac_reg(unsigned long reg, unsigned long data)
{
	unsigned long addr = GmacRegisterBase + reg;
	unsigned long data_t = reg_read(addr);
	data_t &= ~data;
	reg_write(data_t, reg);
}

static unsigned long read_dma_reg(unsigned long reg)
{
	unsigned long data = reg_read(DmaRegisterBase + reg);
	return data;
}

static void write_dma_reg(unsigned long reg, unsigned long data)
{
	reg_write(data, DmaRegisterBase + reg);
}

static void set_dma_reg(unsigned long reg, unsigned long data)
{
	unsigned long addr = DmaRegisterBase + reg;
	unsigned long data_t = reg_read(addr);
	data_t |= data;
	reg_write(data_t, addr);
}

static void clear_dma_reg(unsigned long reg, unsigned long data)
{
	unsigned long addr = DmaRegisterBase + reg;
	unsigned long data_t = reg_read(addr);
	data_t &= ~data;
	reg_write(data_t, reg);
}

/***********************/
/* MAC module function */
/***********************/

static unsigned long read_mii(unsigned long reg)
{
	unsigned long addr;
	unsigned long data;
	addr = ((PhyBase << GmiiDevShift) & GmiiDevMask) | ((reg << GmiiRegShift) & GmiiRegMask) | GmiiRead;
	write_mac_reg(GmacGmiiAddr, (addr | GmiiAppClk5 | GmiiBusy));
	while( (read_mac_reg(GmacGmiiAddr) & GmiiBusy) == GmiiBusy);
	data = read_mac_reg(GmacGmiiData) & 0xFFFF;
	return data;
}

static void write_mii(unsigned long reg, unsigned long data)
{
	unsigned long addr;
	write_mac_reg(GmacGmiiData, data);
	addr = ((PhyBase << GmiiDevShift) & GmiiDevMask) | ((reg << GmiiRegShift) & GmiiRegMask) | GmiiWrite;
	write_mac_reg(GmacGmiiAddr, (addr | GmiiAppClk5 | GmiiBusy));
	while( (read_mac_reg(GmacGmiiAddr) & GmiiBusy) == GmiiBusy);
}

static void gmac_set_address(void *ptr)
{
	char *mac_addr = (char *)ptr;
	unsigned long int data;

	data = (mac_addr[5] << 8) | mac_addr[4];
	write_mac_reg(GmacAddr0High, data);
	data = (mac_addr[3] << 24) | (mac_addr[2] << 16) | (mac_addr[1] << 8) | mac_addr[0];
	write_mac_reg(GmacAddr0Low, data);
}

static void gmac_get_address(void *ptr)
{
	char *mac_addr = (char *)ptr;
	unsigned long int data_lo, data_hi;

	data_lo=read_mac_reg(GmacAddr0Low);
	data_hi=read_mac_reg(GmacAddr0High);

	mac_addr[5]=(data_hi>>8) & 0xff;
	mac_addr[4]=(data_hi & 0xff);
	mac_addr[3]=(data_lo>>24) & 0xff;
	mac_addr[2]=(data_lo>>16) & 0xff;
	mac_addr[1]=(data_lo>>8) & 0xff;
	mac_addr[0]=(data_lo & 0xff);
}


/*********************************************************/
/* reset ethernet block                                  */
/********************************************************/
static int is_mac_to_mac()
{
	ulong board_id =  readl(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1);
	switch(board_id) {
		default:
			return 0;
	}
}

static void eth_reset(void)
{
	unsigned long data;

	data = readl(MMREG_BASE + ETH_CTRL_REG);
	data |= ETH_CTRL_REG_GTXCLK_SOURCE_MASK;
	writel(data, MMREG_BASE + ETH_CTRL_REG);

	data = readl(MMREG_BASE + CG1_CLK_SRC_SEL4);
	data &= ~(ETHR0_TXCLK_SRC_SEL_MASK);
	writel(data, MMREG_BASE + CG1_CLK_SRC_SEL4);

	mdelay(20);

#if 0 /* turn the urgent bit off for bring up */	
    data = readl(MMREG_BASE + MC_CH0_ARB_MAIN_CTRL);
    data |= 0x4000;
    writel(data, MMREG_BASE + MC_CH0_ARB_MAIN_CTRL);
    data = readl(MMREG_BASE + MC_CH1_ARB_MAIN_CTRL);
    data |= 0x4000;
    writel(data, MMREG_BASE + MC_CH1_ARB_MAIN_CTRL);
#endif

	mdelay(20);
	data = readl(MMREG_BASE + AON_ETH0_CFG);
	data |= AON_ETH0_CFG_INTF_SEL_MASK;
	writel(data, MMREG_BASE + AON_ETH0_CFG);

	data = readl(MMREG_BASE + GPIO_B_CTRL);
	data = (data & (~GPIO_B_CTRL_GPIO_MODE_SEL_MASK));
	writel(data, MMREG_BASE + GPIO_B_CTRL);

#if 1
	// reset ethernet0
	// CG_RESET_REG\ETH0_RESET   = 0x1
	// CG_RESET_REG\ETH0_RESET   = 0x0
	data = readl(MMREG_BASE + ACC_RESET_REG0);
	data = data | ACC_RESET_REG0_ETH0_RESET_MASK;
	writel(data, MMREG_BASE + ACC_RESET_REG0);

	data = readl(MMREG_BASE + ACC_RESET_REG0);
	data = data & (~ACC_RESET_REG0_ETH0_RESET_MASK);
	writel(data, MMREG_BASE + ACC_RESET_REG0);

	if(0==gmii)
	{
		data = ETH_CTRL_REG_RESETN_MASK;
		writel(data, MMREG_BASE + ETH_CTRL_REG);
	}
	else
	{
		data = ETH_CTRL_REG_RESETN_MASK;
		writel(data, MMREG_BASE + ETH_CTRL_REG);
	}
#endif
	if(rgmii)
	{
		data = readl(MMREG_BASE + ETH_CTRL_REG);
		data &= ~ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK;
		writel(data, MMREG_BASE + ETH_CTRL_REG);
	
		data = readl(MMREG_BASE + ETH_CTRL_REG);
		data |= ETH_CTRL_REG_RGMII_FIFO_RSTN_MASK | ETH_CTRL_REG_INTF_SEL_MASK | 
				ETH_CTRL_REG_CLK_SHIFT_EN_MASK | ETH_CTRL_REG_GTXCLK_SOURCE_MASK;
		writel(data, MMREG_BASE + ETH_CTRL_REG);
	}
	else
	{
		data = readl(MMREG_BASE + ETH_CTRL_REG);
		data |= ETH_CTRL_REG_CLK_SHIFT_EN_MASK | ETH_CTRL_REG_GTXCLK_SOURCE_MASK;
		writel(data, MMREG_BASE + ETH_CTRL_REG);
	}

	data = readl(MMREG_BASE + ETH_CTRL_REG);
	data |= ETH_CTRL_REG_RX_DESC_CACHE_EN_MASK | ETH_CTRL_REG_TX_DESC_CACHE_EN_MASK;
	writel(data, MMREG_BASE + ETH_CTRL_REG);
}

static int DescDma( DmaDesc Desc )
{
	return (Desc.status & DescOwnByDma) != 0;   /* Owned by DMA */
}

static int DmaRxValid( unsigned int Status )
{
	/* no errors, and whole frame is in the buffer */
	return ( (Status & DescError) == 0 )
		&& ( (Status & DescRxFirst) != 0 )
		&& ( (Status & DescRxLast) != 0 );
}

static int gmac_eth_recv(struct eth_device *netdev)
{
	unsigned int *recv_data, len;
	unsigned long status;
	int j;
	uchar *p;

	PDEBUG("gmac_eth_recv\n");
	j=0;
	do {
		status = read_dma_reg(DmaStatus);
		j++;
		if (status & DmaIntRxStopped)
			write_dma_reg(DmaRxPollDemand, 0);
	} while(!(status  & DmaIntRxCompleted)  && j<100);

	write_dma_reg(DmaStatus, status);

	invalidate_dcache_range((unsigned long)&rxbd[rxbd_cntr], (unsigned long)&rxbd[rxbd_cntr]+sizeof(DmaDesc));
	status = arc_read_uncached_32(&(rxbd[rxbd_cntr].status));
	while(!(status & DescOwnByDma))
	{
		if(DmaRxValid(status))
		{
			len = ((status & DescFrameLengthMask) >> DescFrameLengthShift) - 4; /* ignore ethernet crc */
			recv_data = (unsigned int *)(arc_read_uncached_32(&rxbd[rxbd_cntr].tag1));
#ifndef CONFIG_SYS_DCACHE_OFF
			if(dcache_status()==1)
				invalidate_dcache_range((unsigned long)recv_data, (unsigned long)recv_data+ALIGNTO(len, CONFIG_SYS_CACHELINE_SIZE));
#endif
			p = (uchar *)recv_data;
			PDEBUG("recv addr:0x%x,len:%d\n",p,len);
	
			NetReceive((uchar *)recv_data, len);
			dump_packet(recv_data, len);
			
		}
		else
			printf("DmaRx invalid status = %x\n", status);

		arc_write_uncached_32(&(rxbd[rxbd_cntr].status), DescOwnByDma);
		flush_dcache_range((unsigned long)&rxbd[rxbd_cntr], (unsigned long)&rxbd[rxbd_cntr]+sizeof(DmaDesc));

		rxbd_cntr = (rxbd_cntr + 1) % RX_BDT_LEN;
		invalidate_dcache_range((unsigned long)&rxbd[rxbd_cntr], (unsigned long)&rxbd[rxbd_cntr]+sizeof(DmaDesc));
		status = arc_read_uncached_32(&(rxbd[rxbd_cntr].status));
	}

	PDEBUG("gmac_eth_recv return\n");
	return 0;
}

static int gmac_eth_send(struct eth_device *netdev, volatile void *packet, int length)
{
	unsigned long  status;
	unsigned int i;
	
	PDEBUG("gmac_eth_send\n");
	PDEBUG("gmac_eth_send: addr 0x%x,length:%d\n", packet,length);
	dump_packet(packet, length);

	if(length > ETH_MTU)
	{
		printf("gmac_eth_send: packet length %d greater than MTU\n", length);
		return -1;
	}
	if(length < 64)
		length = 64;

	/* set up the tx descriptor */
	invalidate_dcache_range((unsigned long)&txbd[txbd_cntr], (unsigned long)&txbd[txbd_cntr]+sizeof(DmaDesc));
	if((arc_read_uncached_32(&(txbd[txbd_cntr].status)) & DescOwnByDma) == 0) //owned by the host
	{
		arc_write_uncached_32(&(txbd[txbd_cntr].buffer1), GMAC_DMA_ADDR(packet));
	
		arc_write_uncached_32(&(txbd[txbd_cntr].length), (length << DescSize1Shift) & DescSize1Mask);
#ifndef CONFIG_SYS_DCACHE_OFF
		if(dcache_status()==1)
			flush_dcache_range((unsigned long)packet, (unsigned long)packet+length);
#endif

		status = arc_read_uncached_32(&txbd[txbd_cntr].status);
		status &= TxDescEndOfRing; //clear the descriptor
		status = status | TxDescChain | DescOwnByDma | DescTxFirst | DescTxLast | DescTxIntEnable;
		arc_write_uncached_32(&(txbd[txbd_cntr].status), status);
		flush_dcache_range((unsigned long)&txbd[txbd_cntr], (unsigned long)&txbd[txbd_cntr]+sizeof(DmaDesc));
	} 
	else
	{
		printf("Out of Tx Buffers\n");
		return -ENOMEM;
	}
	
	do {__asm__ __volatile__ ("dsb" : : : "memory");} while(0);

	/* issue a Poll Demand command to wake up TxDMA if it's in suspend mode */
	write_dma_reg(DmaTxPollDemand, 0);

	i=0;
	do {
		status = read_dma_reg(DmaStatus);
		i++;
	}
	while( !( status & DmaIntTxCompleted)  && i<100);

	if(i>=100)
	{
		printf("tx time out status = %x\n", status);
	}

	write_dma_reg(DmaStatus, status);
	arc_write_uncached_32(&(txbd[txbd_cntr].status), (txbd_cntr == TX_BDT_LEN-1) ? TxDescEndOfRing : 0 | TxDescChain);
	arc_write_uncached_32(&(txbd[txbd_cntr].buffer1), 0);
	flush_dcache_range((unsigned long)&txbd[txbd_cntr], (unsigned long)&txbd[txbd_cntr]+sizeof(DmaDesc));

	txbd_cntr = (txbd_cntr + 1) % TX_BDT_LEN;	
	PDEBUG("gmac_eth_send return\n");

	return 0;
}

static int DmaInit(void)
{
	unsigned long data;

	/* 1. Set Host bus access parameters */
	data = read_dma_reg(DmaBusMode);
	if(rgmii)
	{
		write_dma_reg(DmaBusMode, 0x1080a);
	}
	else
	{
		data |= DmaBurstLength8;
		write_dma_reg(DmaBusMode, data);
	}

	/* 2. Mask uncessary interrupt causes: disable all as interrupt
	      mechanism is not implemented in u-boot */
	write_dma_reg(DmaInterrupt, 0);
	write_dma_reg(DmaStatus, read_dma_reg(DmaStatus));

	/* 3. Create Tx/Rx descriptor lists and write to DMA Reg 3 & Reg 4 
	      (descriptor lists have been created in eth_init) */

    debug("rxbd=0x%08x, GMAC_DMA_ADDR(rxbd)=0x%08x\n", rxbd, GMAC_DMA_ADDR(rxbd));
    debug("txbd=0x%08x, GMAC_DMA_ADDR(txbd)=0x%08x\n", txbd, GMAC_DMA_ADDR(txbd));
     
	write_dma_reg(DmaRxBaseAddr, GMAC_DMA_ADDR(rxbd));//rx buf desc ptr
	write_dma_reg(DmaTxBaseAddr, GMAC_DMA_ADDR(txbd));//tx buf desc ptr

//	printf(" rxbd = %x   txbd = %x\n", rxbd, txbd);

	return 0;
}

static int MacInit(void)
{
	unsigned long data;	


	write_mac_reg(GmacFrameFilter, GmacFilterOff);

	/* full duplex rx & tx eable disable receive own */
	data = read_mac_reg(GmacConfig);
	data |=  GmacTxEnable | GmacRxEnable | GmacDisableRxOwn;
	if(SPEED_1000 == eth_speed)
		data &= ~GmacSelectMii;
	else
		data |= GmacSelectMii;

	if(DUPLEX_FULL == eth_duplex) 
		data |= GmacFullDuplex;
	else
		data &= ~GmacFullDuplex;
	write_mac_reg(GmacConfig, data);

	data = read_mac_reg(GmacConfig);
	return 0;
}

static void MacPauseControl(void)
{
	u32 omr_reg;
	u32 mac_flow_control_reg;
	
	omr_reg = read_dma_reg(DmaControl);	
	omr_reg |= DmaRxFlowCtrlAct4K | DmaRxFlowCtrlDeact5K |DmaEnHwFlowCtrl;	
	write_dma_reg(DmaControl, omr_reg);	
	
	mac_flow_control_reg = read_mac_reg(GmacFlowControl);
	mac_flow_control_reg |= GmacRxFlowControl | GmacTxFlowControl | 0xFFFF0000;
	write_mac_reg(GmacFlowControl,mac_flow_control_reg);	
	return;
}

static int gmac_eth_init(struct eth_device *netdev, bd_t *bd);
static void gmac_eth_halt(void)
{
	unsigned long data;
//	gmac_eth_init(NULL,NULL);

/*
	data = readl(MMREG_BASE + ACC_RESET_REG0);
	data = data | ACC_RESET_REG0_ETH0_RESET_MASK;
	writel(data, MMREG_BASE + ACC_RESET_REG0);
*/
	initialised=0;

}

static void vixs_eth_fix_clk_source(u32 speed)
{
    switch(speed)
    {

        case SPEED_1000:
        {
            u32 i;

            i=readl(MMREG_BASE + CG1_CLK_SRC_SEL4);
            if(act_eth_port==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
            }
            else
            {
				printf("NIC eth1 is not exist!\n");
            }
            writel(i, MMREG_BASE + CG1_CLK_SRC_SEL4);
            break;
        }
        case SPEED_100:
        {
            u32 i;

            i=readl(MMREG_BASE + CG1_CLK_SRC_SEL4);
            if(act_eth_port==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
                i|=(1<<CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_SHIFT);
            }
            else
            {
				printf("NIC eth1 is not exist!\n");
            }
            writel(i, MMREG_BASE + CG1_CLK_SRC_SEL4);
            break;
        }
        case SPEED_10:
        {
            u32 i;

            i=readl(MMREG_BASE + CG1_CLK_SRC_SEL4);
            if(act_eth_port==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
                i|=(2<<CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_SHIFT);
            }
            else
            {
				printf("NIC eth1 is not exist!\n");
            }
            writel(i, MMREG_BASE + CG1_CLK_SRC_SEL4);
            break;
        }
        default:
            printf("Unknown speed\n");
            break;
    }
}

void gmac_reset(void)
{
	write_dma_reg(DmaBusMode, DmaResetOn);
	mdelay(500);
//	printf("Data after reset %x\n", read_dma_reg(DmaBusMode));
}

void gmac_set_clk_div(u32 clk)
{
	u32 data;

	data = read_mac_reg(GmacGmiiAddr);
	data &=~0x1c;
	data |= clk;
	write_mac_reg(GmacGmiiAddr, data);
}

void gmac_select_mii(void)
{
	u32 data;

	data = read_mac_reg(GmacConfig);
	data |=GmacSelectGmii;
	write_mac_reg(GmacConfig, data);
	
}


static int reset_eth_phy(struct eth_device *netdev)
{

	int i;
	unsigned long temp;

	/*
	 * MAC to MAC connection
	 * hard code to Giga bits full duplex
	 */
	if (is_mac_to_mac())
	{
		eth_speed = SPEED_1000;
		eth_duplex = DUPLEX_FULL;
		printf("Skip ETH PHY Init.\n");
		goto probe_end;
	}
	
	PhyBase = 0;
	while(PhyBase < 32)
	{			
		temp = (read_mii(0x02) << 16)|(read_mii(0x03));
		if (temp != 0xffffffff)
			break;
		PhyBase++;
	}
	
	if (PhyBase == 32)
	{
		printf("Non Valid PHY Index Base detected\n");
		return -1;
	} 
	else 
	{
		printf("Detected Ethernet PHY Index Base %d, PHY id: %x\n",(int)PhyBase, temp);
	}

	if(temp==0x221555)
	{
		gmac_reset();
		gmac_set_clk_div(GmiiAppClk5);

		temp = read_mii(GEN_ctl);
//		write_mii(GEN_ctl, temp | 0x8000);	// reset the PHY to restart auto-negotiation	

		printf("GEN_ctl = %x\n", read_mii(GEN_ctl));
		write_mii(0xd, 0x001c);
		write_mii(0xe, 0x000d);
		write_mii(0xd, 0x401c);
		write_mii(0xe, 0x0808);
		printf("MII 0x1f = %x\n", read_mii(0x1f));
		write_mii(0x1f, 0xa100);
		printf("MII AN_adv = %x\n", read_mii(AN_adv));
		write_mii(AN_adv, 0x05e1);
		write_mii(0x1b, 0x0500);
		printf("GEN_ctl = %x\n", read_mii(GEN_ctl));
		printf("MII 0x1b = %x\n", read_mii(0x1b));
		udelay(500);
		write_mii(GEN_ctl, 0x2000);
		udelay(500);
		write_mii(GEN_ctl, 0x3000);
		
	}
	else
	{
		/* 
 		 * For Marvell 88E3016:
 		 * Need delay the clock sampling when data is stable
 		 * Ref. datasheet PHY specific reg II(28) on page 74.
 		 */
		if ((temp & 0xffffff0) == 0x1410e20) {
			temp = read_mii(28);
			temp |= 0x0400;
			write_mii(28, temp);
			udelay(100);
		}
		gmac_select_mii();
		temp = read_mii(Giga_ctrl);
		if(0 == gmii)
		{
			temp &= ~(ADVERTISE_1000FULL|ADVERTISE_1000HALF);
		}
		else
		{
			temp |= (ADVERTISE_1000FULL|ADVERTISE_1000HALF);
		}

		write_mii(Giga_ctrl, temp);	// diable 1000 Base-T

		gmac_reset();

		gmac_set_clk_div(GmiiAppClk5);


		temp = read_mii(GEN_ctl);
		write_mii(GEN_ctl, temp | 0x8000);	// reset the PHY to restart auto-negotiation	
	}

	i = 0;
	while(((read_mii(GEN_sts) & AUTOCMPLT) != AUTOCMPLT)&&(i < 50000))
	{
		udelay(100);
		i++;
	}

	i = 0;
	while(((read_mii(GEN_sts) & LINK) == 0)&&(i < 50000))
	{
		udelay(100);
		i++;
	}

	if( (read_mii(GEN_sts) & LINK) == 0)
	{
		printf("No link\n");
		return -1;
	}
	else
	{
		printf("Linked up:");
		if(1==gmii)
		{
			temp = read_mii(Giga_stat);
			if(temp & LPA_1000FULL)
			{
				eth_speed = SPEED_1000;
				eth_duplex = DUPLEX_FULL;
				goto probe_end;
			}
			else if(temp & LPA_1000HALF)
			{
				eth_speed = SPEED_1000;
				eth_duplex = DUPLEX_HALF;
				goto probe_end;
			}
	
		}
	
		temp = read_mii(AN_lpa);
		if((temp & LPA_100FULL)||(temp & LPA_100HALF))
			eth_speed = SPEED_100;
		else
			eth_speed = SPEED_10;

		if((temp & LPA_100FULL)||(temp & LPA_10FULL))
			eth_duplex = DUPLEX_FULL;
		else
			eth_duplex = DUPLEX_HALF;

		
				
	probe_end:
		vixs_eth_fix_clk_source(eth_speed);

		printf("%s,%s\n",(eth_speed == SPEED_1000)?"1000M":(eth_speed == SPEED_100)?"100M":"10M",
			(eth_duplex == DUPLEX_FULL)? "duplex":"half duplex");

	}


	
#if(ENABLE_LOOPBACK ==1)
	temp = read_mii(GEN_ctl);
	write_mii(GEN_ctl, temp | LOOPBACK);
#endif

	return 0;
}

void enable_clk(void)
{
	u32 data;

	debug("Enable ethernet clk\n");
	data = readl(MMREG_BASE + CG1_CLK_STOP0);
	data &= ~(CG1_CLK_STOP0_ETHR0_REFCLK_STOP_MASK | CG1_CLK_STOP0_ETHR0_TXCLK_STOP_MASK);
	writel(data, (MMREG_BASE + CG1_CLK_STOP0));

	data = readl(MMREG_BASE + ACC_BLK_STOP0);
	data &= ~(ACC_BLK_STOP0_ETH0_BLK_STOP_MASK);
	writel(data, MMREG_BASE + ACC_BLK_STOP0);
}

/* Referring to Synopsys Databook v3.42a 3.4.1 Initialization */
static int gmac_eth_init(struct eth_device *netdev, bd_t *bd)
{
	int i, size;
	int skip_mem_allocation=0;
	

	if(initialised)
		goto out;

	txbd_cntr=rxbd_cntr=0;

	enable_clk();
	/* Reset ethernet block */
	eth_reset();

	gmac_reset();
	gmac_set_clk_div(GmiiAppClk5);

	if((rxbd_org==0) || (txbd_org==0))
	{
		rxbd_org = (unsigned int)malloc( RX_BDT_LEN * sizeof(DmaDesc) + GMAC_DESC_ALIGN);
		txbd_org = (unsigned int)malloc( TX_BDT_LEN * sizeof(DmaDesc) + GMAC_DESC_ALIGN);


		if((NULL == rxbd_org) || (NULL == txbd_org))
		{
			printf("Ethernet driver memory allocation failure\n");
			return -1;
		}
	}
	else
		skip_mem_allocation=1;

	rxbd = (DmaDesc *)((rxbd_org + GMAC_DESC_ALIGN) & (~(GMAC_DESC_ALIGN - 1)));
	txbd = (DmaDesc *)((txbd_org + GMAC_DESC_ALIGN) & (~(GMAC_DESC_ALIGN - 1)));


//	printf("rxbd_org = 0x%x,rxbd = 0x%x\n", rxbd_org,rxbd);
//	printf("txbd_org = 0x%x,txbd = 0x%x\n", txbd_org,txbd);
	
	/* Reset the PHY */
	if(reset_eth_phy(netdev) < 0)
	{
		printf("Reset PHY failure\n");
		return -1;
	}

	/* Create Rx descriptor list : enhanced descriptor structure is
	   implemented on eagle */
	for(i=0; i<RX_BDT_LEN; i++)
	{
		/* RDES0 */
		arc_write_uncached_32(&(rxbd[i].status), DescOwnByDma);

		/* RDES1 */
		size = RX_BUF_ALIGN(ETH_MTU + GMAC_BUFFER_PAD);
		arc_write_uncached_32(&(rxbd[i].length), size | RxDescChain);

		if(!skip_mem_allocation)
		{
			char *p;

			/* RDES2 */
			p = (unsigned char *)malloc(size + GMAC_DATA_ALIGN);
			if(NULL == p)
			{
				printf("Allocate Ethernet RX right failure\n");
				return -1;
			}
			
			arc_write_uncached_32(&(rxbd[i].tag2), p);
	//		printf("tag 2 malloc addr = %x\n", p);
	
			p = (char *)((unsigned int)(p + 31) & (~31));
			arc_write_uncached_32(&(rxbd[i].tag1), p);
	//		printf("-------->tag1 malloc addr = %x\n", p);
			
			p = (char *)(((unsigned int)p) & 0x7fffffff);
			arc_write_uncached_32(&(rxbd[i].buffer1), (unsigned long *)p);
		}
		/*if (arc_read_uncached_32(&(rxbd[i].buffer1)) == 0 )
		{
			printf("no memory for receive descriptor i = %d\n", i);
		}*/

		/* RDES3 */
		if(i != (RX_BDT_LEN - 1))
			arc_write_uncached_32(&rxbd[i].buffer2, GMAC_DMA_ADDR(&rxbd[i+1]));
	}
	
	arc_write_uncached_32(&(rxbd[RX_BDT_LEN-1].length), arc_read_uncached_32(&rxbd[RX_BDT_LEN-1].length) | RxDescEndOfRing);
	arc_write_uncached_32(&rxbd[RX_BDT_LEN-1].buffer2, GMAC_DMA_ADDR(&rxbd[0]));

	flush_dcache_range((unsigned long)rxbd, (unsigned long)rxbd+sizeof(DmaDesc)*RX_BDT_LEN);

	/* Create Tx descriptor list with enhanced descriptor structure */
	/* All TX BD's owned by CPU */
	for( i = 0; i<TX_BDT_LEN; i++)
	{
		/* TDES0 */
		arc_write_uncached_32(&(txbd[i].status), TxDescChain);

		/* TDES1 */
		arc_write_uncached_32(&(txbd[i].length), 0);

		/* TDES2 */
		arc_write_uncached_32(&(txbd[i].buffer1), 0);

		/* TDES3 */
		if( i != (TX_BDT_LEN) )
			arc_write_uncached_32(&txbd[i].buffer2, GMAC_DMA_ADDR(&txbd[i + 1]));
		
	}
	arc_write_uncached_32(&(txbd[TX_BDT_LEN-1].status), TxDescChain | TxDescEndOfRing);
	arc_write_uncached_32(&txbd[TX_BDT_LEN-1].buffer2, GMAC_DMA_ADDR(&txbd[0]));

	flush_dcache_range((unsigned long)txbd, (unsigned long)txbd+sizeof(DmaDesc)*TX_BDT_LEN);
	//printf(" rxbd = %x   txbd = %x %x\n", rxbd, txbd, netdev);

	/* set GMAC hardware address */
	if(getenv("ethaddr")!=NULL)
	{
		unsigned char mac[6];
		eth_getenv_enetaddr("ethaddr", mac);
		printf("MAC: Set to %02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0],
			mac[1],
			mac[2],
			mac[3],
			mac[4],
			mac[5]);
		gmac_set_address(mac);
	}
	else
	{
		unsigned char hwmac[6];

		gmac_get_address(hwmac);
		printf("HW MAC: Set to %02x:%02x:%02x:%02x:%02x:%02x\n",
			hwmac[0],
			hwmac[1],
			hwmac[2],
			hwmac[3],
			hwmac[4],
			hwmac[5]);
	}

	/* Step 1~3 */
	DmaInit();

	/* Step 4~5 */
	MacInit();
	MacPauseControl();
	/* Clear Interrupt Status */
	write_dma_reg(DmaStatus, read_dma_reg(DmaStatus));
	mdelay(500);

	initialised = 1;

out:
	/* Step 6 */
	write_dma_reg(DmaControl, DmaRxStart | DmaTxStart);

	return (0);
}

int miiread(const char *devname, unsigned char addr,
                   unsigned char reg, unsigned short *value)
{
	*value=read_mii(reg);
	return 0;
}
int miiwrite(const char *devname, unsigned char addr,
                    unsigned char reg, unsigned short value)
{
	write_mii(reg, value);
	return 0;
}

int board_eth_init(bd_t *bd)
{
	char *tmp;
	
	struct eth_device *dev = &eth_netdev;

	tmp = getenv("ethact");
	if(tmp)
	{
		if(strcmp(tmp, "eth0") == 0)
		{
			act_eth_port=0;
			strcpy(dev->name,"eth0");
		}
		else if(strcmp(tmp, "eth1") == 0)
		{
			act_eth_port=1;
			strcpy(dev->name,"eth1");
		}
	}
	else
	{
		act_eth_port=0;
		strcpy(dev->name,"eth0");
	}

	tmp = getenv("gmii");
	if(tmp && (strcmp(tmp,"yes") == 0))
	{
		gmii=1;
		printf("1000M/100M/10M Ethernet is used.\n");
	}
	else
	{
		gmii=0;
		printf("100M/10M Ethernet is used.\n");
	}
	
	
	tmp = getenv("rgmii");
	if(!tmp || (strcmp(tmp,"yes") == 0))
	{
		rgmii=1;
		eth_speed = SPEED_1000;
		printf("RGMII PHY is used.\n");
	}
	else
	{
		rgmii=0;
		eth_speed = SPEED_100;
		printf("MII PHY is used.\n");
	}

	dev->init = gmac_eth_init;
	dev->send = gmac_eth_send;
	dev->recv = gmac_eth_recv;
	dev->halt = gmac_eth_halt;

	eth_register(dev);	
	miiphy_register("eth0", miiread, miiwrite);

	return 0;
}

void dump_eth(void)
{
	int i;

	gmac_eth_init(NULL, NULL);

	printf("gmac:\n");
	for(i=0;i<=0xd8;i+=4)
		printf("Register %04x = 0x%08x\n", i, read_mac_reg(i));
	for(i=0x100;i<=0x1fc;i+=4)
		printf("Register %04x = 0x%08x\n", i, read_mac_reg(i));

	printf("\ndma:\n");
	for(i=0;i<=0x54;i+=4)
		printf("Register %04x = 0x%08x\n", i, read_dma_reg(i));

	printf("\nmii:\n");
	for(i=0;i<=0x1c;i+=1)
		printf("Register %04x = 0x%08x\n", i, read_mii(i));
}
#endif /* (CONFIG_COMMANDS & CFG_CMD_NET) */

