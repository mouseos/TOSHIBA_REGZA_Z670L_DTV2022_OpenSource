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
 *
 */

#ifndef __XCODE_MMC_H_
#define __XCODE_MMC_H_

#define MMC_DESC_NUM (32*1024) 

#define DESC_BUF_SIZE 4096


#define CORE_REG(_c, _r) ((_c) ? (_c * 0x200 + _r) : (_r))
#define S_CORE 0

struct xcode_mmc {
	unsigned int	sysad;		/* _SYSTEM_ADDRESS_0 */
	unsigned short	blksize;	/* _BLOCK_SIZE_BLOCK_COUNT_0 15:00 */
	unsigned short	blkcnt;		/* _BLOCK_SIZE_BLOCK_COUNT_0 31:16 */
	unsigned int	argument;	/* _ARGUMENT_0 */
	unsigned short	trnmod;		/* _CMD_XFER_MODE_0 15:00 xfer mode */
	unsigned short	cmdreg;		/* _CMD_XFER_MODE_0 31:16 cmd reg */
	unsigned int	rspreg0;	/* _RESPONSE_R0_R1_0 CMD RESP 31:00 */
	unsigned int	rspreg1;	/* _RESPONSE_R2_R3_0 CMD RESP 63:32 */
	unsigned int	rspreg2;	/* _RESPONSE_R4_R5_0 CMD RESP 95:64 */
	unsigned int	rspreg3;	/* _RESPONSE_R6_R7_0 CMD RESP 127:96 */
	unsigned int	bdata;		/* _BUFFER_DATA_PORT_0 */
	unsigned int	prnsts;		/* _PRESENT_STATE_0 */
	unsigned char	hostctl;	/* _POWER_CONTROL_HOST_0 7:00 */
	unsigned char	pwrcon;		/* _POWER_CONTROL_HOST_0 15:8 */
	unsigned char	blkgap;		/* _POWER_CONTROL_HOST_9 23:16 */
	unsigned char	wakcon;		/* _POWER_CONTROL_HOST_0 31:24 */
	unsigned short	clkcon;		/* _CLOCK_CONTROL_0 15:00 */
	unsigned char	timeoutcon;	/* _TIMEOUT_CTRL 23:16 */
	unsigned char	swrst;		/* _SW_RESET_ 31:24 */
	unsigned int	norintsts;	/* _INTERRUPT_STATUS_0 */
	unsigned int	norintstsen;	/* _INTERRUPT_STATUS_ENABLE_0 */
	unsigned int	norintsigen;	/* _INTERRUPT_SIGNAL_ENABLE_0 */
	unsigned short	acmd12errsts;	/* _AUTO_CMD12_ERR_STATUS_0 15:00 */
	unsigned char	res1[2];	/* _RESERVED 31:16 */
	unsigned int	capareg;	/* _CAPABILITIES_0 */
	unsigned char	res2[4];	/* RESERVED, offset 44h-47h */
	unsigned int	maxcurr;	/* _MAXIMUM_CURRENT_0 */
	unsigned char	res3[4];	/* RESERVED, offset 4Ch-4Fh */
	unsigned short	setacmd12err;	/* offset 50h */
	unsigned short	setinterr;	/* offset 52h */
	unsigned char	admaerr;	/* offset 54h */
	unsigned char	res4[3];	/* RESERVED, offset 55h-57h */
	unsigned long	admaaddr;	/* offset 58h-5Fh */
	unsigned char	res5[0x9c];	/* RESERVED, offset 60h-FBh */
	unsigned short	slotintstatus;	/* offset FCh */
	unsigned short	hcver;		/* HOST Version */
	unsigned char	res6[0x100];	/* RESERVED, offset 100h-1FFh */
};

struct xcsdh_idmac_desc {
	u32 d0;
	u32 d1;
	u32 d2;
	u32 d3;
};

struct mmc_host {
	struct xcode_mmc *reg;
	unsigned int version;	/* SDHCI spec. version */
	unsigned int core_id;	/* host controller id */
	unsigned int clock;		/* Current clock (MHz) */
	unsigned int card_present; 	
	int pwr_gpio;			/* Power GPIO */
	int oc_gpio;			/* Power Over Current  GPIO */
	int reserved;
	struct xcsdh_idmac_desc desc[MMC_DESC_NUM];	/* DMA descriptor */
};



/* REG define */
#define SMCC_CTRL     0x00
#define SMCC_PWREN    0x04
#define SMCC_CLKDIV   0x08
#define SMCC_CLKSRC   0x0C
#define SMCC_CLKENA   0x10
#define SMCC_CTYPE    0x18
#define SMCC_BLKSIZ   0x1C
#define SMCC_BYTCNT   0x20
#define SMCC_INT_MASK 0x24
#define SMCC_CMDARG   0x28
#define SMCC_CMD      0x2C
#define SMCC_RESP0    0x30
#define SMCC_RESP1    0x34
#define SMCC_RESP2    0x38
#define SMCC_RESP3    0x3C
#define SMCC_RINTSTS  0x44
#define SMCC_STATUS   0x48
#define SMCC_FIFOTH   0x4C
#define SMCC_CDETECT  0x50
#define SMCC_WRTPRT   0x54
#define SMCC_TCBCNT   0x5C
#define SMCC_TBBCNT   0x60
#define SMCC_USRID    0x68
#define SMCC_VERID    0x6c
#define SMCC_BMOD     0x80
#define SMCC_PLDMND   0x84
#define SMCC_DBADDR   0x88
#define SMCC_IDSTS    0x8c
#define SMCC_IDINTEN  0x90
#define SMCC_DSCADDR  0x94
#define SMCC_BUFADDR  0x98

#define SMCC_STATUS_DATA_BUSY_SHIFT 9
#define SMCC_CMD_USE_HOLD_REG_SHIFT 29

#define MMC_OCR_CCC             (1 << 30)     /* Card Capacity Status for SDHC.new for XC4 */
#define MMC_CAP_MULTIWRITE      (1 << 1)        /* Can accurately report bytes sent to card on error */

/* REG bit field */
/* CTRL */
#define SMCC_CTRL_IDMAC_SHIFT 25
#define SMCC_CTRL_INT_EN_SHIFT 4
#define SMCC_CTRL_DMA_RESET_SHIFT 2
#define SMCC_CTRL_FIFO_RESET_SHIFT 1
#define SMCC_CTRL_CONTROLLER_RESET_SHIFT 0
/* BMOD */
#define SMCC_BMOD_DE_SHIFT 7
#define SMCC_BMOD_SWR_SHIFT 0
/* CMD */
#define SMCC_CMD_START_CMD_SHIFT 31
#define SMCC_CMD_UPDATE_CLK_SHIFT 21
#define SMCC_CMD_SEND_INIT_SHIFT 15
#define SMCC_CMD_WAIT_PRVDATA_CMP_SHIFT 13
#define SMCC_CMD_SEND_AUTO_STOP_SHIFT 12
#define SMCC_CMD_DATA_RW_SHIFT 10
#define SMCC_CMD_DATA_EXP_SHIFT 9
#define SMCC_CMD_CHK_RSP_CRC_SHIFT 8
#define SMCC_CMD_RSP_LEN_SHIFT 7
#define SMCC_CMD_RSP_EXP_SHIFT 6
/* RINTSTS */
#define SMCC_RINTSTS_CARDDET_SHIFT 0
#define SMCC_RINTSTS_RE_SHIFT 1
#define SMCC_RINTSTS_CD_SHIFT 2
#define SMCC_RINTSTS_DTO_SHIFT 3
#define SMCC_RINTSTS_TXDR_SHIFT 4
#define SMCC_RINTSTS_RXDR_SHIFT 5
#define SMCC_RINTSTS_RCRC_SHIFT 6
#define SMCC_RINTSTS_DCRC_SHIFT 7
#define SMCC_RINTSTS_RTO_SHIFT 8
#define SMCC_RINTSTS_DRTO_SHIFT 9
#define SMCC_RINTSTS_HTO_SHIFT 10
#define SMCC_RINTSTS_FRUN_SHIFT 11
#define SMCC_RINTSTS_HLE_SHIFT 12
#define SMCC_RINTSTS_SBE_SHIFT 13
#define SMCC_RINTSTS_ACD_SHIFT 14
#define SMCC_RINTSTS_EBE_SHIFT 15

/* IRQ Mask */
#define SMCC_RINTSTS_CARDDET_MASK (1 << SMCC_RINTSTS_CARDDET_SHIFT)
#define SMCC_RINTSTS_CD_MASK (1 << SMCC_RINTSTS_CD_SHIFT)
#define SMCC_RINTSTS_DTO_MASK (1 << SMCC_RINTSTS_DTO_SHIFT)
#define SMCC_RINTSTS_TXDR_MASK (1 << SMCC_RINTSTS_TXDR_SHIFT)
#define SMCC_RINTSTS_RXDR_MASK (1 << SMCC_RINTSTS_RXDR_SHIFT)
/* for sdio */
#define SMCC_RINTSTS_ALLSDIO_IRQ_MASK (0xffff0000)

/* IRQ Errors Mask */
#define SMCC_RINTSTS_HTO_MASK (1 << SMCC_RINTSTS_HTO_SHIFT)
#define SMCC_RINTSTS_DRTO_MASK (1 << SMCC_RINTSTS_DRTO_SHIFT)
#define SMCC_RINTSTS_RTO_MASK (1 << SMCC_RINTSTS_RTO_SHIFT)
#define SMCC_RINTSTS_RE_MASK (1 << SMCC_RINTSTS_RE_SHIFT)
#define SMCC_RINTSTS_HLE_MASK (1 << SMCC_RINTSTS_HLE_SHIFT)
#define SMCC_RINTSTS_FRUN_MASK (1 << SMCC_RINTSTS_FRUN_SHIFT)
#define SMCC_RINTSTS_DCRC_MASK (1 << SMCC_RINTSTS_DCRC_SHIFT)
#define SMCC_RINTSTS_RCRC_MASK (1 << SMCC_RINTSTS_RCRC_SHIFT)
#define SMCC_RINTSTS_EBE_MASK (1 << SMCC_RINTSTS_EBE_SHIFT)
#define SMCC_RINTSTS_SBE_MASK (1 << SMCC_RINTSTS_SBE_SHIFT)

#define SMCC_RINTSTS_ALL_ERR_MASK \
	(SMCC_RINTSTS_HTO_MASK |  \
	 SMCC_RINTSTS_DRTO_MASK | \
	 SMCC_RINTSTS_RTO_MASK |  \
	 SMCC_RINTSTS_RE_MASK |	  \
	 SMCC_RINTSTS_HLE_MASK |  \
	 SMCC_RINTSTS_FRUN_MASK | \
	 SMCC_RINTSTS_DCRC_MASK | \
	 SMCC_RINTSTS_RCRC_MASK | \
	 SMCC_RINTSTS_EBE_MASK | \
	 SMCC_RINTSTS_SBE_MASK)

/* INTMASK */
#define SMCC_INT_MASK_SDIO_INT_SHIFT 16

/* descriptor bits */
#define SMCC_DESC0_OWN_SHIFT 31
#define SMCC_DESC0_CES_SHIFT 30
#define SMCC_DESC0_ER_SHIFT 5
#define SMCC_DESC0_CH_SHIFT 4
#define SMCC_DESC0_FS_SHIFT 3
#define SMCC_DESC0_LD_SHIFT 2 
#define SMCC_DESC0_DIC_SHIFT 1

#define SMCC_DESC0_OWN_MASK (1 << SMCC_DESC0_OWN_SHIFT)
#define SMCC_DESC0_CES_MASK (1 << SMCC_DESC0_CES_SHIFT)
#define SMCC_DESC0_ER_MASK (1 << SMCC_DESC0_ER_SHIFT)
#define SMCC_DESC0_CH_MASK (1 << SMCC_DESC0_CH_SHIFT)
#define SMCC_DESC0_FS_MASK (1 << SMCC_DESC0_FS_SHIFT)
#define SMCC_DESC0_LD_MASK  (1 << SMCC_DESC0_LD_SHIFT)
#define SMCC_DESC0_DIC_MASK (1 << SMCC_DESC0_DIC_SHIFT)

#endif	/* __XCODE_MMC_H_ */
