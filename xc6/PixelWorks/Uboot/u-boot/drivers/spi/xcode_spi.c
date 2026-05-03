/*
 * (C) Copyright 2002
 * Gerald Van Baren, Custom IDEAS, vanbaren@cideas.com.
 *
 * Influenced by code from:
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
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
 */

#include <common.h>
#include <spi.h>

#include <malloc.h>
#include <asm/arch/xcodeRegDef.h>
#include <asm/io.h>

/*-----------------------------------------------------------------------
 * Definitions
 */

//#define DEBUG_SPI
#ifdef DEBUG_SPI
#define PRINTD(fmt,args...)	printf (fmt ,##args)
#else
#define PRINTD(fmt,args...)
#endif

//#define SPI_AUTO_DMA_MODE

#define reverse_endian(x) (x = (((x&0xff)<<24)|((x&0xff00)<<8)|((x&0xff0000)>>8)|(x&0xff000000)>>24))

struct xcode_spi_slave {
	struct spi_slave slave;
	unsigned int mode;
};

static inline struct xcode_spi_slave *to_xcode_spi(struct spi_slave *slave)
{
	return container_of(slave, struct xcode_spi_slave, slave);
}

/*=====================================================================*/
/*                         Public Functions                            */
/*=====================================================================*/

/*-----------------------------------------------------------------------
 * Initialization
 */
void spi_init (void)
{
	unsigned int temp;
	unsigned int board_id;
	
 	board_id = readl(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1);

	/* wait for P9 to clear the interlock */
	while(readl(XC_SOC_PROC_MMREG_BASE + MIPS_INTERLOCK0) != 0);

	temp  = readl(XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);
	temp &= ~ACC_BLK_STOP0_NRFC_BLK_STOP_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);

	temp = readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
	temp |= ACC_RESET_REG0_NRFC_RESET_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
	udelay(10);
	temp &= ~ACC_RESET_REG0_NRFC_RESET_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);

	temp = readl(XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);
	temp &= ~RBM_PADU_CTRL_PADU_CTRL_SPI0_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);

	temp = (0x08 << SPI_PULSE_WIDTH_CONFIG_SPI_CLK_HI_WIDTH_SHIFT) | \
		(0x08 << SPI_PULSE_WIDTH_CONFIG_SPI_CLK_LOW_WIDTH_SHIFT);
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_PULSE_WIDTH_CONFIG);

#ifdef SPI_AUTO_DMA_MODE	
	/* clear the interrupt and disable the interrupt */
	temp = SPI_FIFO_INT_STATUS_SPI_DMA_DONE_MASK | SPI_FIFO_INT_STATUS_SPI_DONE_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_FIFO_INT_STATUS);
	writel(0x0, XC_SOC_PROC_MMREG_BASE + SPI_FIFO_MIPS_INT_MASK);
	writel(0x0, XC_SOC_PROC_MMREG_BASE + SPI_FIFO_HOST_INT_MASK);
#endif
	
	temp = SPI_CFG_SPI_RESETN_MASK;
	temp |= (0x0 << SPI_CFG_CAP_EDGE_SHIFT) | (0x2 << SPI_CFG_SHFT_EDGE_SHIFT);	
#ifdef SPI_AUTO_DMA_MODE
	temp |= (0x1 << SPI_CFG_DMA_SEL_SHIFT) | (0x1 << SPI_CFG_AUTO_DMA_SHIFT);
#endif	
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_CFG);

	return;
}

struct spi_slave *spi_setup_slave(unsigned int bus, unsigned int cs,
		unsigned int max_hz, unsigned int mode)
{
	struct xcode_spi_slave *xs;

	xs = malloc(sizeof(struct xcode_spi_slave));
    
	if (!xs)
		return NULL;

    memset((void*)xs, 0, sizeof(struct xcode_spi_slave));
    
	xs->slave.bus = bus;
	xs->slave.cs = cs;
	xs->mode = mode;

	/* TODO: Use max_hz to limit the SCK rate */
	return &xs->slave;
}

void spi_free_slave(struct spi_slave *slave)
{
	struct xcode_spi_slave *xs = to_xcode_spi(slave);
    if(xs)
	    free(xs);
    else
        printf("Invalid slave\n");
}

int spi_claim_bus(struct spi_slave *slave)
{
	struct xcode_spi_slave *xs = to_xcode_spi(slave);
	unsigned int temp;

    udelay(10);
	return 0;
}

void spi_release_bus(struct spi_slave *slave)
{
	/* Nothing to do */
}

#ifdef SPI_AUTO_DMA_MODE
/* Single bit SPI register mode data transfer */
static void spi_auto_dma_shift(unsigned int cmd, unsigned int offset, unsigned int xfer_size, unsigned int hold_en, unsigned char *buf, unsigned char spi_addr_size_3byte)
{
	unsigned int temp;

	temp = readl(XC_SOC_PROC_MMREG_BASE + SPI_CFG);
	temp &= ~SPI_CFG_XFER_SIZE_MASK;
	temp |= 3 << SPI_CFG_XFER_SIZE_SHIFT;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_CFG);

	writel((unsigned long)buf, XC_SOC_PROC_MMREG_BASE + SPI_DMA_ADDRESS);
	temp = readl(XC_SOC_PROC_MMREG_BASE + SPI_DMA_SIZE_CONFIG);
	temp &= ~SPI_DMA_SIZE_CONFIG_SPI_DMA_XFER_SIZE_MASK;
	temp |= xfer_size << SPI_DMA_SIZE_CONFIG_SPI_DMA_XFER_SIZE_SHIFT;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_DMA_SIZE_CONFIG);

	temp = readl(XC_SOC_PROC_MMREG_BASE + SPI_DMA_CONFIG);
	temp &= ~SPI_DMA_CONFIG_SPI_DMA_WE_REN_MASK;
	temp |= 1 << SPI_DMA_CONFIG_SPI_DMA_WE_REN_SHIFT;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_DMA_CONFIG);

	/* clear interrupt for SPI */
	temp = SPI_FIFO_INT_STATUS_SPI_DMA_DONE_MASK | SPI_FIFO_INT_STATUS_SPI_DONE_MASK;
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_FIFO_INT_STATUS);

	/* trigger auto dma */
	temp = cmd << 24) | ((offset <<(spi_addr_size_3byte ? 0 : 8)) & 0xffffff);
	writel(temp, XC_SOC_PROC_MMREG_BASE + SPI_WDAT);
	do {
		temp = readl(XC_SOC_PROC_MMREG_BASE + SPI_FIFO_INT_STATUS);
		temp &= (SPI_FIFO_INT_STATUS_SPI_DMA_DONE_MASK | SPI_FIFO_INT_STATUS_SPI_DONE_MASK);
	} while(temp != (SPI_FIFO_INT_STATUS_SPI_DMA_DONE_MASK | SPI_FIFO_INT_STATUS_SPI_DONE_MASK));

	writel(SPI_FIFO_INT_STATUS_SPI_DMA_DONE_MASK | SPI_FIFO_INT_STATUS_SPI_DONE_MASK, XC_SOC_PROC_MMREG_BASE + SPI_FIFO_INT_STATUS);
	return;
}
#endif

static unsigned int spi_shift(unsigned int xfer_size, unsigned int hold_en, unsigned int cap_edge, unsigned int shift_edge, unsigned int wdat)
{
	unsigned int temp = 0;
	unsigned int cfg = 0;
	cfg |= (xfer_size << SPI_CFG_XFER_SIZE_SHIFT | hold_en << SPI_CFG_HOLD_EN_SHIFT);
	cfg |= (cap_edge << SPI_CFG_CAP_EDGE_SHIFT | shift_edge << SPI_CFG_SHFT_EDGE_SHIFT);
	cfg |= SPI_CFG_SPI_RESETN_MASK;

	writel(cfg, XC_SOC_PROC_MMREG_BASE + SPI_CFG);
	writel(wdat, XC_SOC_PROC_MMREG_BASE + SPI_WDAT);

    PRINTD("%s->%d config 0x%08x tx data 0x%08x, xfer_size %d, hold_en: %d\n", 
		__func__, __LINE__, cfg, wdat, (int)xfer_size, (int)hold_en);

	while(readl(XC_SOC_PROC_MMREG_BASE + SPI_STATUS) & SPI_STATUS_SPI_BUSY_MASK);

	temp = readl(XC_SOC_PROC_MMREG_BASE + SPI_RDAT);

    PRINTD("%s->%d rx data 0x%08x\n", __func__, __LINE__, temp);

    return temp;
}

/*-----------------------------------------------------------------------
 * SPI transfer
 *
 * This writes "bitlen" bits out the SPI MOSI port and simultaneously clocks
 * "bitlen" bits in the SPI MISO port.  That's just the way SPI works.
 *
 * The source of the outgoing bits is the "dout" parameter and the
 * destination of the input bits is the "din" parameter.  Note that "dout"
 * and "din" can point to the same memory location, in which case the
 * input data overwrites the output data (since both are buffered by
 * temporary variables, this is OK).
 */
int  spi_xfer(struct spi_slave *slave, unsigned int bitlen,
		const void *dout, void *din, unsigned long flags)
{
	struct xcode_spi_slave *xs = to_xcode_spi(slave);
	unsigned int cnt, xfered = 0;
	unsigned int tmpdin  = 0, tmpdout = 0;
	unsigned int hold = 0;
    const char *txd = dout;
    char *rxd = din;
	int i;


	/* Change the bits to count */
	cnt = bitlen >> 3;

	if (bitlen % 8) {
		printf("!!!! Warning, SPI xfer requested size %d is not byte aligned data \n", (int)bitlen);
		cnt++;
	}

	if(dout){
		PRINTD("[%s] Tx buf-> 0x%p Tx size= %d : %d\n", __func__, dout, (int)cnt, (int)bitlen);
    }

	if(din){
		PRINTD("[%s] Rx buf-> 0x%p Rx size= %d : %d\n", __func__, din, (int)cnt, (int)bitlen);
        memset(din, 0, cnt);
    }

	while (xfered < cnt) {

        if(dout){
            memcpy((char*)(&tmpdout), txd, cnt > 4 ? 4 : cnt);
            PRINTD("xfered_size=%d, pTx=0x%p, Tx_data=0x%02x, wdat=0x%08x\n", xfered, txd, *txd, tmpdout);
        }

        if(rxd == xs)
            printf("!!!!!!!! Error memory written to SPI Slave\n");


		if ((cnt < 4)||(xfered >= (cnt - 4))) {
			// process xfer bytes less then 4 bytes

			// process hold
			hold = 0;
			if (flags & SPI_XFER_BEGIN)
				hold = 1;
			if (flags & SPI_XFER_END)
				hold = 0;
	
			tmpdin = spi_shift(cnt - xfered - 1, hold, 0, 2, reverse_endian(tmpdout));

			if (din) {
				for (i=0; i<(cnt-xfered); i++){
					rxd[xfered + i] = (tmpdin>>((cnt-xfered-1-i)*8)) & 0xff;
                    PRINTD("rxd@0x%p xfered=%d, rxd[%d]=0x%02x\n", (void*)rxd, xfered, xfered+i, rxd[xfered+i]);
                }
			}
			
			xfered = cnt;			
		} else {
			// 4 bytes transfer
			if (flags & SPI_XFER_BEGIN)
				hold = 1;
			if (flags & SPI_XFER_END)
				hold = 0;
			if (xfered < (cnt - 4))
				hold = 1;
				
			tmpdin = spi_shift(3, hold, 0, 2, reverse_endian(tmpdout));
			if(din) {
				for (i=0; i<4; i++) {
					rxd[xfered + i] = (tmpdin >> (8*(3-i))) & 0xff;
                    PRINTD("rxd@0x%p, xfered=%d,  rxd[%d]=0x%02x\n", (void*)rxd, xfered, xfered+i, rxd[xfered+i]);
                }
            }
			xfered += 4;
            txd +=4;
		}
	}

	return(0);
}
