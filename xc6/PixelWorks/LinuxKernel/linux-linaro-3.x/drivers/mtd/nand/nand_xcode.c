/*
 * drivers/mtd/nand/xcode.c
 *
 * (C) 2008 Vixs Systems, Inc.
 *
 * Author: Jilai Wang <jlwang@vixs.com>
 * Copyright (C) 2008 Vixs System Inc
 *
 * Interface to generic NAND code for Vixs Xcode SOC chips
 *
 * $Id: nand_xcode.c,v 1.12 2010-11-12 20:46:07 echi Exp $
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/rslib.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <plat/xcodeRegDef.h>

#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/inftl.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

//it is better to put below definition in a seperated .h file, since user space application need it too
//just put here since this mechanism is not really necessary
#include <linux/ioctl.h>
#include <mach/xcode6-common.h>

extern LogInfoStruct *xc_log_info;

/* ioctl definitions */
#define XC_NAND_IOC_MAGIC 0xf0 //currently no one use this number, according to Documentation/ioctl-number.txt

#define XC_NAND_IOC_ERASE_DEV      _IOWR(XC_NAND_IOC_MAGIC, 0, int)

#define XC_NAND_IOC_MAXNR 0 //we can change it whenever adding nuew ioctl

/* this structure is used to transfer data between user/kernel mode
   for real command, we may don't need so many parameters*/
struct xc_nand_para {
	unsigned int para1;
//	unsigned int para2;
//	unsigned int para3;
//	unsigned int para4;
};


/* Where to look for the devices? */

#define XCODE_NAND_IOREMAP_LEN		0xb0
#define XCODE_NAND_REG_BASE			(XC_SOC_PROC_MMREG_BASE+0xE000)
#define XC_NFC_MAX_BANK_NUM			8  //it is max chip in software too
/* Jason XC4 FPGA use 4 bank */
//#define XC_NFC_MAX_BANK_NUM			0x4  //it is max chip in software too

#define XC_NFC_BANK_SEL_MASK		0xff000000
#define XC_NFC_BANK_SEL_SHIFT		24
#define XC_NFC_CMDPARAM_MASK		0xffff0000
#define XC_NFC_CMDPARAM_SHIFT		16
#define XC_NFC_CMDPARAM_SHIFT2		24

//command code
#define XC_NFC_CMDCODE_READ			0x00
#define XC_NFC_CMDCODE_READ_DWORD	0x02
#define XC_NFC_CMDCODE_PAGE_PROG	0x80
#define XC_NFC_CMDCODE_ERASE		0x60
#define XC_NFC_CMDCODE_RESET		0xFF
#define XC_NFC_CMDCODE_READ_STATUS	0x70
#define XC_NFC_CMDCODE_READ_ID		0x90

/* Hardware Interlock */
#define HOST_MUTEX_START			0x08
#define XC_FLASH_HW_MUTEX				MIPS_INTERLOCK2
#define XC_FLASH_HW_MUTEX_ID_SHIFT		MIPS_INTERLOCK2_ID4_SHIFT
#define XC_FLASH_INTERLOCK_NAND_DRV_ID	(HOST_MUTEX_START + 1)

/* interrupt line */
#define XC_NFC_INTERRUPT_LINE	XCODE6_IRQ_NFC
DECLARE_COMPLETION(xcode_nand_comp);


//for unknown reason, setting timeout to 10ms, or 1 jiffies may not work, 
//change it to 10 for safety. Anyway, we don't need the timeout unless there is error
//in that case we shouldn't access NAND at all.
#define XC_NFC_CMD_TIMEOUT		10      //10ms, or 1 jiffies
#define XC_NFC_BUFFER_OFF_STEP	4096  //we used a page(8192 as buffer), 0-4095 for one buffer, the left for another

unsigned char __attribute__((aligned(32))) syndrome[512];//16 bytes per 512 bytes data

static unsigned long __initdata xcode_nand_locations[] = {
        XCODE_NAND_REG_BASE,
	0xffffffff };

static struct mtd_info *xcodeNandList = NULL;

struct xcode_nand_priv {
	void __iomem *base;
	struct mtd_info *nextdev; // what for?

	u32 current_chip; //current working bank, in hardware's term
	unsigned int last_command; //last command set by software
	u32 current_pos; // current position in buffer,
	int current_page_addr; //only used when writting, to delay the time of configuring register so we can pipeline writting operation
	u32 pending_write; //0: no pending writing, 1: there is a writing operation isn't finished.
	u32 offset; //offset for writting buffer
	uint8_t *buffer; //buffer used for read/write
	uint8_t *buffer_phys; //physical address for buffer
	struct completion *completion;
	int part_write;
};

//below definition is ok for both x8 or x16 nand devices supported by xcode NFC
//although for x8 devices, we waste some cells
static struct nand_ecclayout xcode_nand_oob_16 = {
	.eccbytes = 4,
	.eccpos = {12, 13, 14, 15},
	.oobfree = {
		{.offset = 0,
		 .length = 4},
		{.offset = 6,
		 .length = 6}}
};

/* 64 bytes spare data with 1 bit ECC per 512 bytes array */
static struct nand_ecclayout xcode_nand_oob_64 = {
	.eccbytes = 16,
	.eccpos = {48, 49, 50, 51, 52, 53, 54, 55,
			   56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = {
		{.offset = 8,
		 .length = 40}}
};

/* 64 bytes spare data with 4 bits BCH per 512 bytes array */ 
static struct nand_ecclayout xcode_nand_oob_64_4bit = {
	.eccbytes = 28,
	.eccpos = {36, 37, 38, 39, 40, 41, 42, 43, 
			   44, 45, 46, 47, 48, 49, 50, 51,
			   52, 53, 54, 55, 56, 57, 58, 59,
			   60, 61, 62, 63},
	.oobfree = {
		{.offset = 8,
		 .length = 28}}
};

/* 128 bytes spare data with builtin ECC or 1 bit ECC for 512 bytes */
static struct nand_ecclayout xcode_nand_oob_128 = {
	.eccbytes = 16,
	.eccpos =  {112, 113, 114, 115, 116, 117, 118, 119,
				120, 121, 122, 123, 124, 125, 126, 127},
	.oobfree = {
		{.offset = 8,
		 .length = 104}}
};

/* 224 bytes spare data with 8 bits BCH per 512 bytes array, 16 bits per 1K */
static struct nand_ecclayout xcode_nand_oob_224_16bit = {
	.eccbytes = 128,
	.eccpos =  {96, 97, 98, 99, 100, 101, 102, 103,
				104, 105, 106, 107, 108, 109, 110, 111,
				112, 113, 114, 115, 116, 117, 118, 119,
				120, 121, 122, 123, 124, 125, 126, 127,
				128, 129, 130, 131, 132, 133, 134, 135,
				136, 137, 138, 139, 140, 141, 142, 143,
				144, 145, 146, 147, 148, 149, 150, 151,
				152, 153, 154, 155, 156, 157, 158, 159,
				160, 161, 162, 163, 164, 165, 166, 167,
				168, 169, 170, 171, 172, 173, 174, 175,
				176, 177, 178, 179, 180, 181, 182, 183,
				184, 185, 186, 187, 188, 189, 190, 191,
				192, 193, 194, 195, 196, 197, 198, 199,
				200, 201, 202, 203, 204, 205, 206, 207,
				208, 209, 210, 211, 212, 213, 214, 215,
				216, 217, 218, 219, 220, 221, 222, 223},
	.oobfree = {
		{.offset = 8,
		 .length = 88}}
};

/* 256 bytes spare data with 8 bits BCH per 512 bytes array, 16 bits per 1K */
static struct nand_ecclayout xcode_nand_oob_256_16bit = {
	.eccbytes = 128,
	.eccpos =  {128, 129, 130, 131, 132, 133, 134, 135,
				136, 137, 138, 139, 140, 141, 142, 143,
				144, 145, 146, 147, 148, 149, 150, 151,
				152, 153, 154, 155, 156, 157, 158, 159,
				160, 161, 162, 163, 164, 165, 166, 167,
				168, 169, 170, 171, 172, 173, 174, 175,
				176, 177, 178, 179, 180, 181, 182, 183,
				184, 185, 186, 187, 188, 189, 190, 191,
				192, 193, 194, 195, 196, 197, 198, 199,
				200, 201, 202, 203, 204, 205, 206, 207,
				208, 209, 210, 211, 212, 213, 214, 215,
				216, 217, 218, 219, 220, 221, 222, 223,
				224, 224, 226, 227, 228, 229, 230, 231,
				232, 233, 234, 235, 236, 237, 238, 239,
				240, 241, 242, 243, 244, 245, 246, 247,
				248, 249, 250, 251, 252, 253, 254, 255},
	.oobfree = {
		{.offset = 8,
		 .length = 120}}
};

/* SDK partition tables */
/* page 2048 + 64 block 128KiB */
//partition table for 256MB(2Gbit)
static struct mtd_partition xcode_nand_partition_256MB[] = {
	{
		.name		= "Boot_FW1",
		.offset		= 0x00000000,
		.size		= 0x00100000,
	}, {
		.name		= "Loader_0",
		.offset		= 0x00100000,
		.size		= 0x00200000,
	}, {
		.name		= "Loader_1",
		.offset		= 0x00300000,
		.size		= 0x00200000,
	}, {
		.name		= "ENV",
		.offset		= 0x00500000,
		.size		= 0x00100000,
	}, {
		.name		= "Kernel_0",
		.offset		= 0x00600000,
		.size		= 0x00800000,
	}, {
		.name		= "Kernel_1",
		.offset		= 0x00E00000,
		.size		= 0x00800000,
	}, {
		.name		= "Root_0",
		.offset		= 0x01600000,
		.size		= 0x06400000,
	}, {
		.name		= "Root_1",
		.offset		= 0x07A00000,
		.size		= 0x06400000,
	}, {
		.name		= "Storage",
		.offset		= 0x0DE00000,
		.size		= 0x02100000,
	}, {
		.name		= "BBT",
		.offset		= 0x0ff00000,
		.size		= 0x00100000,
	}, 
};

//partition table for 512MB
static struct mtd_partition xcode_nand_partition_512MB[] = {
	{
		.name		= "Boot_FW1",
		.offset		= 0x00000000,
		.size		= 0x00100000,
	}, {
		.name		= "Loader_0",
		.offset		= 0x00100000,
		.size		= 0x00200000,
	}, {
		.name		= "Loader_1",
		.offset		= 0x00300000,
		.size		= 0x00200000,
	}, {
		.name		= "ENV",
		.offset		= 0x00500000,
		.size		= 0x00100000,
	}, {
		.name		= "Kernel_0",
		.offset		= 0x00600000,
		.size		= 0x00800000,
	}, {
		.name		= "Kernel_1",
		.offset		= 0x00E00000,
		.size		= 0x00800000,
	}, {
		.name		= "Root_0",
		.offset		= 0x01600000,
		.size		= 0x08000000,
	}, {
		.name		= "Root_1",
		.offset		= 0x09600000,
		.size		= 0x08000000,
	}, {
		.name		= "Storage",
		.offset		= 0x11600000,
		.size		= 0x0E900000,
	}, {
		.name		= "BBT",
		.offset		= 0x1ff00000,
		.size		= 0x00100000,
	},
};

// partition table for 1GB 
static struct mtd_partition xcode_nand_partition_1GB[] = {
	{
		.name		= "Boot_FW1",
		.offset		= 0x00000000,
		.size		= 0x00100000,
	}, {
		.name		= "Loader_0",
		.offset		= 0x00100000,
		.size		= 0x00200000,
	}, {
		.name		= "Loader_1",
		.offset		= 0x00300000,
		.size		= 0x00200000,
	}, {
		.name		= "ENV",
		.offset		= 0x00500000,
		.size		= 0x00100000,
	}, {
		.name		= "Kernel_0",
		.offset		= 0x00600000,
		.size		= 0x00800000,
	}, {
		.name		= "Kernel_1",
		.offset		= 0x00E00000,
		.size		= 0x00800000,
	}, {
		.name		= "Root_0",
		.offset		= 0x01600000,
		.size		= 0x10000000,
	}, {
		.name		= "Root_1",
		.offset		= 0x11600000,
		.size		= 0x10000000,
	}, {
		.name		= "Storage",
		.offset		= 0x21600000,
		.size		= 0x1E900000,
	}, {
		.name		= "BBT",
		.offset		= 0x3ff00000,
		.size		= 0x00100000,
	},
};

//partition table for 2GB with Page: 8KiB Block: 512KiB 
static struct mtd_partition xcode_nand_partition_2GB[] = {
	{
		.name		= "Boot_FW1",
		.offset		= 0x00000000,
		.size		= 0x00200000,
	}, {
		.name		= "Loader_0",
		.offset		= 0x00200000,
		.size		= 0x00400000,
	}, {
		.name		= "Loader_1",
		.offset		= 0x00600000,
		.size		= 0x00400000,
	}, {
		.name		= "ENV",
		.offset		= 0x00A00000,
		.size		= 0x00200000,
	}, {
		.name		= "Kernel_0",
		.offset		= 0x00C00000,
		.size		= 0x01000000,
	}, {
		.name		= "Kernel_1",
		.offset		= 0x01C00000,
		.size		= 0x01000000,
	}, {
		.name		= "Root_0",
		.offset		= 0x02C00000,
		.size		= 0x10000000,
	}, {
		.name		= "Root_1",
		.offset		= 0x12C00000,
		.size		= 0x10000000,
	}, {
		.name		= "Storage",
		.offset 	= 0x22C00000,
		.size		= 0x5D000000,
	}, {
		.name		= "BBT",
		.offset		= 0x7fc00000,
		.size		= 0x00400000,
	}, 
};

static unsigned int xcode_nand_part_num = 10;
static unsigned long xcode_nand_config_location = XCODE_NAND_REG_BASE;

/* customer partition tables */
/* page size 4096 + 256  block 256KiB */
static struct mtd_partition xcode_nand_partition_512MB_BID1400[] = {
	{
		.name		= "Reserved_0",
		.offset		= 0x0,
		.size		= 0x800000,
	},
	{
		.name		= "Reserved_1",
		.offset		= 0x800000,
		.size		= 0x800000,
	},
	{
		.name		= "Kernel_0",
		.offset		= 0x1000000,
		.size		= 0x0f780000,
	},
	{
		.name		= "Kernel_1",
		.offset		= 0x10780000,
		.size		= 0x0f780000,
	},
	{
		.name		= "BBT",
		.offset		= 0x1FF00000,
		.size		= 0x100000,
	},
};

static struct mtd_partition xcode_nand_partition_1GB_BID1400[] = {
	{
		.name		= "Reserved_0",
		.offset		= 0x0,
		.size		= 0x800000,
	},
	{
		.name		= "Reserved_1",
		.offset		= 0x800000,
		.size		= 0x800000,
	},
	{
		.name		= "Kernel_0",
		.offset		= 0x1000000,
		.size		= 0x1f780000,
	},
	{
		.name		= "Kernel_1",
		.offset		= 0x20780000,
		.size		= 0x1f780000,
	},
	{
		.name		= "BBT",
		.offset		= 0x3FF00000,
		.size		= 0x100000,
	},
};

module_param(xcode_nand_config_location, ulong, 0);
MODULE_PARM_DESC(xcode_nand_config_location, "Physical memory address at which to probe for Xcode NAND Controller");

/* Sector size for HW ECC */
#define SECTOR_SIZE 512

/* JASON: Macro for access the registers. I can't understand why
 * the orginal author can do readl(addr + offset) million times
 */
#define _MMREG(off)((unsigned long) (XC_SOC_PROC_MMREG_BASE + (off)))
#define MMR_READ(reg) xc_readl(_MMREG(reg))
#define MMR_WRITE(data, reg)        xc_writel((unsigned int)(data), (unsigned int)(XC_SOC_PROC_MMREG_BASE + (reg)))

struct cdev xcode_nand_cdev;

static int xcode_nand_init_hw(struct mtd_info *mtd);

static void get_nand_bus (void)
{
	unsigned int tmp, reg;
	xc_writel((XC_FLASH_INTERLOCK_NAND_DRV_ID << XC_FLASH_HW_MUTEX_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX));
	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX) >> XC_FLASH_HW_MUTEX_ID_SHIFT) & 0x0F;
	while(tmp != XC_FLASH_INTERLOCK_NAND_DRV_ID) {
		msleep(1) ;
		xc_writel((XC_FLASH_INTERLOCK_NAND_DRV_ID <<  XC_FLASH_HW_MUTEX_ID_SHIFT), (XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX));
		tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX) >> XC_FLASH_HW_MUTEX_ID_SHIFT) & 0x0F;
	}
	
	reg = xc_readl(XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);
	reg &= ~PADU_CTRL_FLASH_MASK;
	xc_writel(reg, XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);

	tmp = xc_readl(XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
	tmp &= ~SPI_OVERRIDE_MASK;
	tmp |= NFC_NRFCN_EN_OVERRIDE_MASK;
	xc_writel(tmp, XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);

	udelay(1);	
}

static void release_nand_bus (void)
{
	unsigned int tmp;

	tmp = (xc_readl(XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX) >> XC_FLASH_HW_MUTEX_ID_SHIFT) & 0x0F;
	if(tmp == XC_FLASH_INTERLOCK_NAND_DRV_ID){
		xc_writel((XC_FLASH_INTERLOCK_NAND_DRV_ID& 0x0F) << XC_FLASH_HW_MUTEX_ID_SHIFT, XC_SOC_PROC_MMREG_BASE + XC_FLASH_HW_MUTEX);	
	}
}
//XCODE NAND CHIP functions
static void xcode_nand_cmdfunc(struct mtd_info *mtd, unsigned int command,
				int column, int page)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;
	u32 temp;
	int i, interleave=0;

//32 sj, ej;
      
#if 0
	printk("[%s]: cmd 0x%x, col %d, page_addr %d, current_chip %d\n",
	       __func__, command, column, page,
	       xcode_nand->current_chip);
#endif

	//chip number out of range, return directly
	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM) 
		return;

	xcode_nand->last_command = command;

	//wait until previous write command finished. if xcode_nand->pending_write changed in interrupt
	// between check and waiting, it should be ok since completion can handle this
	if (xcode_nand->pending_write && 
	    (command == NAND_CMD_READ0 || 
	     command == NAND_CMD_READ1 ||
	     command == NAND_CMD_READOOB || 
	     command == NAND_CMD_PAGEPROG || 
	     command == NAND_CMD_ERASE1 ||
	     command == NAND_CMD_READID || 
	     command == NAND_CMD_STATUS || 
	     command == NAND_CMD_RESET )) 
		//set time out value to 1 jiiffes, or 10ms 
		wait_for_completion_timeout(xcode_nand->completion, 
					    XC_NFC_CMD_TIMEOUT);

	if(chip->options & NAND_INTERLEAVE_MASK) {
		interleave = (chip->options & NAND_INTERLEAVE_MASK) >> NAND_INTERLEAVE_SHIFT;
		//printk("[%s] is interleave mode %d\n", __func__, interleave);
	}

	// we need to trust that upper layer won't issue a command other than read status and reset while device is busy
	//try to support both small and large flash here
	switch (command) {
	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
	case NAND_CMD_READOOB:
		get_nand_bus();
		//we have only 1 read command for all case
		if (command ==NAND_CMD_READ0) {
			xcode_nand->current_pos =  column;
		} else if (command ==NAND_CMD_READ1) { 
			xcode_nand->current_pos = 256 + column;
		} else { 
			xcode_nand->current_pos = mtd->writesize + column; //read oob
		}
		xc_writel(virt_to_phys(syndrome), XC_SOC_PROC_MMREG_BASE + BCH_SYNDRM_ADDR);

		if(interleave == 0){ // non-interleave
			xc_writel((1 << xcode_nand->current_chip) << XC_NFC_BANK_SEL_SHIFT, 
		       XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);

			xc_writel(page, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);

			//setting up DMA buffer
			inv_dcache_range((unsigned long)(xcode_nand->buffer), (unsigned long)(xcode_nand->buffer + mtd->writesize + mtd->oobsize));
			xc_writel((unsigned long)xcode_nand->buffer_phys, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
			xc_writel((unsigned long)(xcode_nand->buffer_phys + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);

			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			
			xc_writel(XC_NFC_CMDCODE_READ + (1 << XC_NFC_CMDPARAM_SHIFT), 
		       XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		}
		else {
			/* We always start with bank 0, so start_bank is not assigned */
			if(interleave == 8)
				xc_writel(page<<3, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
			else // interleave_number = 4
				xc_writel(page<<2, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);

			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			temp &= ~NF_COL_ADDR_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			//setting up DMA buffer
			xc_writel((unsigned long)xcode_nand->buffer_phys, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
			xc_writel((unsigned long) (xcode_nand->buffer_phys + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);
			
			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_READ | (interleave << XC_NFC_CMDPARAM_SHIFT),
						   XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		}
		//for test, access 4 pages one time
		//writel(XC_NFC_CMDCODE_READ + (4 << XC_NFC_CMDPARAM_SHIFT), XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		break;
				
	case NAND_CMD_RNDOUT:
		//RNDOUT always issued after a normal read command,chage position and software will read from buffer directly
		xcode_nand->current_pos = column;
		return;
			
	case NAND_CMD_SEQIN:

		//there won't have another NAND_CMD_SEQIN issued after a NAND_CMD_SEQIN
		//it is safe to reset buffer by now
		//memset(xcode_nand->buffer, 0xff, mtd->writesize + mtd->oobsize);
		//memory access is so timing consuming, reset only oob part that hardware may touch
		if (xcode_nand->offset) 
			xcode_nand->offset = 0;
		else 
			xcode_nand->offset = XC_NFC_BUFFER_OFF_STEP;
			
		memset(xcode_nand->buffer + xcode_nand->offset + mtd->writesize, 0xff, mtd->oobsize);
		xcode_nand->current_pos =  column;
		xcode_nand->current_page_addr =  page;

		if (column) { 
			xcode_nand->part_write = 1;
			//reset data buffer for partial program
			memset(xcode_nand->buffer + xcode_nand->offset, 0xff, mtd->writesize);
		}
			
		return;
			
	case NAND_CMD_RNDIN:

		//RNDIN always issued after a NAND_CMD_SEQIN or another NAND_CMD_RNDIN, 
		// chage position and software will write data to buffer directly
		xcode_nand->current_pos =  column;
		return;
			
	case NAND_CMD_PAGEPROG:
		get_nand_bus();
			
		if(interleave == 0){ // non-interleave
			xc_writel((1 << xcode_nand->current_chip) << XC_NFC_BANK_SEL_SHIFT, 
		       XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			xc_writel(xcode_nand->current_page_addr, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);

			//setting up DMA buffer
			flush_dcache_range((unsigned long)(xcode_nand->buffer + xcode_nand->offset), (unsigned long)(xcode_nand->buffer + xcode_nand->offset + mtd->writesize + mtd->oobsize));
			xc_writel((unsigned long)xcode_nand->buffer_phys + xcode_nand->offset, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
			xc_writel((unsigned long) (xcode_nand->buffer_phys + xcode_nand->offset + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);

			//disable hardware ecc for partial program
			if (xcode_nand->part_write){
				xc_writel(0x1, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
				temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
				temp &= ~BCH_ENABLE_MASK;
				xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
			}

			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_PAGE_PROG + (1 << XC_NFC_CMDPARAM_SHIFT), XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		}
		else{
			if(interleave == 8)
				xc_writel(xcode_nand->current_page_addr << 3, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
			else // interleave = 4
				xc_writel(xcode_nand->current_page_addr << 2, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);

			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			temp &= ~NF_COL_ADDR_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			//setting up DMA buffer
			flush_dcache_range((unsigned long)(xcode_nand->buffer + xcode_nand->offset), (unsigned long)(xcode_nand->buffer + xcode_nand->offset + mtd->writesize + mtd->oobsize));
			xc_writel((unsigned long)xcode_nand->buffer_phys + xcode_nand->offset, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
			xc_writel((unsigned long) (xcode_nand->buffer_phys + xcode_nand->offset + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);
			
			//disable hardware ecc for partial program
			if (xcode_nand->part_write)
				xc_writel(0x1, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
			
			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_PAGE_PROG + (interleave << XC_NFC_CMDPARAM_SHIFT),
							XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);

		}
		xcode_nand->pending_write = 1;
		return;

		//for test, progarm 4 pages at a time
		//writel(XC_NFC_CMDCODE_PAGE_PROG + (4 << XC_NFC_CMDPARAM_SHIFT), XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);

	case NAND_CMD_ERASE1:
		get_nand_bus();

		if(interleave == 0){ // non-interleave
			xc_writel((1 << xcode_nand->current_chip) << NF_BANK_ADDR_SHIFT, 
		       XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			xc_writel(page, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
			
			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_ERASE , XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		}
		else{
			/* switch to non-interleave mode to erase banks parallelly */
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			temp &= ~INTERLEAVE_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			temp &= ~NF_BANK_ADDR_MASK;

			if(interleave == 8)
				temp |= (0xff << NF_BANK_ADDR_SHIFT);
			else // interleave = 4
				temp |= (0xf << NF_BANK_ADDR_SHIFT);

			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
			xc_writel(page, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_ERASE , XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			temp |= INTERLEAVE_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
		}
		break;
			
	case NAND_CMD_STATUS:
		get_nand_bus();
		temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);

		//issue the command only when controller is ready
		if ( !(temp & 1) ) {
			//re-initialze completion struct before issue command
			INIT_COMPLETION(*(xcode_nand->completion));
			xc_writel(XC_NFC_CMDCODE_READ_STATUS + ((1 << xcode_nand->current_chip) << XC_NFC_CMDPARAM_SHIFT), 
			       XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		}
		break;

	case NAND_CMD_READID:
		get_nand_bus();
		for(i=0; i<1000; i++){
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);
			if(!(temp & NFC_CMD_STATUS_BUSY_MASK))
				break;
			udelay(100);
		}
		if(i >= 1000)
		{
			/* there is a un-finished command causing the busy bit stuck. clear it before
			    issuing the next command */
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			temp &= ~NFC_CONTROL_RESETN_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			udelay(10);
			temp |= NFC_CONTROL_RESETN_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			udelay(10);
			xc_writel(0xF, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
			udelay(10);
		}

		xcode_nand->current_pos = 0;
		//re-initialze completion struct before issue command
		INIT_COMPLETION(*(xcode_nand->completion));
		xc_writel((1 << xcode_nand->current_chip) << XC_NFC_CMDPARAM_SHIFT, \
				XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
		xc_writel(XC_NFC_CMDCODE_READ_ID + ((1 << xcode_nand->current_chip) << XC_NFC_CMDPARAM_SHIFT)
		       + (5 << XC_NFC_CMDPARAM_SHIFT2), 
		       XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);

		break;

	case NAND_CMD_RESET:
		get_nand_bus();
		temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);

		//reset controller if it is not ready
		if (temp & 1)  {
			/* this reset will reset the busy bit. other settings remain the same except NFC_WRITE_PROT */
			temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			temp &= ~NFC_CONTROL_RESETN_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			udelay(1);
			temp |= NFC_CONTROL_RESETN_MASK;
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
			udelay(1);
			xc_writel(0xF, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
			udelay(1);

		} 
				
		//re-initialze completion struct before issue command
		INIT_COMPLETION(*(xcode_nand->completion));
		if(interleave == 0) // non-interleave
			xc_writel(XC_NFC_CMDCODE_RESET + ((1 << xcode_nand->current_chip) << XC_NFC_CMDPARAM_SHIFT), 
		       XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		else
			xc_writel(XC_NFC_CMDCODE_RESET + (chip->numchips << XC_NFC_CMDPARAM_SHIFT),
		       XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
		break;

		//below are command we don't support, or don't care
	case NAND_CMD_ERASE2:
//	case NAND_CMD_STATUS_MULTI:
	case NAND_CMD_READSTART:
	case NAND_CMD_RNDOUTSTART:
	case NAND_CMD_CACHEDPROG:
	default:
		return;

	}

	wait_for_completion_timeout(xcode_nand->completion, XC_NFC_CMD_TIMEOUT);

	if (command == NAND_CMD_READ0 || command == NAND_CMD_READ1 || command == NAND_CMD_READOOB)
		inv_dcache_range((unsigned long)(xcode_nand->buffer), (unsigned long)(xcode_nand->buffer + mtd->writesize + mtd->oobsize));

	//interrup not work, use a simple loop just for test
	/*do {
	  udelay(20);
	  temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	  } while ((temp & 0x10) == 0);
	  xc_writel(0xffffffff, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);*/
	
	return;
}

static uint8_t xcode_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;
	uint8_t ret = 0xff;
	u32 temp;

	//since we always have at least 1 chip selected. if upper layer try to get data from 
	//an un-exist chip, just return 0xff.
	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM) return ret;
	//printk(KERN_INFO "xcode_nand_read_byte() command =%x, position=%x\n",
	//	xcode_nand->last_command, xcode_nand->current_pos);

	switch (xcode_nand->last_command) {
	case NAND_CMD_STATUS:

		//it is possible that we didn't issue the read status command at all since controller is busy
		temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);

		if (temp & 1)
			ret = 0; //not ready
		else
			ret = (0xff & xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_DEV_STATUS)) ;

		//it should be ok to ignore below operation by now, add it only when there is problem
		//for the lock mechanism in this register, software is unaware about it, we won't use it by now
		/*temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT) ;
		  if (chip->numchips > 4) { //max number of chips is 8, in our case
		  if (temp & (1 << (xcode_nand->current_chip/2)) == 0)
		  ret &= ~NAND_STATUS_WP;
		  }
		  else {
		  if (temp & (1 << xcode_nand->current_chip) == 0)
		  ret &= ~NAND_STATUS_WP;
		  }*/

		break;
			
	case NAND_CMD_READID:

		if (xcode_nand->current_pos == 4)
			ret = (0xff & xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_EXTENDED_ID)) ;
		else if (xcode_nand->current_pos < 4)// position is 0-3
			ret = (0xff & (xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_ID) >> (xcode_nand->current_pos * 8))) ;

		xcode_nand->current_pos++;

		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
	case NAND_CMD_READOOB:
	case NAND_CMD_RNDOUT:
//		printk("%s: cur pos %d\n", __func__, xcode_nand->current_pos); /* JDEBUG */
		ret = *(xcode_nand->buffer + xcode_nand->current_pos);
		xcode_nand->current_pos++;
		break;
			
	default:
		;
						
	}
	
	return ret;
}


static u16 xcode_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;
	u16 ret = 0xffff;

	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM) return ret;

	//to be simple, call read byte function and adjust endianess.
	//this function isn't like to be called anyway.
	ret = xcode_nand_read_byte(mtd);
	ret += xcode_nand_read_byte(mtd) << 8;

	return ret;
}


static void xcode_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM) return;

#if 0
	printk(KERN_INFO "data1 =%x, data2=%x,sdata1=%x,sdata2=%x,pos=%x\n",
	       *(xcode_nand->buffer), *(xcode_nand->buffer+1),
	       *(xcode_nand->buffer+xcode_nand->current_pos), *(xcode_nand->buffer+xcode_nand->current_pos+1),
	       xcode_nand->current_pos);
#endif
	
	switch (xcode_nand->last_command) {
	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
	case NAND_CMD_READOOB:
	case NAND_CMD_RNDOUT:

		memcpy(buf, xcode_nand->buffer + xcode_nand->current_pos, len);

#if 0	/* dump oob for 256 bytes */
		if ((xcode_nand->last_command == NAND_CMD_READOOB)&& (*((unsigned *)0xffee0008)==0x9090)){
			printk("[%s] read %d oob pos %x\n", __func__, len, xcode_nand->current_pos);
			//dump_stack();
		
		int length = 0;
		if (len <= 0x100){
			printk("data buffer:\n");
			while (length < (len >> 2)) {
				printk("0x%08x 0x%08x 0x%08x 0x%08x\n", \
						((unsigned int*)buf)[length], \
						((unsigned int*)buf)[length+1], \
						((unsigned int*)buf)[length+2], \
						((unsigned int*)buf)[length+3]);
				length += 4;
			}
		}
	}
#endif
		xcode_nand->current_pos += len;
		break;
			
	default: //it is unlikely this branch will be reached, just implement it in case of abnormal
		for (i = 0; i < len; i++)
			buf[i] = xcode_nand_read_byte(mtd);
		break;
	}
}

static void xcode_nand_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM ||
	    xcode_nand->current_pos >= mtd->writesize + chip->ecc.layout->eccpos[0]) return;
	
	switch (xcode_nand->last_command) {
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
		if (xcode_nand->current_pos + len > mtd->writesize + chip->ecc.layout->eccpos[0])
			//don't touch data used by ecc
			memcpy(xcode_nand->buffer + xcode_nand->offset + xcode_nand->current_pos, buf, 
			       mtd->writesize + chip->ecc.layout->eccpos[0] - xcode_nand->current_pos);
		else 
			memcpy(xcode_nand->buffer + xcode_nand->offset + xcode_nand->current_pos, buf, len);
				
		xcode_nand->current_pos += len;
			
		break;
			
	default: //no any other case we can write data into buffer.
		;
	}
}

static int xcode_nand_verify_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	if (xcode_nand->current_chip == XC_NFC_MAX_BANK_NUM) return -EFAULT;

	if (xcode_nand->current_pos >= mtd->writesize + chip->ecc.layout->eccpos[0]) return 0;

	//don't compare data used by ecc
	if (xcode_nand->current_pos + len > mtd->writesize + chip->ecc.layout->eccpos[0]) {
		if (memcmp(xcode_nand->buffer + xcode_nand->current_pos, buf, 
			   mtd->writesize + chip->ecc.layout->eccpos[0] - xcode_nand->current_pos))
			return -EFAULT;
	}
	else {
		if (memcmp(xcode_nand->buffer + xcode_nand->current_pos, buf, len))
			return -EFAULT;
	}
	
	return 0;
}


static int xcode_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				  uint8_t *buf, int oob_required, int page)
{
	int reg;

	chip->ecc.read_page_raw(mtd, chip, buf, oob_required, page);

	reg = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	if(reg & ECC_ERR_INT_MASK){
		printk("%s: ecc error\n", __func__);
		mtd->ecc_stats.failed++;
		reg |= ECC_ERR_INT_MASK;
		xc_writel(reg, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	}
	else if(reg & ECC_1BIT_INT_MASK){
	    printk("%s: fixed ecc error\n", __func__);
		mtd->ecc_stats.corrected++;
		reg |= ECC_1BIT_INT_MASK;
		xc_writel(reg, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	}

	/* the old ecc check won't work when the data is all 0xff, such as new erased */
#if 0
	int i, j;
	int eccsteps = chip->ecc.steps;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	chip->ecc.read_page_raw(mtd, chip, buf);
	
	for ( i = 0 ; i < eccsteps; i++ ) {
		u32 stat;

		stat = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_ECC_STAUTS0 + i*4);
		if (stat & 0x2) {
			/*if (stat & 0xfff00) 
			  mtd->ecc_stats.failed++;*/
			//check ecc data
			for (j=0; j < 4; j++) {
				if ( *(xcode_nand->buffer + mtd->writesize + 
				       chip->ecc.layout->eccpos[0] + j + i * 4) != 
				     0xff ) {
					printk("%s: ecc fail. ecc data = 0x%x\n",
					       __func__, 
					       (*(xcode_nand->buffer + mtd->writesize + 
						  chip->ecc.layout->eccpos[0] + j + i * 4)));
					mtd->ecc_stats.failed++; 
					break;
				}
			}

			//check first 16 bytes of data.
			for (j=0; j < 16; j++) {
				if ( *(xcode_nand->buffer + j + i * 512) != 0xff ) {
					printk("%s: 16byte fail. data = 0x%x\n",
					       __func__, 
					       (*(xcode_nand->buffer + j + i * 512) != 0xff ));
					mtd->ecc_stats.failed++; 
					break;
				}
			}
		}
		else if (stat & 0x1)
			mtd->ecc_stats.corrected ++;
	}
#endif
	return 0;
}

static int xcode_write_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				    const uint8_t *buf, int oob_required)
{
	chip->write_buf(mtd, buf, mtd->writesize);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

#if 0
static int xcode_write_page_null(struct mtd_info *mtd)
{
	//do nothing, just used to skip checking in nand_scan_tail
	return 0;
}
#endif

static int xcode_nand_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;
	int temp;

	// directly return ready so that following command can be issued.
	//we will waiting in cmdfunc if necessary.
	if (xcode_nand->pending_write) return NAND_STATUS_READY;

	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);
	
	if (temp & 1)   return 0;

	xcode_nand_cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
	return xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_DEV_STATUS) & NAND_STATUS_READY;
}


static int xcode_nand_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct xcode_nand_priv *xcode_nand = chip->priv;
	int temp;

	// directly return ready so that following command can be issued.
	//we will waiting in cmdfunc if necessary.
	//will this cause problem when writting is in fact failed? or WP status changed?
	if (xcode_nand->pending_write) return NAND_STATUS_READY;

	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);
	
	if (temp & 1)   return 0; //0 also means device is busy

	xcode_nand_cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
	temp = (int)chip->read_byte(mtd);;

	return temp;
}



static void xcode_nand_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	if (chipnr == -1 || chipnr >= chip->numchips) //deselect
		xcode_nand->current_chip = 0;
	else
		xcode_nand->current_chip = chipnr;

	return;
}

static inline int __init xcode_nand_init_chip(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct xcode_nand_priv *xcode_nand = chip->priv;

	//we don't need delay since NFC is we are interrupt driven. set 1 just for compatiable consideration
	chip->chip_delay = 1;  
	chip->waitfunc= xcode_nand_waitfunc; //wait function will do nothing
	chip->dev_ready = xcode_nand_dev_ready;

	chip->numchips = XC_NFC_MAX_BANK_NUM; //set the maxiam number, system will detect and reset the value later.
	chip->read_buf = xcode_nand_read_buf;
	chip->read_byte = xcode_nand_read_byte;
	chip->read_word = xcode_nand_read_word;
	chip->write_buf = xcode_nand_write_buf;
//	chip->verify_buf = xcode_nand_verify_buf;
	chip->select_chip = xcode_nand_select_chip;
	chip->cmdfunc= xcode_nand_cmdfunc;

	//by default, we enabled hardware ECC, deal with large flash device
	chip->ecc.mode		= NAND_ECC_HW;
	chip->ecc.size		= 512;
	chip->ecc.bytes 	= 3;
	chip->ecc.strength  = 1;
	chip->ecc.layout	= NULL; //will set later
	chip->ecc.read_page = xcode_read_page_hwecc;
	chip->ecc.write_page = xcode_write_page_hwecc; //hardware will calulate and write ECC by itself
	chip->ecc.calculate = NULL;//xcode_write_page_null;
	chip->ecc.correct = NULL;//xcode_write_page_null;
	chip->ecc.hwctl = NULL;//xcode_write_page_null;

	xcode_nand->last_command = NAND_CMD_RESET; //set this so current buffer is not available
	xcode_nand->current_chip = 0;
	xcode_nand->current_pos =  0;
	xcode_nand->completion=  &xcode_nand_comp;
		

	/* JASON XXX */
	/* Bad Block table scanning is very slow on XC4 FPGA
	 * Skip it for now
	 */
	//chip->options |= NAND_SKIP_BBTSCAN;
	chip->bbt_options |= NAND_BBT_USE_FLASH;
    chip->options |= NAND_NO_SUBPAGE_WRITE;

	return 0;
}

static int xcode_nand_init_hw(struct mtd_info *mtd)
{
	u32 temp;

    temp = xc_readl(XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);
    temp &= ~RBM_PADU_CTRL_PADU_CTRL_FLASH_MASK;
    xc_writel(temp, XC_SOC_PROC_MMREG_BASE + RBM_PADU_CTRL);
	
    temp = xc_readl(XC_SOC_PROC_MMREG_BASE + GPIO_N_CTRL);
    temp &= ~GPIO_N_CTRL_GPIO_MODE_SEL_MASK;
    xc_writel(temp, XC_SOC_PROC_MMREG_BASE + GPIO_N_CTRL);
	
    // for low power mode, enable NFC clk
    temp = xc_readl(XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);
    temp &= ~NFC_BLK_STOP_MASK;
    xc_writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);
    
	/* hard reset */
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
	temp |= NFC_RESET_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
	temp &= ~NFC_RESET_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);

	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
	temp |= SEL_ENABLE_MASK | NFC_NRFCN_EN_OVERRIDE_MASK;
	temp &= ~SPI_OVERRIDE_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
	
	/* soft reset */
	xc_writel(0x00010000, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
	udelay(10);
	xc_writel(0x00010011, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
	udelay(10);
	
	/* set IO timing */
	xc_writel(0x280000f2, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
	xc_writel(0x20158392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
	xc_writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
	xc_writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);
	
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	temp |= 0xf; // NANDF_WP
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	temp &= ~NFC_WRITE_PROT_LOCK_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	
	/* set bank info: xc4 uses page_size=2048 flash */
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
	temp &= ~NF_BANK_ADDR_MASK;
	temp |= 0x01000000;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
	temp &= ~NF_COL_ADDR_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
	
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
	temp &= ~BANK_NUM_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);

	/* Bootstrap override setting, setup for 2K x 64, x8 bus width, 3 read cycle*/
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BOOTSTRAP_OVRRIDE);
	temp |= BOOT_CTRL_EN_OVRRIDE_MASK;
	temp &= ~BUS_WIDTH_OVRRIDE_MASK;
	temp |= BLOCK_SIZE_OVRRIDE_MASK;
	temp &= ~PAGE_SIZE_OVRRIDE_MASK;
	temp |= RADDR_CYC_OVRRIDE_MASK;
	temp &= ~PAGE_BLOCK_OVRRIDE_MASK;
	temp |= BOOTSTRAP_OVRRIDE_EN_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BOOTSTRAP_OVRRIDE);

	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_SPARE_AREA);
	temp &= ~SPARE_AREA_SET_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_SPARE_AREA);

	//disable interleave mode
	/* JASON XXX: disable STOP_ON_ERR too */
	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
	temp &= ~((3<<24)|(1<<31));
	temp &= ~(1<<16);
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
	
	/* don't generate interrupts other than CMD_DONE */
	xc_writel(0x10, XC_SOC_PROC_MMREG_BASE + NFC_HOST_INT_MASK);

#ifdef USE_IIA	
	//enable NFC interrupt
	IIALocalSetMask(IIA_NFC_INT);
#endif

	return 0;	
}

static int xcode_nand_use_ecc(void)
{
	/* 1B_DISABLE: 0
	 * 1B_CORR: 1*/
	MMR_WRITE(0x02,	NFC_ECC_CONTROL);
	MMR_WRITE(0x00, NFC_BCH_CONTROL);

	return(0);
}

static int xcode_nand_use_bch(int bits)
{
	unsigned long temp;
	MMR_WRITE(0x01,	NFC_ECC_CONTROL);
	if (bits > 32) {
		printk(KERN_ERR "Invalid BCH len bits %d!\n", bits);
		return (-1);
	}

	/* disable BCH and enable will trigger the BCH setup */
	MMR_WRITE(0x0, NFC_BCH_CONTROL);
	wmb();
	temp = (bits<<BCH_NBIT_CORR_SHIFT) | \
			 BCH_ENABLE_MASK | BCH_CORR_EN_MASK;
	MMR_WRITE(temp, NFC_BCH_CONTROL);

	switch (bits) {
	case 8:
		/* 4 Bits for 512 Bytes Array */		
		MMR_WRITE(0x3b95db57, NFC_BCH_GEN_POLY0);
		MMR_WRITE(0x3c9efbe2, NFC_BCH_GEN_POLY1);
		MMR_WRITE(0x95ca6aab, NFC_BCH_GEN_POLY2);
		MMR_WRITE(0x00015e72, NFC_BCH_GEN_POLY3);
		break;

	case 16:
		/* 8 Bits for 512 Bytes Array */
		MMR_WRITE(0x48737e4b, NFC_BCH_GEN_POLY0);
		MMR_WRITE(0xfa516f45, NFC_BCH_GEN_POLY1);
		MMR_WRITE(0x2d698996, NFC_BCH_GEN_POLY2);
		MMR_WRITE(0xc48ce282, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0xbd718c88, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0x8f5a4a77, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0xa1712efc, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0x00000001, NFC_BCH_GEN_POLY3);
		break;

	case 17:
	default:
		/* from spec, first write the reg with BCH_ENABLE off */
		MMR_WRITE(0x1ed12a5b, NFC_BCH_GEN_POLY0);
		MMR_WRITE(0xa6671cb0, NFC_BCH_GEN_POLY1);
		MMR_WRITE(0x367cb0bc, NFC_BCH_GEN_POLY2);
		MMR_WRITE(0x16f476b4, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0xbb04565b, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0xc6bc515b, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0xdb43ef34, NFC_BCH_GEN_POLY3);
		MMR_WRITE(0x00005d94, NFC_BCH_GEN_POLY3);
		break;
	}

	return(0);
}

static int xcode_nand_disable_corr(void)
{
	MMR_WRITE(0x01,	NFC_ECC_CONTROL);
	MMR_WRITE(0x00, NFC_BCH_CONTROL);
	

	return(0);
}

static void xcode_config_ecc(struct nand_chip *chip)
{
	if(chip->options & NAND_4BIT_ECC)
		xcode_nand_use_bch(8);
	else if (chip->options & NAND_8BIT_ECC)
		xcode_nand_use_bch(16);
	else // NAND_1BIT_ECC
		xcode_nand_use_ecc();
}

static void xcode_set_io_timing(void)
{
	unsigned int jedec_id, ext_id;
	jedec_id = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_ID);
	ext_id = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_EXTENDED_ID);

	if ((jedec_id == 0x9590dc01) && (ext_id == 0x54)) {
		printk("set io timing for S34ML08G1\n");		
		xc_writel(0x280000f2, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		xc_writel(0x20158392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		xc_writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		xc_writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);
	} else if ((jedec_id == 0x2690dc98) && (ext_id == 0x76)) {
		// IO Timing for TC58NVG2S0HTAI0
		printk("set io timing for TC58NVG2S0HTAI0\n");		
		xc_writel(0x20000092, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		xc_writel(0x20158112, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		xc_writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		xc_writel(0x00000008, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);	
	} else if ((jedec_id == 0xa690d32c) && (ext_id == 0x64)) {
		printk("set io timing for MT29F8G08ABACA\n");		
		xc_writel(0x380000a1, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		xc_writel(0x2015b392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		xc_writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		xc_writel(0x0000000e, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);
	} else {
		printk("use default io timing\n");
	}

	return;
}

irqreturn_t xcode_nfc_interrupt(int irq, void *dev_id)
{
	struct mtd_info *mtd;
	struct nand_chip *chip;
	struct xcode_nand_priv *xcode_nand;
	u32 temp;

	if (unlikely(dev_id == NULL))
		return IRQ_NONE;
	
	mtd = (struct mtd_info *)dev_id;
	chip = mtd->priv;
	xcode_nand = chip->priv;

#ifdef USE_IIA
	if(!IIALocalReadInt(IIA_NFC_INT))
		return IRQ_NONE;

//	printk("%s: irq reg : 0x%x\n", __func__, temp);

	//clear the interrupt bit at first		
	IIALocalClearInt(IIA_NFC_INT);
#endif

	temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	//clear interrupt. since we enabled only command done interrupt, don't check the source of interrrupt here.

	xc_writel(0xffffffff, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	//xc_writel(0x10, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
	
	if (temp & 0x10) { // we care only command done interrupt
//		printk(KERN_INFO "xcode_nfc_interrupt(), interrupt status =%x \n", temp);
		xcode_nand->pending_write = 0;
		complete_all(xcode_nand->completion);
		
		if (xcode_nand->part_write) {
			xcode_config_ecc(chip);
			xcode_nand->part_write = 0;
		}
	}
	release_nand_bus();
	return IRQ_HANDLED;
}

static int __init xcode_nand_probe(unsigned long nand_base)
{
	struct mtd_info *mtd = NULL;
	struct nand_chip *chip;
	struct xcode_nand_priv *xcode_nand=NULL;
	int len, ret = 0, oobsize;
	unsigned int board_id, temp;

	board_id = xc_readl(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1) & 0xFF00; 

	len = sizeof(struct mtd_info) +
		sizeof(struct nand_chip) + sizeof(struct xcode_nand_priv);
	mtd = (struct mtd_info*)kmalloc(len, GFP_KERNEL);
	if (!mtd) {
		printk(KERN_ERR "Xcode NAND kmalloc (%d bytes) failed!\n", len);
		ret = -ENOMEM;
		goto fail;
	}
	memset(mtd, 0, len);

	chip = (struct nand_chip *) (mtd + 1);
	xcode_nand = (struct xcode_nand_priv *) (chip + 1);
	mtd->priv = (struct nand_chip *)chip;
	mtd->owner = THIS_MODULE;
	chip->priv	= (struct xcode_nand_priv *)xcode_nand;

	//setting address here
	xcode_nand->base = (void __iomem *)nand_base;

	/* to support interleave on a super block, data size can grow to 4096x4 b + 128x4. buffer size 20 KiB*/
	xcode_nand->buffer = (uint8_t *)__get_free_pages(GFP_DMA | GFP_KERNEL, get_order(0x5000));
	if (xcode_nand->buffer == NULL) goto fail;
	xcode_nand->buffer_phys = (uint8_t *)virt_to_phys(xcode_nand->buffer);
	
	printk(KERN_INFO "xcode_nand board id 0x%x io_buffer virt=0x%x, phys=0x%x\n", 
					(unsigned)board_id, (unsigned)xcode_nand->buffer, (unsigned)xcode_nand->buffer_phys);

	memset(xcode_nand->buffer, 0xff, 0x4000);

	//Clear all residue interrupt first
	xc_writel(0xffffffff, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);

	//we need interrupt before scanning
	ret = request_irq(XC_NFC_INTERRUPT_LINE, xcode_nfc_interrupt, IRQF_SHARED /*| SA_INTERRUPT*/, "XC_NAND", (void *)mtd);
	if (ret)
	{
		printk("Failed to allocate NAND irq\n");
		goto fail;
	}
	
	//scan hardware to set  necessary parameters at first
	xcode_nand_init_chip(mtd);
	//will enable hardware interrup here,so have to set some handler at first.
	xcode_nand_init_hw(mtd);

	ret = nand_scan_ident(mtd, XC_NFC_MAX_BANK_NUM, nand_flash_ids);

	if (ret) goto fail_irq; //fail

	//reconfigure the controller override register with the correct setting
	//bus width, default x8
	temp = 0;
	// block size 
	temp |= (mtd->erasesize >= (128 * 1024) ? 1 : 0)<<BLOCK_SIZE_OVRRIDE_SHIFT;
	// page size
	temp |= (mtd->writesize >> 12)<<PAGE_SIZE_OVRRIDE_SHIFT;
	// read cycle
	temp |= RADDR_CYC_OVRRIDE_MASK;
	// page / block
	temp |= ((mtd->erasesize / mtd->writesize == 64) ? 0 : 1)<<PAGE_BLOCK_OVRRIDE_SHIFT;
	temp |= BOOTSTRAP_OVRRIDE_EN_MASK;
	xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BOOTSTRAP_OVRRIDE);
	if(mtd->oobsize > 128)
		xc_writel(0x80000000 | (mtd->oobsize & SPARE_AREA_MASK), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_AREA);

	xcode_config_ecc(chip);
	xcode_set_io_timing();

	if(chip->numchips % 4){ 
		// interleave only applies to multiple of 4 chips 
		oobsize = mtd->oobsize;
	}
	else{ /* interleave mode */
		temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
		temp |= INTERLEAVE_MASK;
		if(chip->numchips >= 8){
			chip->options |= NAND_INTERLEAVE_8;
			len = 8;
			temp |= 3 << BANK_NUM_SHIFT;
		}
		else {
			chip->options |= NAND_INTERLEAVE_4;
			len = 4;
			temp |= 2 << BANK_NUM_SHIFT;
		}

		xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);

		mtd->writesize *= len;
		mtd->erasesize *= len;
		mtd->oobsize *= len;
		chip->bbt_erase_shift = ffs(mtd->erasesize) - 1;
		chip->page_shift = ffs(mtd->writesize) - 1;
		oobsize = mtd->oobsize/len;
	}

	//replace ecc_layout with our version
	//printk(KERN_DEBUG "xcode_probe() oobsize = %d\n", (int)oobsize);
	switch(oobsize)
	{
	case 16:
		chip->ecc.layout = &xcode_nand_oob_16;
		break;

	case 64:
		if(chip->options & NAND_4BIT_ECC)
			chip->ecc.layout = &xcode_nand_oob_64_4bit;
		else
			chip->ecc.layout = &xcode_nand_oob_64;
		break;

	case 128:
		chip->ecc.layout = &xcode_nand_oob_128;
		break;

	case 224:
		chip->ecc.layout = &xcode_nand_oob_224_16bit;
		break;

	case 256:
		chip->ecc.layout = &xcode_nand_oob_256_16bit;
		break;

	default:
		printk(KERN_INFO "OOB size %x is not supported\n", mtd->oobsize);
		ret = -ENXIO;
		goto fail_irq;
	}

	//printk(KERN_INFO "xcode_probe() nand_scan_ident() passed, will call nand_scan_tail()\n");
	ret = nand_scan_tail(mtd);


	//test code 
	/*read_org = mtd->read;
	  write_org = mtd->write;
	  mtd->read = xcode_nand_read;
	  mtd->write = xcode_nand_write;*/
	
	if (ret == 0) {		
		switch(mtd->size) {
		case 0x80000000:
			ret = mtd_device_register( mtd, xcode_nand_partition_2GB, xcode_nand_part_num);
			break;

		case 0x40000000:
			if (board_id == 0x1400) {
				xcode_nand_part_num = 5;
				ret = mtd_device_register( mtd, xcode_nand_partition_1GB_BID1400, xcode_nand_part_num);
			} else {
				ret = mtd_device_register( mtd, xcode_nand_partition_1GB, xcode_nand_part_num);
			}
			break;

		case 0x20000000:
			if (board_id == 0x1400) {
				xcode_nand_part_num = 5;
				ret = mtd_device_register( mtd, xcode_nand_partition_512MB_BID1400, xcode_nand_part_num);
			} else {
				ret = mtd_device_register( mtd, xcode_nand_partition_512MB, xcode_nand_part_num);
			}
			break;

		case 0x10000000:
			ret = mtd_device_register( mtd, xcode_nand_partition_256MB, xcode_nand_part_num);
			break;

		default:
			ret = mtd_device_register( mtd, xcode_nand_partition_512MB, xcode_nand_part_num);
			break;
		}
	}
	
	if (ret) goto fail_irq; //fail

#if 0
	/*printk(KERN_INFO " will Read the whole disk!!!\n ");
	  time_xcode = jiffies;
	  for (len = 0; len < (mtd->size >> nand->page_shift); len ++) {
	  if (mtd->block_isbad(mtd, len <<  nand->page_shift)) {
	  //printk(KERN_WARNING "nand_erase: skipped to erase a "
	  //       "bad block  0x%08d\n", len );
	  continue;
	  }
		
	  nand->select_chip(mtd, len / (nand->chipsize >> nand->page_shift));

	  nand->cmdfunc(mtd, NAND_CMD_READ0, 0 , len & nand->pagemask);
		
	  ret = nand->waitfunc(mtd, nand);
		
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  DEBUG(MTD_DEBUG_LEVEL0, "nand_erase: "
	  "Failed erase, block 0x%08x\n", len );
	  } 

	  }
	  time_xcode = jiffies - time_xcode;
	  printk(KERN_INFO " time used to Read the whole disk is %d, pages Read=%d\n ", time_xcode, len);


	  printk(KERN_INFO " will program the whole disk!!!\n ");
	  time_xcode = jiffies;
	  for (len = 0; len < (mtd->size >> nand->page_shift); len ++) {
	  if (mtd->block_isbad(mtd, len <<  nand->page_shift)) {
	  //printk(KERN_WARNING "nand_erase: skipped to erase a "
	  //       "bad block  0x%08d\n", len );
	  continue;
	  }
		
	  nand->select_chip(mtd, len / (nand->chipsize >> nand->page_shift));

	  nand->cmdfunc(mtd, NAND_CMD_PAGEPROG, 0 , len & nand->pagemask);
		
	  ret = nand->waitfunc(mtd, nand);
		
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  DEBUG(MTD_DEBUG_LEVEL0, "nand_erase: "
	  "Failed erase, block 0x%08x\n", len );
	  } 

	  }
	  time_xcode = jiffies - time_xcode;
	  printk(KERN_INFO " time used to program the whole disk is %d, pages written=%d\n ", time_xcode, len);

	  printk(KERN_INFO " will program the whole disk with similar process in software!!!\n ");
	  time_xcode = jiffies;
	  for (len = 0; len < (mtd->size >> nand->page_shift); len ++) {
	  if (mtd->block_isbad(mtd, len <<  nand->page_shift)) {
	  //printk(KERN_WARNING "nand_erase: skipped to erase a "
	  //       "bad block  0x%08d\n", len );
	  continue;
	  }
		
	  nand->select_chip(mtd, len / (nand->chipsize >> nand->page_shift));

	  xcode_nand_dev_ready(mtd);
	  nand->cmdfunc(mtd, NAND_CMD_SEQIN, 0 , len & nand->pagemask);
	  xcode_nand_write_buf(mtd, xcode_nand->buffer+4096, 2048);
	  xcode_nand_write_buf(mtd, xcode_nand->buffer+6144, 64);
	  nand->cmdfunc(mtd, NAND_CMD_PAGEPROG, 0 , len & nand->pagemask);
		
	  ret = nand->waitfunc(mtd, nand);
		
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  DEBUG(MTD_DEBUG_LEVEL0, "nand_erase: "
	  "Failed erase, block 0x%08x\n", len );
	  } 

	  }
	  time_xcode = jiffies - time_xcode;
	  printk(KERN_INFO " time used to program the whole disk is %d, pages written=%d\n ", time_xcode, len);


	  printk(KERN_INFO " will read/program the whole disk!!!\n ");
	  time_xcode = jiffies;
	  for (len = 0; len < (mtd->size >> nand->page_shift); len ++) {
	  if (mtd->block_isbad(mtd, len <<  nand->page_shift)) {
	  //printk(KERN_WARNING "nand_erase: skipped to erase a "
	  //       "bad block  0x%08d\n", len );
	  continue;
	  }
		
	  nand->select_chip(mtd, len / (nand->chipsize >> nand->page_shift));
	  if (len % 2 )
	  memset(xcode_nand->buffer, 0x5a, mtd->writesize); 
	  else
	  memset(xcode_nand->buffer, 0xa5, mtd->writesize); 

	  nand->cmdfunc(mtd, NAND_CMD_PAGEPROG, 0 , len & nand->pagemask);
		
	  ret = nand->waitfunc(mtd, nand);
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  printk("error when programming!!! \n");
	  } 

	  nand->cmdfunc(mtd, NAND_CMD_READ0, 0 , len & nand->pagemask);
		
	  ret = nand->waitfunc(mtd, nand);
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  printk("error when reading!!! \n");
	  } 
		

	  }
	  time_xcode = jiffies - time_xcode;
	  printk(KERN_INFO " time used to read/program the whole disk is %d, pages written=%d\n ", time_xcode, len);


	  //erase all the chip, just for test, here len used to indicate the current block
	  printk(KERN_INFO " will erase the whole disk!!!\n ");
	  time_xcode = jiffies;
	  for (len = 0; len < (mtd->size >> nand->phys_erase_shift); len++) {
	  if (mtd->block_isbad(mtd, len << nand->phys_erase_shift)) {
	  //printk(KERN_WARNING "nand_erase: skipped to erase a "
	  //       "bad block  0x%08d\n", len );
	  continue;
	  }
		
	  nand->select_chip(mtd, len / (nand->chipsize >> nand->phys_erase_shift));

	  nand->erase_cmd(mtd, len <<(nand->phys_erase_shift - nand->page_shift));
		
	  ret = nand->waitfunc(mtd, nand);
		
	  // See if block erase succeeded 
	  if (ret & NAND_STATUS_FAIL) {
	  DEBUG(MTD_DEBUG_LEVEL0, "nand_erase: "
	  "Failed erase, block 0x%08x\n", len );
	  }

	  }
	  time_xcode = jiffies - time_xcode;
	  printk(KERN_INFO " time used to erase the whole disk is %d\n ", time_xcode);*/
#endif

	/* Success! */
	if(xc_log_info) {
		if (board_id == 0x1400) {
			xc_log_info->nandinfo.start = 0x800000;
			xc_log_info->nandinfo.size = (u32)0x800000;
		} else {		
			xc_log_info->nandinfo.start = 0x100000;
			xc_log_info->nandinfo.size = (u32)0x200000;
		}
		xc_log_info->nandinfo.page_size = (u32)mtd->writesize;
		xc_log_info->nandinfo.data_buf = (u32)xcode_nand->buffer_phys;
		xc_log_info->nandinfo.buff_size = xc_log_info->nandinfo.size;
		xc_log_info->nandinfo.spare_buf = (u32)xc_log_info->nandinfo.data_buf + xc_log_info->nandinfo.size;
	}
	xcodeNandList = mtd;
	return 0;

fail_irq:
	free_irq(XC_NFC_INTERRUPT_LINE, mtd);

fail:
	if (mtd) kfree(mtd);
//	if (xcode_nand->buffer) free_page( (unsigned long)xcode_nand->buffer);
	if (xcode_nand->buffer)
		free_pages((unsigned long)xcode_nand->buffer, get_order(0x2000));

	printk(KERN_ERR "Xcode NAND flash driver initialization failed!\n");
	return ret;
}


long xcode_nand_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int rv = 0;
//	struct xc_nand_para para;
//	struct nand_chip *chip = mtd->priv;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != XC_NAND_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > XC_NAND_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		rv = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		rv =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (rv) return -EFAULT;

	switch(cmd) {

	case XC_NAND_IOC_ERASE_DEV:
		/* should we take advantage of this new feature?
		   just leave it for later decision
		   if (! capable (CAP_SYS_ADMIN))
		   return -EPERM;*/
//TOFIX
#if 0
		rv = copy_from_user(&para, (void*)arg, sizeof(struct xc_nand_para));
		if (!rv) {
			//para.para1 for offset, para.para2 for length
			if ((para.para1 & (mtd->erasesize-1)) || (para.para2  & (mtd->erasesize-1))) {
				printk (KERN_NOTICE "NAND erase command: Error, Offset and size must be sector aligned, \
						     ofs = %x, erasesize = %x\n", para.para1, (int) mtd->erasesize);
				return -EFAULT;
			}

			for (i = para.para1; (i < para.para1 + para.para2) && (i < mtd->size); i += mtd->erasesize) {
				chip->select_chip(mtd, i / chip->chipsize);
				chip->erase_cmd(mtd, (i & (chip->chipsize - 1)) >> chip->page_shift);
				rv = chip->waitfunc(mtd, chip);
					
				// See if block erase succeeded 
				if (rv & NAND_STATUS_FAIL) {
					printk("nand_erase: Failed erase, block 0x%08x\n", i );
				}					
			}
		}
#endif
		break;
		
	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	
	return rv;	
}

//current we implement only ioctl. just give user chance to call us directly
struct file_operations xcode_nand_fops = {
	.owner = THIS_MODULE,
	.compat_ioctl = xcode_nand_ioctl,
};

static int xcode_nand_setup_cdev(void)
{
	dev_t dev;
	int rv;

	rv = alloc_chrdev_region(&dev, 0, 1, "xcode_nand");
	if (rv < 0) {
		printk(KERN_WARNING "xcode_nand: can't get major number\n");
		return rv;
	}

	cdev_init(&xcode_nand_cdev, &xcode_nand_fops);
	xcode_nand_cdev.owner = THIS_MODULE;
	rv = cdev_add(&xcode_nand_cdev, dev, 1);
	if (rv) printk(KERN_NOTICE "Error %d adding xcode_nand\n", rv);

	return rv;
}

static ssize_t xcode_nand_proc_write(struct file *flip, const char __user *buf,
			      size_t len, loff_t *ppos)
{
	char kb[16];
	int mode;
	int err = 0;

	if (len >= 15) {
		return (0);
	}

	copy_from_user(kb, buf, len);
	mode =  simple_strtol(kb, NULL, 10);

	printk(KERN_WARNING "Change Error Correction mode may corrupt the Nand Flash data!\n");
	/*
	 * Error Correction Mode:
	 * 0 - disable error correction
	 * 1 - 1 bit ECC
	 * 8 - 8 bits BCH, 4 bits on Nand Datasheet.
	 * 16 - 16 bits BCH, 8 bits on Nand Datasheet.
	 * default - 17 bits BCH
	 */
	if (mode == 0)
		err = xcode_nand_disable_corr();		
	else if (mode == 1)
		err = xcode_nand_use_ecc();
	else if (mode == 8)
		err = xcode_nand_use_bch(8);
	else if (mode == 16)
		err = xcode_nand_use_bch(16);
	else
		err = xcode_nand_use_bch(17);

	if (err)
		return (-EFAULT);
	else
		return (len);
}

static int xcode_nand_proc_show(struct seq_file *m, void *v)
{
	volatile u32 ecc_ctrl;
	volatile u32 bch_ctrl;

	int ecc_enable = 0;
	int bch_enable = 0;
	int mode = 0;
	
	/* get the hardware status */
	ecc_ctrl = MMR_READ(NFC_ECC_CONTROL);
	bch_ctrl = (MMR_READ(NFC_BCH_CONTROL) & BCH_NBIT_CORR_MASK) >> BCH_NBIT_CORR_SHIFT;

	/* bit0 of reg ECC_CONTROL is ECC_1B_DISABLE,
	 * so NOT it and get its bit0 */
	ecc_enable = (~(ecc_ctrl & 1)) & 1;
	bch_enable = bch_ctrl > 0 ? 1 : 0;

	if (ecc_enable && (! bch_enable)) {
		mode = 1;
	} else if ( (! ecc_enable) && (bch_enable)) {
		mode = bch_ctrl;
	} else if ( (! ecc_enable) && ( ! bch_enable)) {
		mode = 0;
	} else {
		mode = -EFAULT;
	}

	return seq_printf(m, "%d\n", mode);
}

static int xcode_nand_proc_open(struct inode *inode, struct file *file)
{
        return single_open(file, xcode_nand_proc_show, NULL);
}

static const struct file_operations proc_nand_fops = {
	.open = xcode_nand_proc_open,
	.read = seq_read,
	.write = xcode_nand_proc_write,
};

static int xcode_nand_setup_proc(void)
{
	if (!proc_create("xc_nand_corr_mode", 0644, NULL, &proc_nand_fops))
	{
		printk(KERN_ERR "nand_xcode: cannot create proc entry\n");
		return -ENOMEM;
	}
//	proc->owner = THIS_MODULE;

	return (0);
}



static void release_nandxcode(void)
{
	struct mtd_info *mtd, *nextmtd;
	struct nand_chip *nand;
	struct xcode_nand_priv *xcode_nand;
       
       
	for (mtd = xcodeNandList; mtd; mtd = nextmtd) {
		nand = mtd->priv;
		xcode_nand = nand->priv;

		nextmtd = xcode_nand->nextdev;
		nand_release(mtd);
		free_irq(XC_NFC_INTERRUPT_LINE, mtd);
		free_pages((unsigned long)xcode_nand->buffer, get_order(0x2000));
		kfree(mtd);
	}

	xcodeNandList = NULL;
}

static int __init init_nandxcode(void)
{
	int i, ret = 0;

	printk(KERN_INFO "Start NAND device initializing\n");

	if (xcode_nand_config_location) {
		//printk(KERN_INFO "Using configured XCode probe address 0x%lx\n", xcode_nand_config_location);
		ret = xcode_nand_probe(xcode_nand_config_location);
		if (ret < 0)
			goto outerr;
	}
	else {
		for (i = 0; (xcode_nand_locations[i] != 0xffffffff); i++) {
			xcode_nand_probe(xcode_nand_locations[i]);
		}
	}

	if (!xcodeNandList) {
		ret = -ENODEV;
		goto outerr;
	}
	
	printk(KERN_INFO "Xcode NAND device Driver is ready\n");

	xcode_nand_setup_cdev();
	xcode_nand_setup_proc();
	
	return 0;
outerr:
	printk(KERN_INFO "No NAND device detected\n");
	return ret;
}

static void __exit cleanup_nandxcode(void)
{
	/* Cleanup the nand/Xcode resources */
	release_nandxcode();

}

module_init(init_nandxcode);
module_exit(cleanup_nandxcode);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jilai Wang <jlwang@vixs.com>");
MODULE_DESCRIPTION("Xcode NAND device driver\n");
