#include <asm/arch/xcodeRegDef.h>
#include <config.h>     /* ARC700 clock freq.   */
#include <asm/errno.h>
#include <asm/io.h>
#include <malloc.h>
#include <common.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <nand.h>

#ifdef CONFIG_CMD_NAND

#define XC_NFC_CMD_TIMEOUT 1000
#define XC_NFC_BUFFER_OFF_STEP 4096
#define NAND_INTERLEAVE_MASK 0xf0000000
#define NAND_INTERLEAVE_SHIFT 28
#define LP_OPTIONS NAND_SAMSUNG_LP_OPTIONS
#define XC_NFC_BANK_SEL_SHIFT 24
#define XC_NFC_CMDPARAM_MASK        0xffff0000
#define XC_NFC_CMDPARAM_SHIFT       16
#define XC_NFC_CMDPARAM_SHIFT2      24

//command code
#define XC_NFC_CMDCODE_READ         0x00
#define XC_NFC_CMDCODE_READ_DWORD   0x02
#define XC_NFC_CMDCODE_PAGE_PROG    0x80
#define XC_NFC_CMDCODE_ERASE        0x60
#define XC_NFC_CMDCODE_RESET        0xFF
#define XC_NFC_CMDCODE_READ_STATUS  0x70
#define XC_NFC_CMDCODE_READ_ID      0x90


unsigned char __attribute__((aligned(32))) syndrome[512];

DECLARE_GLOBAL_DATA_PTR;

#define NAND_CMD_TIMEOUT_MS		10

/* 64 byte oob block info for large page (== 2KB) device
 */
static struct nand_ecclayout eccoob_64_1bit = {
	.eccbytes = 16,
	.eccpos = {48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = {
			{
			.offset = 8,
			.length = 40,
			},
	}
};
/* 128 bytes spare data with builtin ECC or 1 bit ECC for 512 bytes */
static struct nand_ecclayout eccoob_128 = {
	.eccbytes = 16,
	.eccpos =  {112, 113, 114, 115, 116, 117, 118, 119,
				120, 121, 122, 123, 124, 125, 126, 127},
	.oobfree = {
		{.offset = 8,
		 .length = 104}}
};

/* 224 bytes spare data with 8 bits BCH per 512 bytes array, 16 bits per 1K */
static struct nand_ecclayout eccoob_224_16bit = {
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
static struct nand_ecclayout eccoob_256_16bit = {
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
		 	 .length = 120
			}
	},
};

enum {
	ECC_OK,
	ECC_TAG_ERROR = 1 << 0,
	ECC_DATA_ERROR = 1 << 1
};

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
} xcode_priv;


static struct mtd_info *our_mtd;
static struct nand_chip nand_chip[CONFIG_SYS_MAX_NAND_DEVICE];

/**
 * Wait for command completion
 * The typical Max block erase time is 3-3.5 ms, use 10ms as time out.
 * @param reg	nand_ctlr structure
 * @return
 *	1 - Command completed
 *	0 - Timeout
 */
static int wait_for_completion_timeout(void *dontcare, int timeout)
{

	int done=0;
	unsigned long temp;

	do
	{
		temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
		if(temp & 0x10)
			done=1;
		else
		{
			udelay(10);
			timeout--;
		}
	}while ((!done) && (timeout>0));

	done=0;
	do
	{
		temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);
		if((temp & 0x1)==0)
			done=1;
		else
		{
			udelay(10);
			timeout--;
		}
	}while ((!done) && (timeout>0));

	if(timeout<=0){
		debug("NAND: operation timeout\n");
	}
	return 0;
}

/**
 * Read one byte from the chip
 *
 * @param mtd	MTD device structure
 * @return	data byte
 *
 * Read function for 8bit bus-width
 */
static uint8_t read_byte(struct mtd_info *mtd)
{
    struct nand_chip *chip = mtd->priv;
    struct xcode_nand_priv *xcode_nand = chip->priv;
    uint8_t ret = 0xff;
    u32 temp;

    //since we always have at least 1 chip selected. if upper layer try to get data from
    //an un-exist chip, just return 0xff.
    if (xcode_nand->current_chip == CONFIG_SYS_NAND_MAX_CHIPS) return ret;
#if 0
    printk(KERN_INFO "xcode_nand_read_byte() command =%x, position=%x\n",
            xcode_nand->last_command, xcode_nand->current_pos);
#endif

    switch (xcode_nand->last_command) {
    case NAND_CMD_STATUS:

        //it is possible that we didn't issue the read status command at all since controller is busy
        temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);

		debug("status %x\n", temp);
        if (temp & 1)
            ret = 0; //not ready
        else
            ret = (0xff & xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_DEV_STATUS)) ;

		//it should be ok to ignore below operation by now, add it only when there is problem
		//for the lock mechanism in this register, software is unaware about it, we won't use it by now
		/*temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT) ;
		 * if (chip->numchips > 4) { //max number of chips is 8, in our case
		 * if (temp & (1 << (xcode_nand->current_chip/2)) == 0)
		 * ret &= ~NAND_STATUS_WP;
		 * }
		 * else {
		 * if (temp & (1 << xcode_nand->current_chip) == 0)
		 * ret &= ~NAND_STATUS_WP;
		 * }*/

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
//      printk("%s: cur pos %d\n", __func__, xcode_nand->current_pos); /* JDEBUG */
        ret = *(xcode_nand->buffer + xcode_nand->current_pos);
        xcode_nand->current_pos++;
        break;

    default:
        ;

    }

    return ret;
}

/**
 * Read len bytes from the chip into a buffer
 *
 * @param mtd	MTD device structure
 * @param buf	buffer to store data to
 * @param len	number of bytes to read
 *
 * Read function for 8bit bus-width
 */
static void read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
    int i;
    struct nand_chip *chip = mtd->priv;
    struct xcode_nand_priv *xcode_nand = chip->priv;

    if (xcode_nand->current_chip == CONFIG_SYS_NAND_MAX_CHIPS) return;

    switch (xcode_nand->last_command) {
    case NAND_CMD_READ0:
    case NAND_CMD_READ1:
    case NAND_CMD_READOOB:
    case NAND_CMD_RNDOUT:
        memcpy(buf, xcode_nand->buffer + xcode_nand->current_pos, len);
        xcode_nand->current_pos += len;
        break;

    default: //it is unlikely this branch will be reached, just implement it in case of abnormal
        for (i = 0; i < len; i++)
            buf[i] = read_byte(mtd);

    }
}

static void write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
    struct nand_chip *chip = mtd->priv;
    struct xcode_nand_priv *xcode_nand = chip->priv;

    if (xcode_nand->current_chip == CONFIG_SYS_NAND_MAX_CHIPS ||
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

static void nand_command(struct mtd_info *mtd, unsigned int command,
	int column, int page);
/**
 * Check NAND status to see if it is ready or not
 *
 * @param mtd	MTD device structure
 * @return
 *	1 - ready
 *	0 - not ready
 */
static int nand_dev_ready(struct mtd_info *mtd)
{
    struct nand_chip *chip = mtd->priv;
    struct xcode_nand_priv *xcode_nand = chip->priv;
    int temp;

    // directly return ready so that following command can be issued.
           //we will waiting in cmdfunc if necessary.
                 if (xcode_nand->pending_write) return NAND_STATUS_READY;

    temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CMD_STATUS);

    if (temp & 1)   return 0;

    nand_command(mtd, NAND_CMD_STATUS, -1, -1);
    return xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_DEV_STATUS) & NAND_STATUS_READY;
}

/* Dummy implementation: we don't support multiple chips */
static void nand_select_chip(struct mtd_info *mtd, int chipnr)
{
    struct nand_chip *chip = mtd->priv;
    struct xcode_nand_priv *xcode_nand = chip->priv;

    if (chipnr == -1 || chipnr >= chip->numchips) //deselect
        xcode_nand->current_chip = 0;    
    else
        xcode_nand->current_chip = chipnr;
	debug("[%s] chipnr %d, set current chip %d\n", __func__, chipnr, xcode_nand->current_chip);
    return;
}

static inline void get_nand_bus(void)
{
    unsigned int temp;

    temp = readl(XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
	temp &= ~SPI_OVERRIDE_MASK;
	temp |= NFC_NRFCN_EN_OVERRIDE_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
	udelay(10);

    return;
}

/**
 * Send command to NAND device
 *
 * @param mtd		MTD device structure
 * @param command	the command to be sent
 * @param column	the column address for this command, -1 if none
 * @param page_addr	the page address for this command, -1 if none
 */
static void nand_command(struct mtd_info *mtd, unsigned int command,
	int column, int page)
{
		struct nand_chip *chip = mtd->priv;
		struct xcode_nand_priv *xcode_nand = chip->priv;
		u32 temp;
		int i, interleave=0;
		temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS) & 0x7;
		if(temp) {
			debug("before issue command 0x%x get ECC error for previous command\n", command);
			xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
		}

		//chip number out of range, return directly
		if (xcode_nand->current_chip == CONFIG_SYS_NAND_MAX_CHIPS)
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

		if(chip->options & NAND_INTERLEAVE_MASK)
				interleave = (chip->options & NAND_INTERLEAVE_MASK) >> NAND_INTERLEAVE_SHIFT;
		// we need to trust that upper layer won't issue a command other than read status and reset while device is busy
		//try to support both small and large flash here
		switch (command) {
				case NAND_CMD_READ0:
				case NAND_CMD_READ1:
				case NAND_CMD_READOOB:	
						flush_dcache_range((unsigned long)(xcode_nand->buffer), \
								(unsigned long)(xcode_nand->buffer + mtd->writesize + mtd->oobsize));
						get_nand_bus();
						//we have only 1 read command for all case
						if (command ==NAND_CMD_READ0)
								xcode_nand->current_pos =  column;
						else if (command ==NAND_CMD_READ1)
								xcode_nand->current_pos = 256 + column;
						else
								xcode_nand->current_pos = mtd->writesize + column; //read oob

						xc_writel(syndrome, XC_SOC_PROC_MMREG_BASE + BCH_SYNDRM_ADDR);
						if(interleave == 0){ // non-interleave
								xc_writel((1 << xcode_nand->current_chip) << XC_NFC_BANK_SEL_SHIFT,
												XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);

								xc_writel(page, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);

								//setting up DMA buffer
								xc_writel((unsigned long)xcode_nand->buffer_phys, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
								xc_writel((unsigned long) (xcode_nand->buffer_phys + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);
								xc_writel(XC_NFC_CMDCODE_READ + (1 << XC_NFC_CMDPARAM_SHIFT),
												XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);								
						}
						else{
								/* We always start with bank 0, so start_bank is not assigned */
								if(interleave == 4)
										xc_writel(page<<2, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
								else // interleave_number = 2
										xc_writel(page<<1, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
								temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								temp &= ~NF_COL_ADDR_MASK;
								xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								//setting up DMA buffer
								xc_writel((unsigned long)xcode_nand->buffer_phys, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
								xc_writel((unsigned long) (xcode_nand->buffer_phys + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);

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
								xc_writel((unsigned long)xcode_nand->buffer_phys + xcode_nand->offset, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
								xc_writel((unsigned long) (xcode_nand->buffer_phys + xcode_nand->offset + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);

								flush_dcache_range((unsigned long)(xcode_nand->buffer + xcode_nand->offset), (unsigned long)(xcode_nand->buffer + xcode_nand->offset + mtd->writesize + mtd->oobsize));

								//disable hardware ecc for partial program
								if (xcode_nand->part_write){
										xc_writel(0x1, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
										temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
										temp &= ~BCH_ENABLE_MASK;
										xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
								}

								xc_writel(XC_NFC_CMDCODE_PAGE_PROG + (1 << XC_NFC_CMDPARAM_SHIFT), XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
						}
						else{
								if(interleave == 4)
										xc_writel(xcode_nand->current_page_addr<<2, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
								else // interleave = 2
										xc_writel(xcode_nand->current_page_addr<<1, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
								temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								temp &= ~NF_COL_ADDR_MASK;
								xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								//setting up DMA buffer
								xc_writel((unsigned long)xcode_nand->buffer_phys + xcode_nand->offset, XC_SOC_PROC_MMREG_BASE + NFC_DATA_ADDR);
								xc_writel((unsigned long) (xcode_nand->buffer_phys + xcode_nand->offset + mtd->writesize), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_ADDR);
								flush_dcache_range((unsigned long)(xcode_nand->buffer + xcode_nand->offset), (unsigned long)(xcode_nand->buffer + xcode_nand->offset + mtd->writesize + mtd->oobsize));

								//disable hardware ecc for partial program
								if (xcode_nand->part_write)
										xc_writel(0x1, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);

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

								xc_writel(XC_NFC_CMDCODE_ERASE , XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
						}
						else{
								/* switch to non-interleave mode to erase banks parallelly */
								temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
								temp &= ~INTERLEAVE_MASK;
								xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
								temp = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								temp &= ~NF_BANK_ADDR_MASK;
								if(interleave == 4)
										temp |= (0xf<<NF_BANK_ADDR_SHIFT);
								else // interleave = 2
										temp |= (0x3<<NF_BANK_ADDR_SHIFT);
								xc_writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
								xc_writel(page, XC_SOC_PROC_MMREG_BASE + NFC_PAGE_ROW_ADDR);
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
								 *                 issuing the next command */
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
						xc_writel((1 << xcode_nand->current_chip) << NFC_BANK_COL_ADDR_NF_BANK_ADDR_SHIFT, \
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
						if(interleave == 0) // non-interleave
						{
								xc_writel(XC_NFC_CMDCODE_RESET + ((1 << xcode_nand->current_chip) << XC_NFC_CMDPARAM_SHIFT),
												XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
						}
						else
								xc_writel(XC_NFC_CMDCODE_RESET + (chip->numchips << XC_NFC_CMDPARAM_SHIFT),
												XC_SOC_PROC_MMREG_BASE + NFC_COMMAND);
						break;

						//below are command we don't support, or don't care
				case NAND_CMD_ERASE2:
						//  case NAND_CMD_STATUS_MULTI:
				case NAND_CMD_READSTART:
				case NAND_CMD_RNDOUTSTART:
				case NAND_CMD_CACHEDPROG:
				default:
						return;

		}

		wait_for_completion_timeout(xcode_nand->completion, XC_NFC_CMD_TIMEOUT);

		if (command == NAND_CMD_READ0 || command == NAND_CMD_READ1 || command == NAND_CMD_READOOB) {			
			invalidate_dcache_range((unsigned long)(xcode_nand->buffer), \
				(unsigned long)(xcode_nand->buffer + mtd->writesize + mtd->oobsize));
		
		}

		//interrup not work, use a simple loop just for test
		/*do {
		 *       udelay(20);
		 *       temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
		 *       } while ((temp & 0x10) == 0);
		 *       xc_writel(0xffffffff, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);*/

		return;
}


/**
 * Page read/write function
 *
 * @param mtd		mtd info structure
 * @param chip		nand chip info structure
 * @param buf		data buffer
 * @param page		page number
 * @param with_ecc	1 to enable ECC, 0 to disable ECC
 * @param is_writing	0 for read, 1 for write
 * @return	0 when successfully completed
 *		-EIO when command timeout
 */
static int nand_rw_page(struct mtd_info *mtd, struct nand_chip *chip,
	uint8_t *buf, int page, int with_ecc, int is_writing)
{
	BUG();
	return 0;
}

/**
 * Hardware ecc based page read function
 *
 * @param mtd	mtd info structure
 * @param chip	nand chip info structure
 * @param buf	buffer to store read data
 * @param page	page number to read
 * @return	0 when successfully completed
 *		-EIO when command timeout
 */
static int nand_read_page_hwecc(struct mtd_info *mtd,
	struct nand_chip *chip, uint8_t *buf, int page)
{
    int reg;

    chip->ecc.read_page_raw(mtd, chip, buf, page);
	
    reg = xc_readl(XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
    if(reg & ECC_ERR_INT_MASK){
        debug("%s: ecc error at page %d offset 0x%x\n", __func__, page, (unsigned)(page * mtd->writesize));		
        mtd->ecc_stats.failed++;
        reg |= ECC_ERR_INT_MASK;
        xc_writel(reg, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
    }
    else if(reg & ECC_1BIT_INT_MASK){
        debug("%s: fixed ecc error\n", __func__);
        mtd->ecc_stats.corrected++;
        reg |= ECC_1BIT_INT_MASK;
        xc_writel(reg, XC_SOC_PROC_MMREG_BASE + NFC_INT_STATUS);
    }
	return 0;
}

/**
 * Hardware ecc based page write function
 *
 * @param mtd	mtd info structure
 * @param chip	nand chip info structure
 * @param buf	data buffer
 */
static void nand_write_page_hwecc(struct mtd_info *mtd,
	struct nand_chip *chip, const uint8_t *buf)
{
    chip->write_buf(mtd, buf, mtd->writesize);
    chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
}


/**
 * Read raw page data without ecc
 *
 * @param mtd	mtd info structure
 * @param chip	nand chip info structure
 * @param buf	buffer to store read data
 * @param page	page number to read
 * @return	0 when successfully completed
 *		-EINVAL when chip->oob_poi is not double-word aligned
 *		-EIO when command timeout
 */
static int nand_read_page_raw(struct mtd_info *mtd,
	struct nand_chip *chip, uint8_t *buf, int page)
{
    chip->read_buf(mtd, buf, mtd->writesize);
    chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
    return 0;

}

/**
 * Raw page write function
 *
 * @param mtd	mtd info structure
 * @param chip	nand chip info structure
 * @param buf	data buffer
 */
static void nand_write_page_raw(struct mtd_info *mtd,
		struct nand_chip *chip,	const uint8_t *buf)
{
	BUG();
#if 0
	int page;
	struct nand_drv *info;

	BUG();
	info = (struct nand_drv *)chip->priv;
	page = (readl(&info->reg->addr_reg1) >> 16) |
		(readl(&info->reg->addr_reg2) << 16);

	nand_rw_page(mtd, chip, (uint8_t *)buf, page, 0, 1);
#endif
}

/**
 * OOB data read function
 *
 * @param mtd		mtd info structure
 * @param chip		nand chip info structure
 * @param page		page number to read
 * @param sndcmd	flag whether to issue read command or not
 * @return	1 - issue read command next time
 *		0 - not to issue
 */
static int nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
	int page, int sndcmd)
{
	BUG();
#if 0
    unsigned int column;
    unsigned int pagesize = nand->pagesize;
    unsigned int oobsize = nand->oobsize;

    if(ofs < CFG_ENV_OFFSET + CFG_ENV_SIZE*4){
        pagesize /= nand->para_num;
        oobsize /= nand->para_num;
    }
    column = (unsigned int)(ofs & (pagesize - 1));

    /* Do not allow reads cross page */
    if ((column + len) > (pagesize + oobsize)){
        printf ("%s: Attempt read oob cross a page %x %x %x %x %x\n",
            __FUNCTION__, (uint) ofs, column, (uint) len, (uint)pagesize, (uint) oobsize);
        *retlen = 0;
        return -1;
    }

    nand_cmdfunc(nand, XC_NFC_CMDCODE_READ, ofs, 1);
    memcpy(buf, nand->oob_buf + column, len);

    *retlen = len;
#endif
    return 0;

}

/**
 * OOB data write function
 *
 * @param mtd	mtd info structure
 * @param chip	nand chip info structure
 * @param page	page number to write
 * @return	0 when successfully completed
 *		-EINVAL when chip->oob_poi is not double-word aligned
 *		-EIO when command timeout
 */
static int nand_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
	int page)
{
	BUG();
#if 0
    unsigned int column;

    column = (unsigned int)(ofs & (nand->pagesize - 1));
    /* Do not allow reads cross page */
    if ((column + len) > (nand->pagesize + nand->oobsize)) {
        printf ("%s: Attempt write oob cross a page %x %x %x %x %x\n",
            __FUNCTION__, (uint) ofs, column, (uint) len, (uint)nand->pagesize, (uint) nand->oobsize);
        *retlen = 0;
        return -1;
    }

    /* in fact, we are doing read-modify-write back*/
    nand_cmdfunc(nand, XC_NFC_CMDCODE_READ, ofs, 1);
    memcpy(nand->oob_buf + column, buf, len);
    nand_cmdfunc(nand, XC_NFC_CMDCODE_PAGE_PROG, ofs, 1);

    *retlen = len;
#endif
    return 0;

}

static void xcode_nand_init_hw(void)
{
    unsigned int temp;

    /* hard reset */
    get_nand_bus();

    temp = xc_readl(XC_SOC_PROC_MMREG_BASE + GPIO_N_CTRL);
    temp &= ~GPIO_N_CTRL_GPIO_MODE_SEL_MASK;
    xc_writel(temp, XC_SOC_PROC_MMREG_BASE + GPIO_N_CTRL);

    // for low power mode, enable NFC clk
	temp = readl(XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);
    temp &= ~NFC_BLK_STOP_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_BLK_STOP0);

    temp = readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    temp |= NFC_RESET_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    temp = readl(XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);
    temp &= ~NFC_RESET_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + ACC_RESET_REG0);

    temp = readl(XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);
    temp |= SEL_ENABLE_MASK | NFC_NRFCN_EN_OVERRIDE_MASK;
	temp &= ~SPI_OVERRIDE_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NRFC_NFC_SEL_OVERRIDE);

    /* soft reset */
    writel(0x00010000, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
    udelay(10);
    writel(0x00010001, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
    udelay(10);

    /* set IO timing */
	writel(0x280000f2, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
    writel(0x20158392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
    writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
    writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);

    /* unlock NAND programming */
    temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	temp |= 0xf;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
    temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);
	temp &= ~NFC_WRITE_PROT_LOCK_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_WRITE_PROT);

    /* set bank info: xc4 uses page_size=2048 flash */
    temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
    temp &= ~NF_BANK_ADDR_MASK;
    temp |= 0x01000000;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
    temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);
    temp &= ~NF_COL_ADDR_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BANK_COL_ADDR);

	temp = BOOT_CTRL_EN_OVRRIDE_MASK | RADDR_CYC_OVRRIDE_MASK|BLOCK_SIZE_OVRRIDE_MASK;
	temp |= BOOTSTRAP_OVRRIDE_EN_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BOOTSTRAP_OVRRIDE);

    temp = readl(XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);
    temp &= ~BANK_NUM_MASK;
    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);

    //disable interleave mode, disable STOP_ON_ERR, enable RANDOUT
	writel(NFC_CONTROL_RANDOM_DOUT_MASK | NFC_CONTROL_RESETN_MASK, XC_SOC_PROC_MMREG_BASE + NFC_CONTROL);

    //enable command done interrupt
	writel(0x10, XC_SOC_PROC_MMREG_BASE + NFC_HOST_INT_MASK);
    return;
}

/* ecc setup helper */
static int xcode_nand_use_ecc(void)
{
    /* 1B_DISABLE: 0
     * 1B_CORR: 1*/
    writel(0x02, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
    writel(0x00, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
    return(0);
}

static int xcode_nand_use_bch(int bits)
{
    unsigned long temp;
    writel(0x01, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
    if (bits > 32) {
        printf("Invalid BCH len bits %d!\n", bits);
        return (-1);
    }
	debug("[%s] setup use %d BCH\n", __func__, bits);

    /* disable BCH and enable will trigger the BCH setup */
    writel(0x0, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
    temp = (bits<<BCH_NBIT_CORR_SHIFT) | \
             BCH_ENABLE_MASK | BCH_CORR_EN_MASK;	

//#define DEBUG_BCH
#ifdef DEBUG_BCH
	temp |= BCH_WR_SYNDRM_MASK;
	printf("[%s], write syndrome to 0x%x\n", __func__, (unsigned)syndrome);
#endif

    writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);

    switch (bits) {
    case 8:		
        /* 4 Bits for 512 Bytes Array */		
        writel(0x3b95db57, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY0);
        writel(0x3c9efbe2, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY1);
        writel(0x95ca6aab, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY2);
        writel(0x00015e72, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        break;

    case 16:
        /* 8 Bits for 512 Bytes Array */		
        writel(0x48737e4b, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY0);
        writel(0xfa516f45, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY1);
        writel(0x2d698996, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY2);
        writel(0xc48ce282, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0xbd718c88, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0x8f5a4a77, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
		writel(0xa1712efc, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
		writel(0x00000001, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
		break;

    case 17:
    default:
        /* from spec, first write the reg with BCH_ENABLE off */
		debug("[%s] use default 17 bit BCH\n", __func__);
        writel(0x1ed12a5b, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY0);
        writel(0xa6671cb0, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY1);
        writel(0x367cb0bc, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY2);
        writel(0x16f476b4, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0xbb04565b, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0xc6bc515b, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0xdb43ef34, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        writel(0x00005d94, XC_SOC_PROC_MMREG_BASE + NFC_BCH_GEN_POLY3);
        break;
    }

    return(0);
}

static int xcode_nand_disable_corr(void)
{
    writel(0x01, XC_SOC_PROC_MMREG_BASE + NFC_ECC_CONTROL);
    writel(0x00, XC_SOC_PROC_MMREG_BASE + NFC_BCH_CONTROL);
    return(0);
}

static void xcode_set_io_timing(void)
{
	unsigned int jedec_id, ext_id;
	jedec_id = readl(XC_SOC_PROC_MMREG_BASE + NFC_ID);
	ext_id = readl(XC_SOC_PROC_MMREG_BASE + NFC_EXTENDED_ID);

	if ((jedec_id == 0x9590dc01) && (ext_id == 0x54)) {
		debug("set io timing for S34ML08G1\n");		
		writel(0x280000f2, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		writel(0x20158392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);
	} else if ((jedec_id == 0x2690dc98) && (ext_id == 0x76)) {		
		debug("set io timing for TC58NVG2S0HTAI0\n");		
		writel(0x20000092, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		writel(0x20158112, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		writel(0x00000101, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		writel(0x00000008, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);	
	} else if ((jedec_id == 0xa690d32c) && (ext_id == 0x64)) {
		debug("set io timing for MT29F8G08ABACA\n");		
		writel(0x380000a1, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING);
		writel(0x2015b392, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING2);
		writel(0x00000000, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING3);
		writel(0x0000000e, XC_SOC_PROC_MMREG_BASE + NFC_IO_TIMING4);
	} else {
		debug("use default io timing\n");
	}

	return;
}

/**
 * Board-specific NAND initialization
 *
 * @param nand	nand chip info structure
 * @return 0, after initialized, -1 on error
 */
int xc_nand_init(struct nand_chip *nand, int devnum)
{
	int node, ret;
	unsigned int temp;
	
	nand->ecc.mode = NAND_ECC_HW;
	nand->ecc.layout = &eccoob_64_1bit;

	nand->options = LP_OPTIONS | NAND_BBT_SCANNED; //Skip BBT scan
	nand->cmdfunc = nand_command;
	nand->read_byte = read_byte;
	nand->read_buf = read_buf;
	nand->write_buf = write_buf;
	nand->ecc.read_page = nand_read_page_hwecc;
	nand->ecc.write_page = nand_write_page_hwecc;
	nand->ecc.read_page_raw = nand_read_page_raw;
	nand->ecc.write_page_raw = nand_write_page_raw;
//	nand->ecc.read_oob = nand_read_oob;
//	nand->ecc.write_oob = nand_write_oob;
	nand->select_chip = nand_select_chip;
	nand->numchips=CONFIG_SYS_NAND_MAX_CHIPS;
	nand->dev_ready  = nand_dev_ready;
	nand->priv = &xcode_priv;

	xcode_nand_init_hw();
	
	our_mtd = &nand_info[devnum];
	our_mtd->priv = nand;
	ret = nand_scan_ident(our_mtd, CONFIG_SYS_NAND_MAX_CHIPS, nand_flash_ids);
	if (ret)
		return ret;

	/* alloc a block size of buffer, then read/write a block at once */
	xcode_priv.buffer = malloc(our_mtd->erasesize \
				+ our_mtd->oobsize*(our_mtd->erasesize/our_mtd->writesize) \
				+ 64);
	if(!xcode_priv.buffer)
		return -ENOMEM;

	/* cache line aligned */
	xcode_priv.buffer=((u32)xcode_priv.buffer+31)&~31;
	xcode_priv.buffer_phys=xcode_priv.buffer;
	
	nand->ecc.size = our_mtd->writesize;
	nand->ecc.bytes = our_mtd->oobsize;

	/* Re-configure the controller with the correct settings */
	//bus width, default x8    
	temp = 0;
    // block size
    temp |= (our_mtd->erasesize >= (128 * 1024) ? 1 : 0)<<BLOCK_SIZE_OVRRIDE_SHIFT;
    // page size
    temp |= (our_mtd->writesize >> 12)<<PAGE_SIZE_OVRRIDE_SHIFT;
    // read cycle
    temp |= RADDR_CYC_OVRRIDE_MASK;
    // page / block
    temp |= ((our_mtd->erasesize / our_mtd->writesize == 64) ? 0 : 1)<<PAGE_BLOCK_OVRRIDE_SHIFT;
    temp |= BOOTSTRAP_OVRRIDE_EN_MASK;
    temp = writel(temp, XC_SOC_PROC_MMREG_BASE + NFC_BOOTSTRAP_OVRRIDE);

    if(our_mtd->oobsize > 128)
    	writel(0x80000000 | (our_mtd->oobsize & SPARE_AREA_MASK), XC_SOC_PROC_MMREG_BASE + NFC_SPARE_AREA);

	if (nand->options & NAND_8BIT_ECC) {
		if(our_mtd->oobsize == 224)
			nand->ecc.layout = &eccoob_224_16bit;
		if(our_mtd->oobsize == 256)
			nand->ecc.layout = &eccoob_256_16bit;
		xcode_nand_use_bch(16);
	} else {
		if(our_mtd->oobsize == 128)
			nand->ecc.layout = &eccoob_128;
		xcode_nand_use_ecc();
	}
	
	xcode_set_io_timing();

	ret = nand_scan_tail(our_mtd);
	if (ret)
		return ret;

	ret = nand_register(devnum);
	if (ret)
		return ret;

	return 0;
}

void board_nand_init(void)
{
	struct nand_chip *nand = &nand_chip[0];

	if (xc_nand_init(nand, 0))
		puts("XC NAND init failed\n");
}


#endif

