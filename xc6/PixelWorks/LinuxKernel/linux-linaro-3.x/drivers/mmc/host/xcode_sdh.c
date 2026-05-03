#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
//#include <linux/mmc/protocol.h>

#include <linux/platform_device.h>

#include <asm/io.h>
#ifdef CONFIG_MIPS_XCODE
#include <asm/mach-viper/viper.h>
#include <asm/mach-viper/viperdef.h>
#endif

#ifdef CONFIG_ARCH_XCODE6
#include <plat/xcodeRegDef.h>
#endif

//#define XCT_PROFILE

#ifdef XCT_PROFILE
#include <asm/xc_timer.h>
#endif

#include <linux/proc_fs.h>
#include <linux/mmc/xcode_sdh.h>

MODULE_AUTHOR("Emerson Chi <echi@pixelworks.com>");
MODULE_DESCRIPTION("Driver for SD/MMC Host Controller");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.2");

#define DEV_NAME "xcsdh"

#define ERRMSG(_fmt, _args...) \
	printk(KERN_ERR "[%s->%d]: Error " _fmt, __func__,  __LINE__, ## _args);

#define WARNMSG(_fmt, _args...) \
	printk(KERN_WARNING "[%s->%d]: Warning " _fmt, __func__,  __LINE__, ## _args);

//#define XCSDH_DEBUG
#ifdef XCSDH_DEBUG

#define DPRINTK(_fmt, _args...)		\
	printk("%s: " _fmt, __func__,  ## _args);


#define PRINT_16BYTES(_buf) \
	do {		    \
		int j = 0;				\
		for (j = 0; j < 16; j++) {		\
			printk("%02x ", (_buf[j]));	\
		}					\
		printk("\n");				\
	} while (0);

#define TRACE do{ printk("[%s] -> %d\n", __func__, __LINE__); }while(0)
#else

#define DPRINTK(_fmt, _args...)

#define PRINT_16BYTES(_buf)
#define TRACE
#endif	/* XCSDH_DEBUG */


/* XXX: Danger! No type check!
 * conent of _n will be destroied
 * _n and _r must NOT be same var
 */
#define BIT_REVERSE_U32(_n, _r)			\
	do {					\
		int _i = 0;			\
		_r = 0;					\
		for(_r = _i = 0; _i < 32; _i++) {	\
			_r = (_r << 1) + (_n & 1);	\
			_n >>= 1;			\
		}					\
	} while (0);

#define _MMFB_BASE 0x0
#define _MMREG(off)((void*) (XC_SOC_PROC_MMREG_BASE + (off)))


#define _MMFB_LOC(off) ((void*)(_MMFB_BASE + (off)))

#define MMR_READ(reg) readl(_MMREG(reg))
#define MMR_WRITE(data, reg)        writel( (data), (_MMREG(reg)))

#define MMFB_READ(off)              readl( (_MMFB_LOC(off)))
#define MMFB_WRITE(data, off)       writel( (data), (_MMFB_LOC(off)))


#define CORE_REG(_c, _r) ((_c) ? (_c * 0x200 + _r) : (_r))

#define S_CORE 0

static const u32 XCSDH_INT_DATA_MASK =
SMCC_RINTSTS_DTO_MASK |
//	SMCC_RINTSTS_TXDR_MASK |
//	SMCC_RINTSTS_RXDR_MASK |
SMCC_RINTSTS_RTO_MASK |
SMCC_RINTSTS_DRTO_MASK |
SMCC_RINTSTS_HTO_MASK |
SMCC_RINTSTS_DCRC_MASK |
SMCC_RINTSTS_FRUN_MASK;

static const u32 XCSDH_INT_RSP_MASK =
SMCC_RINTSTS_RTO_MASK |
SMCC_RINTSTS_RE_MASK |
SMCC_RINTSTS_RCRC_MASK;


static spinlock_t iplock;

static struct platform_device *xcsdh_device = NULL;
static struct mmc_host *xcsdh_mmc_host = NULL;
static int init_core0 = 0, init_core1 = 0;
static struct xcsdh_master *master;
static struct xcsdh_host direct_host = {.core_id = 0};


/* XXX debug */
//static u32 last_tcbcnt  = 0;
static u32 same_tcbcnt_nr = 0;
//static u32 wait_data_irq_cnt = 0;
//static u32 cmd_cnt = 0;

static atomic_t xcsdh_irq_cnt;
static struct timer_list led_off_timer;
static int flash_scheduled = 0;
static u32 gpio_led_mask = 0;
static u32 g_ready_to_init = 0;

#ifdef CONFIG_MMC_XCODE_ENCRYPTION

static atomic_t dmaHwLockCounter;

// Running MMC encryption / decryption in poll mode
#define MMC_ENCRYPTION_POLL_MODE

#define PROC_eMMC_ENCRYPTION	"eMMC_Encryption"
#define DMA_ENCRYPTION_NAME		"DMA_Encryption"
#ifdef CONFIG_MMC_XCODE_STARTUP_ENCRYPTION
u8 emmcEncryption = 1;
#else
u8 emmcEncryption = 0;
#endif

// For debugging 
#ifdef MMC_ENCRYPTION_DEBUG
u8 enableMMCDebug = 0;

#define COMMMAND_HISTORY	64
MMC_DEBUG_ITEM mmcDebugItems[COMMMAND_HISTORY];
int debugIndex = COMMMAND_HISTORY - 1;

#define ENCRYPT_PARTITIONS			0x30	// GP0 (partition 4), and GP1 (partition 5)
// Changing encrypt partition at runtime. Partition number 8 means encrypt all partitions.
#define ALL_PARTITION_ENCRYPTED		0xFE	// Partition 0 is not encrypted
// encryptPartition is a bit mask of partitions with encryption enabled.
u8 encryptPartition = ENCRYPT_PARTITIONS;
#define PROC_MMC_ENCRYPT_PARTITION	"MMC_ENCRYPT_PARTITION"

#define PROC_MMC_ENCRYPT_DEBUG	"MMC_ENABLE_ENCRYPT_DEBUG"

#define PRINT_SCATTER_LIST(title, sg, sg_len) \
	do { if ((enableMMCDebug & 0x01) && emmcEncryption) print_scatter_list(title, sg, sg_len); } while (0)

#define PRINT_ENCRYPT_DESCRIPTORS(pDesc, count) \
	do { if ((enableMMCDebug & 0x02) && emmcEncryption) print_encrypt_descriptors(pDesc, count); } while (0)

#define PRINT_MMC_COMMAND(title, state, cmd) \
	do { if ((enableMMCDebug & 0x04) && emmcEncryption) printMMC_Command(title, state, cmd); } while (0)

#define ENCRYPT_DEBUG_PRINTK(_fmt, _args...)		\
	do { if ((enableMMCDebug & 0x08) && emmcEncryption) printk(_fmt, ## _args); } while (0)

static ssize_t proc_read_mmc_encrypt_partition(struct file *file, char __user *buf, size_t count, loff_t *offs) 
{
	printk("MMC encrypted partitions: %#x\n", encryptPartition);
	if (encryptPartition == ALL_PARTITION_ENCRYPTED) {
		printk("\tAll partitions are encrypted\n");
	}
	else {
		if (encryptPartition == 0) {
			printk("\tEncyption is disabled on all partitions\n");
		}
	}
	return 0;
}

static ssize_t proc_write_mmc_encrypt_partition(struct file *file, const char __user *buf, size_t count, loff_t *offs) 
{
	u8 partition;
	if (count < sizeof(partition)) {
		printk("Invalid count:%d\n", count);
		return count;
	}
	copy_from_user(&partition, buf, sizeof(partition));
	if (partition < '0' || partition > '7') {
		printk("Invalid input, %c\n", partition);
	}
	else {
		partition -= '0';
		if (partition == 0) {
		// partition = 0 is used to clear encryption on all partitions
			encryptPartition = 0;
		}
		else {
			encryptPartition |= (1 << partition);
		}
	}
	printk("MMC encrypted partition: %#x\n", encryptPartition);
	if (encryptPartition == ALL_PARTITION_ENCRYPTED) {
		printk("\tAll partitions are encrypted\n");
	}
	return count;
}

static const struct file_operations proc_mmc_encrypt_partition_fops = {
	.read = proc_read_mmc_encrypt_partition,
	.write = proc_write_mmc_encrypt_partition,
};


static ssize_t proc_read_mmc_encrypt_debug(struct file *file, char __user *buf, size_t count, loff_t *offs) 
{
	printk("MMC debug: %#x\n", enableMMCDebug);
	return 0;
}

static ssize_t proc_write_mmc_encrypt_debug(struct file *file, const char __user *buf, size_t count, loff_t *offs) 
{
	u8 debug;
	if (count < sizeof(debug)) {
		printk("Invalid count:%d\n", count);
		return count;
	}
	copy_from_user(&debug, buf, sizeof(debug));
	if (debug < '0' || debug > '9') {
		printk("Invalid input, %c\n", debug);
	}
	else {
		enableMMCDebug = debug - '0';
	}
	printk("MMC debug: %#x\n", enableMMCDebug);
	return count;
}

static const struct file_operations proc_mmc_encrypt_debug_fops = {
	.read = proc_read_mmc_encrypt_debug,
	.write = proc_write_mmc_encrypt_debug,
};

// Exposing selected partition selected via CMD6
u8 selectedPartition = 0;
#define PROC_MMC_SELECTED_PARTITION	"MMC_SELECTED_PARTITION"

static ssize_t proc_read_mmc_selected_partition(struct file *file, char __user *buf, size_t count, loff_t *offs) 
{
	printk("MMC selected partition: %d\n", selectedPartition);
	return 0;
}

static ssize_t proc_write_mmc_selected_partition(struct file *file, const char __user *buf, size_t count, loff_t *offs) 
{
	return count;
}

static const struct file_operations proc_mmc_last_partition_fops = {
	.read = proc_read_mmc_selected_partition,
	.write = proc_write_mmc_selected_partition,
};

static void track_selected_partition(u32 arg)
{
	int index;
	if ((arg & 0x03000000) == 0x03000000) { // write to EXT_CSD
		index = (arg & 0x00FF0000) >> 16;
		if (index == 179) {
			selectedPartition = (arg & 0xFF00) >> 8;
		}
	}
}

static void printMMC_Command(char * title, int state, struct mmc_command *cmd)
{
	struct mmc_data * data = cmd->data;
	u32 opcode = cmd->opcode;
	u32 arg = cmd->arg;
	int index, partition;
	printk("%s:%d CMD:%d, args:0x%08x flags:0x%08x\n", 
			title, state, opcode, arg, data ? data->flags : -1);
	switch (opcode) {
		case 6:
			if ((arg & 0x03000000) == 0x03000000) { // write to EXT_CSD
				index = (arg & 0x00FF0000) >> 16;
				if (index == 179) {
					partition = (arg & 0xFF00) >> 8;
					selectedPartition = partition;
					printk("\tpartition:%d\n", partition);
				}
			}
			break;
		case 12:
		case 13:
			if (data) {
				printk("\tbytes_xfered:%d\n", data->bytes_xfered);
			}
			break;
		case 18:
		case 25:
			if (data) {
				printk("\tblocks:%d, sg_len:%d, bytes_xfered:%d\n", 
					data->blocks, data->sg_len, data->bytes_xfered);
			}
			break;
		default:
			break;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			printk("\tRSP 0x%08x 0x%08x 0x%08x 0x%08x\n", 
				cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3]);
		} else {
			printk("\tRSP 0x%08x\n", cmd->resp[0]);
		}
	}
}

static void print_scatter_list(char * title, struct scatterlist *sg, int sg_len)
{
	struct scatterlist * s;
	int i;

	printk("%s scatter list size: %d\n", title, sg_len);
	if (!sg || !sg_len) {
		return;
	}

	for_each_sg(sg, s, sg_len, i) {
		printk("%2d- %d, %d : %#x, %#x\n", i, s->offset, s->length, s->length, s->dma_address);
	}

}

static void print_encrypt_descriptors(struct xcsdh_encrypt_descriptor *pDesc, int count)
{
	int i;

	printk("%s number of descriptors:%d\n", __func__, count);
	if (!pDesc) {
		printk("Invalid descriptor\n");
		return;
	}

	printk("           Source              Destination        Command       CW         CW/IV\n");
	for (i = 0; i < count; i++, pDesc++) {
		printk("%2d- 0x%08x-0x%08x 0x%08x-0x%08x 0x%08x   0x%08x  0x%08x\n", 
			i, pDesc->source_lo, pDesc->source_hi, 
			pDesc->destination_lo, pDesc->destination_hi, 
			pDesc->cmd_specifier, pDesc->crypto_control_word, pDesc->crypto_cw_iv);
	}
}

void recordCommand(struct mmc_command *cmd)
{
	MMC_DEBUG_ITEM * pI;
	debugIndex = ++debugIndex & (COMMMAND_HISTORY - 1);
	pI = &mmcDebugItems[debugIndex];
	pI->opcode = cmd->opcode;
	pI->arg = cmd->arg;
	pI->partition = selectedPartition;
	pI->flags = cmd->flags;
}

#else // MMC_ENCRYPTION_DEBUG

#define PRINT_SCATTER_LIST(title, sg, sg_len)
#define PRINT_ENCRYPT_DESCRIPTORS(pDesc, count)
#define PRINT_MMC_COMMAND(title, state, cmd)
#define ENCRYPT_DEBUG_PRINTK(_fmt, _args...)

#endif // MMC_ENCRYPTION_DEBUG

// Hardware interlock / mutex routines.
// Should be called with interrupt disabled, because of smp_processor_id().
// Using smp_processor_id() scheme is changed in favor of dmaHwLockCounter, 
// with interrupt ENABLED!!
static inline void emmcHw_lock(void) {
	u32 myId = HOST_MMC_LOCK_START << MIPS_INTERLOCK1_ID7_SHIFT;
	u32 reg;
	int counter = -1;

	reg = MMR_READ(XCSDH_ENCRYPT_DMA_MUTEX_REG) & XCSDH_ENCRYPT_DMA_MUTEX_MASK;
	if (reg == myId) {
		counter = atomic_add_return(1, &dmaHwLockCounter);
		if (counter > NR_CPUS) {
			panic("%s:L%d lock counter: %d\n", __func__, __LINE__, counter);
		}
		return;
	}

	while (1) {
		MMR_WRITE(myId, XCSDH_ENCRYPT_DMA_MUTEX_REG);
		// Check the HW lock was obtained, since this function is called with interrupt enabled.
		reg = MMR_READ(XCSDH_ENCRYPT_DMA_MUTEX_REG) & XCSDH_ENCRYPT_DMA_MUTEX_MASK;
		if (reg == myId) {
			// HW lock was obtained
			counter = atomic_add_return(1, &dmaHwLockCounter);
			if (counter > NR_CPUS) {
				panic("%s:L%d lock counter: %d part:%d\n", __func__, __LINE__, counter, selectedPartition);
			}
			break;
		}
		cond_resched();
	}

	ENCRYPT_DEBUG_PRINTK("===> HW lock: %d part:%d\n", counter, selectedPartition);
}

// This function is called with interrupt enabled
static inline void emmcHw_unlock(void) {
	u32 myId = HOST_MMC_LOCK_START << MIPS_INTERLOCK1_ID7_SHIFT;
	int counter = atomic_read(&dmaHwLockCounter);
	u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_MUTEX_REG) & XCSDH_ENCRYPT_DMA_MUTEX_MASK;

	if (reg == 0 && counter == 0) {
		printk("%s redundant caller:%pS\n", __func__, (void *)_RET_IP_);
		return;
	}

	if (reg == myId) {
		counter = atomic_sub_return(1, &dmaHwLockCounter);
		if (counter == 0) {
			MMR_WRITE(myId, XCSDH_ENCRYPT_DMA_MUTEX_REG);
		}
		else if (counter < 0) {
			panic("%s counter:%d", __func__, counter);
		}

		ENCRYPT_DEBUG_PRINTK("<=== HW unlock: %d part:%d\n", counter, selectedPartition);
	}
	else {
		panic("%s not the owner reg:%#x counter:%d caller:%pS\n", __func__, reg, counter, (void *)_RET_IP_);
	}
}

static ssize_t proc_read_eMMC_Encryption(struct file *file, char __user *buf, size_t count, loff_t *offs) 
{
	printk("eMMC Encryption: %s\n", emmcEncryption ? "ENABLED" : "DISABLED");
	return 0;
}

static ssize_t proc_write_eMMC_Encryption(struct file *file, const char __user *buf, size_t count, loff_t *offs) 
{
	u8 encrypt;
	if (count < sizeof(encrypt)) {
		printk("Invalid count:%d\n", count);
		return count;
	}
	copy_from_user(&encrypt, buf, sizeof(encrypt));
	if (encrypt != '0' && encrypt != '1') {
		printk("Invalid input, %c\n", encrypt);
	}
	else {
		emmcEncryption = encrypt - '0';
	}
	printk("eMMC Encryption: %s\n", emmcEncryption ? "ENABLED" : "DISABLED");
	return count;
}

static const struct file_operations proc_eMMC_Encryption_fops = {
	.read = proc_read_eMMC_Encryption,
	.write = proc_write_eMMC_Encryption,
};

static struct xcsdh_host * get_xcsdh_host(void)
{
	int hostNumber = CONFIG_MMC_CARD_ENCRYPTED;
	struct xcsdh_host * pHost = NULL;

	if (hostNumber < XCSDH_HOST_NR) {
		pHost = master->sd_host[hostNumber];
	}

	if (hostNumber >= XCSDH_HOST_NR || pHost == NULL) {
		printk(KERN_ERR "%s failed host#:%d, host:%p\n", __FUNCTION__, hostNumber, pHost);
	}

	return pHost;
}

static inline void mmc_encryption_clear_intr(void) 
{
	volatile u32 reg;
	reg = XCSDH_ENCRYPT_DMA_INT_STATUS_BIT;
	MMR_WRITE(reg, XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
}

static inline void mmc_encryption_disable_intr(void) 
{
	volatile u32 reg;
	reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_MASK_REG);
	reg &= ~XCSDH_ENCRYPT_DMA_INT_MASK_BIT;
	MMR_WRITE(reg, XCSDH_ENCRYPT_DMA_INT_MASK_REG);	
}

static inline void mmc_encryption_enable_intr(void)
{
	volatile u32 reg;
	reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_MASK_REG);
	reg |= XCSDH_ENCRYPT_DMA_INT_MASK_BIT;
	MMR_WRITE(reg, XCSDH_ENCRYPT_DMA_INT_MASK_REG);
}

static inline u32 mmc_encryption_intr_mask(void)
{
	volatile u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_MASK_REG);
	return reg;
}

static inline u32 mmc_encryption_intr_status(void)
{
	volatile u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
	return reg;
}

static inline bool mmc_encryption_intr_pending(void) 
{
	volatile u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
	return (reg & XCSDH_ENCRYPT_DMA_INT_STATUS_BIT);
}

static inline bool mmc_encryption_dma_done(void)
{
	volatile u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
	if (reg & XCSDH_ENCRYPT_DMA_INT_STATUS_BIT) {
		// Clear status bit
		MMR_WRITE(XCSDH_ENCRYPT_DMA_INT_STATUS_BIT, XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
		return true;
	}
	return false;
}

static inline u32 mmc_encryption_dma_status(void)
{
	volatile u32 reg = MMR_READ(XCSDH_ENCRYPT_DMA_STATUS_REG);
	return reg;
}

static bool mmc_encryption_dma_ready(void) 
{
	int i;
	for (i = 0; i < XCSDH_ENCRYPT_DMA_READY_TIMEOUT; i++) {
		if (!(mmc_encryption_dma_status() & XCSDH_ENCRYPT_DMA_CHANNEL_AVAILABLE)) {
			return true;
		}
	}

	// This should not happen!
	printk("DMA channel not ready 0x%08x", mmc_encryption_dma_status());
	return false;
}

static inline void mmc_encryption_write_dma_desc_queue(u32 desc)
{
	volatile u32 reg;
	MMR_WRITE(desc, XCSDH_ENCRYPT_DMA_DESC_QUEUE_REG);
	reg = MMR_READ(XCSDH_ENCRYPT_DMA_DESC_QUEUE_REG);
	if (reg != desc) {
		printk(KERN_ERR "DQ_PTR0 %#x - %#x\n", reg, desc);
	}
}

static bool check_wr_encrypt_rd_decrypt(struct xcsdh_host *host, struct mmc_command * cmd, unsigned int flagRdWr)
{
	struct mmc_data * data;
	bool en_de_crypt = false;

	if (!emmcEncryption) {
		return false;
	}

	if (selectedPartition == 0) {
		return false;
	}
	
	switch (cmd->opcode) {
		case 11:
		case 17:
		case 18:
		case 20:
		case 24:
		case 25:
			data = cmd->data;
			if (data) {
				if (data->flags & flagRdWr) {
					en_de_crypt = true;

					if (encryptPartition & (1 << selectedPartition)) {
						ENCRYPT_DEBUG_PRINTK("%s required on partition: %d, cmd:%d, flags:0x%08x\n", 
								flagRdWr & MMC_DATA_WRITE ? "Encryption" : "Decryption",
								selectedPartition, cmd->opcode, data->flags);
					}
					else {
						en_de_crypt = false;
					}
				}
				else {
					// For case MMC Read -> Decryption, we need to start decryption,
					// when MMC read data is available. Here is the synchronization mechanism:
					// 1- mark the transaction for decryption, here
					// 2- in DTO interrupt check this flag, if it is set, fire an 'event'
					// 3- aftre MMC read, wait on a loop for the fired 'event', clear the 'event'
					// 4- do decryption and reset this flag.
					if (data->flags & MMC_DATA_READ) {
						host->decryptFlag = 1;

						// For debugging purpose, remove if, else block for release image
						if (encryptPartition & (1 << selectedPartition)) {
							ENCRYPT_DEBUG_PRINTK("Marked transaction for decryption partition: %d, cmd:%d\n", 
													selectedPartition, cmd->opcode);
						}
						else {
							host->decryptFlag = 0;
						}
					}
				}
			}
			break;
		default:
			break;
	}

	return en_de_crypt;
}

static bool hwLockRequired(struct mmc_command * cmd)
{
	bool req = false;
	
	if (!emmcEncryption || !cmd ||	!(encryptPartition & (1 << selectedPartition))) {
		return false;
	}
	
	switch (cmd->opcode) {
		case 11:
		case 17:
		case 18:
		case 20:
		case 24:
		case 25:
			if (cmd->data) {
				req = true;
			}
			break;
		default:
			break;
	}

	return req;
}

#define ReadBack_Last_Descriptor

// The mapping between scatterlist entries and DMA descriptors is one-to-one
static int prepare_encrypt_decrypt_descriptors(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct xcsdh_host *host = mmc_priv(mmc);
	struct mmc_data *data = cmd->data;
	struct scatterlist *sg = NULL;
	int count = 0;
	int i = 0;
	volatile u32 desc_base = 0;
	u32 sg_length = 0, cmd_specifier;
	struct xcsdh_encrypt_descriptor *pDesc, *pReadBackDesc;
	int result = 0;

	u32 intr_mask = 0;
	
	static int loopLimit = 500000;  // Loop limit while waiting for decrypt data 
	
	if (!data) {
		printk(KERN_ERR "%s NULL data\n", __func__);
		result = -EINVAL;
		goto return_error;
	}

	if (data->sg_len > XCSDH_DESC_NUM) {
		printk(KERN_ERR "%s invalid sg_len: %d - %d\n", __func__, data->sg_len, XCSDH_DESC_NUM);
		result = -EINVAL;
		goto return_error;
	}

	if (!(data->flags & (MMC_DATA_WRITE | MMC_DATA_READ))) {
		printk(KERN_ERR "%s invalid data flags: %#x\n", __func__, data->flags);
		result = -EINVAL;
		goto return_error;
	}

	for_each_sg(data->sg, sg, data->sg_len, count) {
		if (sg->length & 0xF) {
			printk(KERN_ERR "%s invalid length size:%#x cmd:%d\n", __func__, sg->length, cmd->opcode);
			result = -EINVAL;
			goto return_error;		
		}
	}

	if (data->flags & MMC_DATA_READ) {
		// HW lock has already been obtained in the tasklet.
		intr_mask = mmc_encryption_intr_mask();
		mmc_encryption_disable_intr();

		cmd_specifier = CMD_SPECIFIER_AES_IN_PLACE_DECRYPT;
		// Wait for the EVENT_START_DECRYPTION
		if (host->decryptFlag) {
			// the loop limit will be set empirically
			for (i = 0; true; i++) {
				if(test_and_clear_bit(EVENT_START_DECRYPTION, &host->pending_event))
					break;
			}
#if 0
			if (i >= 500000) {
				printk(KERN_ERR "MMC read data not ready for decryption: %d\n", i);
				host->decryptFlag = 0;
				return -EBUSY;
			}
#endif
			if (i > loopLimit) {
				loopLimit = i;
				printk("DE:%d\n", i);
			}
		}
		else {
			printk(KERN_WARNING "%s decryptFlag is zero!\n", __func__);
		}
		host->decryptFlag = 0;		
	}
	else {
		cmd_specifier = CMD_SPECIFIER_AES_IN_PLACE_ENCRYPT;
	}

	PRINT_SCATTER_LIST("Before DMA map", data->sg, data->sg_len);

	count = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));

	PRINT_SCATTER_LIST("After DMA map", data->sg, data->sg_len);

	if (!count) {
		printk(KERN_ERR "%s DMA sg map failed\n", __func__);
		result = -EFAULT;
		goto return_error;
	}

	if (count > XCSDH_DESC_NUM) {
		printk(KERN_ERR "%s invalid sg list size: %d - %d\n", __func__, count, XCSDH_DESC_NUM);
		result = -EINVAL;
		goto return_error;
	}
	
	ENCRYPT_DEBUG_PRINTK("%s CMD %d, count:%d, sg_len:%d\n", __func__, cmd->opcode, count, data->sg_len);

	for (i = 0, pDesc = &host->encrypt_descriptors[0]; i < count; i++, pDesc++) {
		sg = &(data->sg[i]);
		sg_length = sg_dma_len(sg);
		if (sg_length > CMD_SPECIFIER_MAX_BYTE_COUNT) {
			printk(KERN_ERR "%s sg length too big: %#x\n", __func__, sg_length);
			result = -EINVAL;
			goto return_error;
		}
		if (sg_length & BYTE_COUNT_MULTIPLE_16_MASK) {
			printk(KERN_ERR "%s sg length not multiple of 16 bytes: %#x\n", __func__, sg_length);
		}
		pDesc->source_lo = pDesc->destination_lo = sg->dma_address;
		pDesc->source_hi = pDesc->destination_hi = 0;
		pDesc->cmd_specifier = cmd_specifier | sg_length;
		pDesc->crypto_control_word = CRYPTO_CONTROL_WORD_AES_128;
		pDesc->crypto_cw_iv = CRYPTO_CW_IV_MMC_ENCRYPT;
	}

	// Set end-of-list and enable interrupt on last descriptor
	pDesc--;
	pDesc->cmd_specifier |= (CMD_SPECIFIER_EOL | CMD_SPECIFIER_COMPLETION_INTR);

	PRINT_ENCRYPT_DESCRIPTORS(host->encrypt_descriptors, count);

	if (mmc_encryption_intr_pending()) {
		printk("%s INTR pending\n", __func__);
		for (i = 0; !mmc_encryption_dma_done(); i++);
	}
	
	if (!mmc_encryption_dma_ready()) {
		result = -EBUSY;
		goto return_error;
	}

	flush_dcache_range((unsigned long)(host->encrypt_descriptors),
			(unsigned long)(((host->encrypt_descriptors)) + (sizeof(struct xcsdh_encrypt_descriptor) * count)));
	wmb();

	desc_base = virt_to_phys((void*)(&(host->encrypt_descriptors[0])));

	ENCRYPT_DEBUG_PRINTK("Writing 0x%08x to DMA_DQ_PTR0\n", desc_base);

	mmc_encryption_write_dma_desc_queue(desc_base);

#ifdef MMC_ENCRYPTION_POLL_MODE
	for (i = 0; !mmc_encryption_dma_done(); i++);

	ENCRYPT_DEBUG_PRINTK("DMA polling after: %d\n", i);
#endif

	if (data->flags & MMC_DATA_READ ) {
	// Since ARM CPU has direct access to SEQ0, in case of read;
	// DMA needs to make sure the decrypted data is written back to 
	// DDR before being accessed by ARM. In order to do that we need to 
	// read back the last 16 bytes / or all of the last descriptors.
	// This would cause the SEQ0 to flush the data to the DDR
#ifdef ReadBack_Last_Descriptor	
		pReadBackDesc = pDesc + 1;
		pReadBackDesc->source_hi = pReadBackDesc->destination_hi = 0;
		pReadBackDesc->crypto_control_word = pReadBackDesc->crypto_cw_iv = 0;
		cmd_specifier = pDesc->cmd_specifier;

	// Currently we are reading the complete last descriptor's data		
#ifdef ReadBack_Last_16_BYTES
		// sg_length is multiple of 16, it has been checked above.
		sg_length = cmd_specifier & CMD_SPECIFIER_BYTE_COUNT_MASK;
		readBackAddress = pDesc->source_lo + sg_length - 16;
		pReadBackDesc->source_lo = pReadBackDesc->destination_lo = readBackAddress;
		// No encryption
		cmd_specifier &= ~CMD_SPECIFIER_NO_ENCRYPT_MASK;
		// Set the read size to 16 bytes
		cmd_specifier &= ~CMD_SPECIFIER_BYTE_COUNT_MASK;
		cmd_specifier += 16;
#endif	
		cmd_specifier &= ~CMD_SPECIFIER_NO_ENCRYPT_MASK;	
		pReadBackDesc->source_lo = pReadBackDesc->destination_lo = pDesc->source_lo;
		pReadBackDesc->cmd_specifier = cmd_specifier;

		PRINT_ENCRYPT_DESCRIPTORS(pReadBackDesc, 1);
		
		if (!mmc_encryption_dma_ready()) {
			result = -EBUSY;
			goto return_error;
		}

		flush_dcache_range((unsigned long)(pReadBackDesc), sizeof(struct xcsdh_encrypt_descriptor));
		wmb();

		desc_base = virt_to_phys((void*)(pReadBackDesc));

		ENCRYPT_DEBUG_PRINTK("Writing 0x%08x to DMA_DQ_PTR0 for read back\n", desc_base);
#endif

#ifdef ReadBack_All_Descriptors
		for (i = 0, pDesc = &host->encrypt_descriptors[0]; i < count; i++, pDesc++) {
			pDesc->cmd_specifier &= ~CMD_SPECIFIER_NO_ENCRYPT_MASK;
			pDesc->crypto_control_word = pDesc->crypto_cw_iv = 0;
		}

		if (!mmc_encryption_dma_ready()) {
			result = -EBUSY;
			goto return_error;
		}

		flush_dcache_range((unsigned long)(host->encrypt_descriptors),
				(unsigned long)(((host->encrypt_descriptors)) + (sizeof(struct xcsdh_encrypt_descriptor) * count)));
		wmb();

		desc_base = virt_to_phys((void*)(&(host->encrypt_descriptors[0])));
#endif		

		mmc_encryption_write_dma_desc_queue(desc_base);
		
		for (i = 0; !mmc_encryption_dma_done(); i++);
		
		ENCRYPT_DEBUG_PRINTK("DMA polling read back after: %d\n", i);

		MMR_WRITE(intr_mask, XCSDH_ENCRYPT_DMA_INT_MASK_REG);
	}

	dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, ((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));

return_error:
	emmcHw_unlock();
	return result;
}

// This function is called in 
//		- xcsdh_send_cmd() for write encryption,
//		- post_cmd_done() for read decryption
// in both cases the host->lock had been obtained.
static int encrypt_wr_decrypt_rd(struct mmc_host *mmc, struct mmc_command *cmd)
{
	int result = 0;

	result = prepare_encrypt_decrypt_descriptors(mmc, cmd);
	if (result) {
		printk(KERN_ERR "%s failed: %d\n", __func__, result);
		return result;
	}

	return result;
}

#ifndef MMC_ENCRYPTION_POLL_MODE
static void encryption_tasklet(unsigned long data)
{
	
}

static irqreturn_t mmc_encrypt_irq(int irq, void *dev)
{
	struct xcsdh_host *host = dev;
	mmc_encryption_clear_intr();
	
	tasklet_schedule(&host->mmc_encryption_tasklet);

	return(IRQ_HANDLED);
}
#endif

// Called after host controller has been initialized
static void mmc_encryption_init(void)
{
	struct xcsdh_host * pHost = get_xcsdh_host();
	volatile u32 reg;

	atomic_set(&dmaHwLockCounter, 0);

	printk("%s ReadBack_Last_Descriptor %s %s\n", __FUNCTION__, __DATE__, __TIME__);
	if (pHost == NULL) {
		printk(KERN_ERR "%s failed\n", __FUNCTION__);
		return;
	}

	pHost->decryptFlag = 0;

	if (!proc_create(PROC_eMMC_ENCRYPTION, 0, NULL, &proc_eMMC_Encryption_fops)) {
		printk(KERN_ERR "%s: failed to create procfs eMMC encryption\n", __FUNCTION__);
	}

	if (!proc_create(PROC_MMC_ENCRYPT_PARTITION, 0, NULL, &proc_mmc_encrypt_partition_fops)) {
		printk(KERN_ERR "%s: failed to create procfs MMC encrypt partition\n", __FUNCTION__);
	}
	
#ifndef MMC_ENCRYPTION_POLL_MODE
	if (request_irq(XCSDH_ENCRYPTION_IRQ, mmc_encrypt_irq, 0, DMA_ENCRYPTION_NAME, (void *)pHost)) {
		printk(KERN_ERR "%s failed to register interrupt:%d\n", __FUNCTION__, XCSDH_ENCRYPTION_IRQ);
		return;
	}

	mmc_encryption_clear_intr();
	mmc_encryption_enable_intr();

	tasklet_init(&pHost->mmc_encryption_tasklet, encryption_tasklet, (unsigned long)pHost);
#else
//	mmc_encryption_disable_intr();	
#endif

#ifdef MMC_ENCRYPTION_DEBUG
	if (!proc_create(PROC_MMC_ENCRYPT_DEBUG, 0, NULL, &proc_mmc_encrypt_debug_fops)) {
		printk(KERN_ERR "%s: failed to create procfs MMC encrypt debug\n", __FUNCTION__);
	}

	if (!proc_create(PROC_MMC_SELECTED_PARTITION, 0, NULL, &proc_mmc_last_partition_fops)) {
		printk(KERN_ERR "%s: failed to create procfs selected partition\n", __FUNCTION__);
	}
#endif	
	
	printk("MMC encryption is %s during start up.\n", emmcEncryption ? "ENABLED" : "DISABLED");
	if (encryptPartition == ALL_PARTITION_ENCRYPTED) {
		printk("All MMC partitions are encrypted.\n");
	}
	else {
		int b;
		printk("MMC encrypted partition(s) %#x: ", encryptPartition);
		for (b = 1; b < 8; b++) {
			if (encryptPartition & (1 << b)) {
				if (b < 4)
					printk("P%d ", b);
				else
					printk("GP%d ", b - 4);
			}
		}
		printk("\n");
	}

	// Wait for DMA is ready
	if (!mmc_encryption_dma_ready()) {
		printk("%s DMA busy\n", __func__);
	}

	reg = MMR_READ(XCSDH_ENCRYPT_DMA_INT_STATUS_REG);
	if(reg & XCSDH_ENCRYPT_DMA_INT_STATUS_BIT) {
		printk("%s DMS INT status:%#x\n", __func__, reg);
//		mmc_encryption_clear_intr();
	}

	printk("%s DONE\n", __func__);
}

static void mmc_encryption_exit(void)
{
#ifndef MMC_ENCRYPTION_POLL_MODE	
	struct xcsdh_host * pHost = get_xcsdh_host();

	mmc_encryption_disable_intr();
	if (pHost) {
		free_irq(XCSDH_ENCRYPTION_IRQ, (void *)pHost);
	}
#endif	
}

void printCommandHistory(void) 
{
	int i, c;
	MMC_DEBUG_ITEM * pI;
	unsigned long flags;
	struct xcsdh_host * host = get_xcsdh_host();

	spin_lock_irqsave(&host->lock, flags);
	printk("MMC Previous %d commands\n", COMMMAND_HISTORY);
	printk("     CMD     arg        flags     part\n");
	for (c = 0, i = debugIndex; c < COMMMAND_HISTORY; c++) {
		pI = &mmcDebugItems[i];
		printk("%02d- %04d  0x%08x  0x%08x  %02d\n", i, pI->opcode, pI->arg, pI->flags, pI->partition);
		if (--i < 0) {
			i = COMMMAND_HISTORY - 1;
		}
	}
	spin_unlock_irqrestore(&host->lock, flags);
}

void diagMMC(void)
{
//	struct xcsdh_host * pHost = get_xcsdh_host();
	printk("%s\n", __func__);
//	enableMMCDebug = 0x0f;
	printCommandHistory();
}

#else // CONFIG_MMC_XCODE_ENCRYPTION
#define mmc_encryption_init() 
#define mmc_encryption_exit()
#define printMMC_Command(title, state, cmd)
#define check_wr_encrypt_rd_decrypt(host, cmd, flag)	false
#define emmcHw_lock()
#define emmcHw_unlock()
#define track_selected_partition(arg)
#define PRINT_MMC_COMMAND(title, state, cmd)
#define hwLockRequired(cmd)		false
#define ENCRYPT_DEBUG_PRINTK(_fmt, _args...) 
#endif // CONFIG_MMC_XCODE_ENCRYPTION

#ifdef ENABLE_VIRTUAL_SD
/*
 * Virtual SD card simulation
 */

/* Function prototypes */
/* ------------------------------------------------------------ */
static int xcsdh_virt_irq_cmd_finish(struct xcsdh_host *host);
static int xcsdh_virt_irq_data_finish(struct xcsdh_host *host);
static int xcsdh_virt_force_chk_card(struct xcsdh_host *host);
/* ------------------------------------------------------------ */

struct xcsdh_virt_slot_irq xcsdh_virt_slot_irqs = {
	.cmd_finish = xcsdh_virt_irq_cmd_finish,
	.data_finish = xcsdh_virt_irq_data_finish,
	.force_chk_card = xcsdh_virt_force_chk_card,
};

struct xcsdh_virt_slot xcsdh_virt_slot = {
	.irqs = &xcsdh_virt_slot_irqs,
	.host = NULL,
	.vsd_ops = NULL,
	.card = NULL,
};

struct xcsdh_virt_slot *xcsdh_virt_get_slot(void)
{
	if ((&xcsdh_virt_slot) != NULL) {
		return (&xcsdh_virt_slot);
	} else {
		return (NULL);
	}
}
EXPORT_SYMBOL(xcsdh_virt_get_slot);

/* Only support 1 slot, 1 SD
   The caller (virtual SD card) should place the its instance into the
   .card member
   */
struct xcsdh_virt_slot *xcsdh_virt_sd_card_reg(struct xcsdh_virt_slot *slot)
{
	printk("xcsdh_virt_slot.card %p\n", xcsdh_virt_slot.card);
	printk("slot->card %p, slot->vsd_ops %p, slot->vsd_ops->get_card_name %p\n",
			slot->card, slot->vsd_ops, &(slot->vsd_ops->get_card_name));
	if (xcsdh_virt_slot.card == NULL) {
		printk("%s: NO SD card registered !\n", __func__);
		return(NULL);
	} else {
		return(&xcsdh_virt_slot);
	}
}

EXPORT_SYMBOL(xcsdh_virt_sd_card_reg);

static int xcsdh_virt_irq_cmd_finish(struct xcsdh_host *host)
{
	return(0);
}

static int xcsdh_virt_irq_data_finish(struct xcsdh_host *host)
{
	return(0);
}

static int xcsdh_virt_force_chk_card(struct xcsdh_host *host)
{
	printk("%s: host \"%s\" virtual slot check card \"%s\"\n",
			__func__, host->name,
			host->vslot->vsd_ops->get_card_name());

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));

	return (0);
}

#endif //ENABLE_VIRTUAL_SD
/*
 * Virual end here
 */

/* ---------- Function Prototype ---------- */
static u32 xcsdh_ubusy_wait(u32 core, int time_out_cnt);
static int xcsdh_access_smcc_reg(struct xcsdh_host *host, u32 reg, volatile u32 *rdata, u32 wdata, int write);
static int xcsdh_reset_smcc_hw(void);
static int xcsdh_reset_smcc_core(struct xcsdh_host *host);
static int xcsdh_send_cmd(struct mmc_host *mmc, struct mmc_command *cmd);
static void xcsdh_request(struct mmc_host *mmc, struct mmc_request *req);
static void xcsdh_set_ios(struct mmc_host *mmc, struct mmc_ios *ios);
static void xcsdh_set_cap(struct mmc_host *mmc);
static void xcsdh_enable_sdio_irq(struct mmc_host *mmc, int enable);
static int xcsdh_set_power(struct xcsdh_host *host, u32 on);
static void xcsdh_sdio_card_set_irq(int cardnum, int on);
static int xcsdh_check_card_present(struct xcsdh_host *host);
static int xcsdh_enable_dma(struct xcsdh_host *host);
static int xcsdh_set_clk(struct xcsdh_host *host, u32 clk);
static int xcsdh_set_bus_width(struct xcsdh_host *host, u32 w);
static int xcsdh_reset_fifo(struct xcsdh_host *host);
static int xcsdh_wait_ciu(struct xcsdh_host *host, int time_out_cnt);
static irqreturn_t xcsdh_irq_handler(int irq, void *dev_id);
static void xcsdh_tasklet_finish(unsigned long param);
static void xcsdh_cmd_timeout_timer_fn(unsigned long param);
static void xcsdh_data_irq(struct xcsdh_host *host, u32 irq_state);
static int post_data_complete(struct xcsdh_host *host, struct mmc_data *data);
static int post_command_done(struct xcsdh_host *host, struct mmc_command *cmd);

/* ---------- Function Prototype Ends ---------- */



#ifdef XCSDH_DEBUG
/* debug usage */
static void xcsdh_print_board_ver(void)
{
	u32 id = 0;

	id = MMR_READ(RBM_BOARD_ID);
	printk("Board ID Content: 0x%08x\n", id);
	printk("Board ID: 0x%x\n", (id & BOARD_ID_MASK) >> BOARD_ID_SHIFT);
	printk("Tech ID: 0x%x\n", (id & TECH_ID_MASK) >> TECH_ID_SHIFT);
	printk("Rev ID: 0x%x\n", (id & REV_ID_MASK) >> REV_ID_SHIFT);
	id = MMR_READ(CG_DUMMY_REG);
	printk("CG Dummy Reg: 0x08%x\n", id);

	//	printk("Package: 0x%x\n", (id & PACKAGE_MASK) >> PACKAGE_SHIFT);
	//	printk("NVBOOT: 0x%x\n", (id & NVBOOT_MASK) >> NVBOOT_SHIFT);
	//	printk("BOOTROM: 0x%x\n", (id & BOOTROM_MASK) >> BOOTROM_SHIFT);
	//	printk("HOSTSEL: 0x%x\n", (id & HOSTSEL_MASK) >> HOSTSEL_SHIFT);

	return;
}
#endif

static u32 xcsdh_ubusy_wait(u32 core, int time_out_cnt)
{
		u32 csr_status;
	do {
		csr_status = MMR_READ(CORE_REG(core, SMCC_CSR_STATUS_REG));
		if (csr_status & SMCC_CSR_STATUS_REG_CSR_ERR_MASK)
			return 1;

		if (--time_out_cnt == 0){
			ERRMSG("SMCC HW error, check hardware\n");
			return 1;
		}
	} while(csr_status & SMCC_CSR_STATUS_REG_CSR_BUSY_MASK);

	return 0;
}


static u32 xcsdh_read_smcc_reg_locked(struct xcsdh_host *host, u32 reg)
{
    u32 err;
    int time_out_cnt = 100;
    u32 csr_ctrl_reg, data = 0xFFFFFFFF;

    do
    {
        err = xcsdh_ubusy_wait(host->core_id, time_out_cnt);
        if (err) {
            WARN_ONCE(1, "ubusy_wait failed\n");
            break;
        }

        csr_ctrl_reg = reg;

        MMR_WRITE(csr_ctrl_reg, CORE_REG(host->core_id, SMCC_CSR_CTRL_REG));
        err = xcsdh_ubusy_wait(host->core_id, time_out_cnt);
        if (err) {
            WARN_ONCE(1, "ubusy_wait failed\n");
            break;
        }

        data = MMR_READ(CORE_REG(host->core_id, SMCC_RDAT_REG));
    }   while (0);

    return data;
}

static void xcsdh_write_smcc_reg_locked(struct xcsdh_host *host, u32 reg, u32 data)
{
    u32 err;
    int time_out_cnt = 100;
    u32 csr_ctrl_reg;

    do
    {
        err = xcsdh_ubusy_wait(host->core_id, time_out_cnt);
        if (err) {
            WARN_ONCE(1, "ubusy_wait failed\n");
            break;
        }

        csr_ctrl_reg = (host->core_id == 0) ? ( 1 << SMCC_CSR_CTRL_REG_WOP_SHIFT) : (1 << SMCC_CSR_CTRL_REG1_WOP1_SHIFT);
        csr_ctrl_reg |= reg;

        MMR_WRITE(data, CORE_REG(host->core_id, SMCC_WDAT_REG));
        MMR_WRITE(csr_ctrl_reg, CORE_REG(host->core_id, SMCC_CSR_CTRL_REG));
        err = xcsdh_ubusy_wait(host->core_id, time_out_cnt);
        if (err) {
            WARN_ONCE(1, "ubusy_wait failed\n");
            break;
        }
    }   while (0);

    return;
}

static u32 xcsdh_read_smcc_reg(struct xcsdh_host *host, u32 reg)
{
    unsigned long flags;
    u32 data;

    spin_lock_irqsave(&host->iplock, flags);
    data = xcsdh_read_smcc_reg_locked(host, reg);
    spin_unlock_irqrestore(&host->iplock, flags);
    return data;
}

static void xcsdh_write_smcc_reg(struct xcsdh_host *host, u32 reg, u32 data)
{
    unsigned long flags;

    spin_lock_irqsave(&host->iplock, flags);
    xcsdh_write_smcc_reg_locked(host, reg, data);
    spin_unlock_irqrestore(&host->iplock, flags);
    return;
}

static int xcsdh_access_smcc_reg(struct xcsdh_host *host, u32 reg, volatile u32 *rdata, u32 wdata, int write)
{
	u32 beef = 0;
	int err = 0;

    if (write) {
        xcsdh_write_smcc_reg(host, reg, wdata);
    }
    else {
        beef = xcsdh_read_smcc_reg(host, reg);
        *rdata = beef;
    }

	return (err);
}

/* 
*   This function is used in the kernel start up stage for direct access to core 0 before
*   it can be initialized.
*
*   This is done for boot time optimization of the system.
*/
static u32 xcsdh_read_smcc_reg_direct(u32 reg)
{
    unsigned long flags;
    u32 data;

    spin_lock_irqsave(&iplock, flags);
    data = xcsdh_read_smcc_reg_locked(&direct_host, reg);
    spin_unlock_irqrestore(&iplock, flags);

    return data;
}

static int xcsdh_wait_ciu(struct xcsdh_host *host, int time_out_cnt)
{
	u32 reg = 0;
	int err = 0;
	u32 start_cmd_bmask = 1 << SMCC_CMD_START_CMD_SHIFT;

	/* start_cmd bit will be 0 if taken by CIU */
	do {
		err = xcsdh_access_smcc_reg(host, SMCC_CMD, &reg, 0, 0);
		if (err)
			return (-1);

		if (--time_out_cnt == 0) {
			ERRMSG("Wait CIU timedout. CMD 0x%x\n", reg);
		return (-1);
	}
	} while (reg & start_cmd_bmask);

	return 0;
}

/*
 * XCode6 enable host controllers
 * Use Board_ID variable to determine the supportness of SD 0 and/or SD 1
 */
static int xcsdh_enable_host(void)
{
#ifdef CONFIG_PLAT_XCODE68xx
	/* disbale SD host 1 for Enginneering board */
	u32 board_id = MMR_READ(CG_DUMMY_REG1);
	volatile u32 tmp;

	switch (board_id) {
	case 0x1200:
	case 0x1201:
	case 0x1500:
	case 0x1501:
		return 2;

	case 0x1400:
	case 0x1401:
		return 1;

	case 0x0030:
	case 0x0031:
	case 0x0032:
	case 0x0033:
		/* for SDK board GIOP-9 low TS module pluged no SD host 1*/
		tmp = MMR_READ(GPIO_DEDICATED_IN) & (1 << 9);
		if(tmp)
			return 2;
		else
			return 0;
	default:
		return 0;
	}
#else
	return 2;
#endif
}

static void smcc_register_dump_all(void)
{
	int i, core;
	int err;
	u32 rintsts;
	struct xcsdh_host *host;

		printk("IP registers:\n");

	for(core=0;core<2;core++)
	{
		printk("Core %d:\n", core);
		host = master->sd_host[core];
		for(i=0; i<=0x98;i+=4)
		{
			if(i==0x7c)
				continue;
			err = xcsdh_access_smcc_reg(host, i, &rintsts, 0, 0);
			printk("0x%04x = 0x%08x\n", i, rintsts);
		}
			err = xcsdh_access_smcc_reg(host, 0x100, &rintsts, 0, 0);
			printk("0x0100 = 0x%08x\n", rintsts);
			err = xcsdh_access_smcc_reg(host, 0x104, &rintsts, 0, 0);
			printk("0x0104 = 0x%08x\n", rintsts);
	}
		printk("Xcode registers:\n");
		i=MMR_READ(SMCC_CSR_STATUS_REG );
		printk("SMCC_CSR_STATUS_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_CSR_CTRL_REG );
		printk("SMCC_CSR_CTRL_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_WDAT_REG );
		printk("SMCC_WDAT_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_RDAT_REG );
		printk("SMCC_RDAT_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_STATUS_REG );
		printk("SMCC_STATUS_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_IP_RAW_INT_REG );
		printk("SMCC_IP_RAW_INT_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_IP_RAW_INT_MASK_REG );
		printk("SMCC_IP_RAW_INT_MASK_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_INT_EN_REG );
		printk("SMCC_INT_EN_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_CTRL_REG );
		printk("SMCC_CTRL_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_NEG_SAMPL_REG );
		printk("SMCC_NEG_SAMPL_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_NEG_DRV_REG );
		printk("SMCC_NEG_DRV_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_DEBUG_REG );
		printk("SMCC_DEBUG_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_DUMMY_REG );
		printk("SMCC_DUMMY_REG = 0x%08x\n", i);
		i=MMR_READ(SMCC_CSR_STATUS_REG1 );
		printk("SMCC_CSR_STATUS_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_CSR_CTRL_REG1 );
		printk("SMCC_CSR_CTRL_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_WDAT_REG1 );
		printk("SMCC_WDAT_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_RDAT_REG1 );
		printk("SMCC_RDAT_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_STATUS_REG1 );
		printk("SMCC_STATUS_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_IP_RAW_INT_REG1 );
		printk("SMCC_IP_RAW_INT_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_IP_RAW_INT_MASK_REG1 );
		printk("SMCC_IP_RAW_INT_MASK_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_INT_EN_REG1 );
		printk("SMCC_INT_EN_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_CTRL_REG1 );
		printk("SMCC_CTRL_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_NEG_SAMPL_REG1 );
		printk("SMCC_NEG_SAMPL_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_NEG_DRV_REG1 );
		printk("SMCC_NEG_DRV_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_DEBUG_REG1 );
		printk("SMCC_DEBUG_REG1 = 0x%08x\n", i);
		i=MMR_READ(SMCC_DUMMY_REG1 );
		printk("SMCC_DUMMY_REG1 = 0x%08x\n", i);
}

static int xcsdh_smcc_fbe_reset(struct xcsdh_host *host)
{
	volatile u32 reg;
	int err;

	/* reset dma */
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	set_bit(2, (unsigned long *)&reg);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, reg, 1);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	while ((reg & (1 << SMCC_CTRL_DMA_RESET_SHIFT)) != 0) {
		udelay (10);
		err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg,
				0, 0);
	}

	/* reset fifo */
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	set_bit(1, (unsigned long *)&reg);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, reg, 1);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	while ((reg & (1 << SMCC_CTRL_FIFO_RESET_SHIFT)) != 0) {
		udelay (10);
		err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg,
				0, 0);
	}


	/* reset controller */
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	set_bit(0, (unsigned long *)&reg);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, reg, 1);
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg, 0, 0);
	while ((reg & (1 << SMCC_CTRL_CONTROLLER_RESET_SHIFT)) != 0) {
		udelay (10);
		err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &reg,
				0, 0);
	}

	/* clear all IRQ status */
	xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, 0xffffffff, 1);
	xcsdh_access_smcc_reg(host, SMCC_IDSTS, NULL, 0xffffffff, 1);

	switch (host->core_id) {
		case 0:
			MMR_WRITE(0xffff, SMCC_STATUS_REG); /* write to clear */
			break;
		case 1:
			MMR_WRITE(0xffff, SMCC_STATUS_REG1); /* write to clear */
			break;
	}


	return (err);
}

static int xcsdh_reset_smcc_core(struct xcsdh_host *host)
{
	volatile u32 reg;
	int core_id = host->core_id;

	switch (core_id) {
		case 0:
			/* host 0 */
			reg = MMR_READ(SMCC_CTRL_REG);
			reg |= (SMCC_CTRL_REG_SOFT_RST_MASK);
			MMR_WRITE(reg, SMCC_CTRL_REG);
			udelay(100);

			reg = MMR_READ(SMCC_CTRL_REG);
			reg &= ~(SMCC_CTRL_REG_SOFT_RST_MASK);
			MMR_WRITE(reg, SMCC_CTRL_REG);
			udelay(100);

			xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, 0xffffffff, 1);
			xcsdh_access_smcc_reg(host, SMCC_IDSTS, NULL, 0xffffffff, 1);
			MMR_WRITE(0xffff, SMCC_STATUS_REG); /* write to clear */
			MMR_WRITE(0, SMCC_INT_EN_REG);
			break;

		case 1:
			/* host 1 */
			reg = MMR_READ(SMCC_CTRL_REG1);
			reg |= (SOFT_RST1_MASK);
			MMR_WRITE(reg, SMCC_CTRL_REG1);
			udelay(100);

			reg = MMR_READ(SMCC_CTRL_REG1);
			reg &= ~(SOFT_RST1_MASK);
			MMR_WRITE(reg, SMCC_CTRL_REG1);
			udelay(100);

			xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, 0xffffffff, 1);
			xcsdh_access_smcc_reg(host, SMCC_IDSTS, NULL, 0xffffffff, 1);
			MMR_WRITE(0xffff, SMCC_STATUS_REG1); /* write to clear */
			MMR_WRITE(0, SMCC_INT_EN_REG1);
			break;
	}

	return (0);
}

static int xcsdh_reset_smcc_hw(void)
{
    volatile u32 reg;
	volatile u32 acc_reset, reg_clk_stop, reg_blk_clk_stop, reg_clk_src_en;
    u32	temp = 0;

    if (xcsdh_enable_host() == 2) {
        init_core0 = 1;
        init_core1 = 1;
    } else if (xcsdh_enable_host() == 1) {
        init_core1 = 1;
    } else {
		init_core0 = 1;
	}
    
#ifdef CONFIG_PLAT_XCODE64xx
	reg_clk_src_en = MMR_READ(CG1_CLK_SRC_EN1);
    reg_clk_stop = MMR_READ(CG1_CLK_STOP1);
    reg_blk_clk_stop = MMR_READ(ACC_BLK_STOP0);
	acc_reset = MMR_READ(ACC_RESET_REG0);

	if(init_core0) {
		reg = MMR_READ(GPIO_C_CTRL);
		reg &= ~GPIO_C_CTRL_GPIO_MODE_SEL0_MASK;
		MMR_WRITE(reg, GPIO_C_CTRL);
		reg_clk_src_en |= CG1_CLK_SRC_EN1_SMCC0CLK_SRC_EN_MASK;
		reg_clk_stop &= ~SMCC0CLK_STOP_MASK;
		reg_blk_clk_stop &= ~SMCC0_BLK_STOP_MASK;
		acc_reset &= ~SMCC0_RESET_MASK;
	}

	if(init_core1) {
		reg = MMR_READ(RBM_PADU_CTRL);
		reg |= (PADU_CTRL_SD1_MASK);
		reg &= ~RBM_PADU_CTRL_PADU_CTRL_GPIO_MASK;
		MMR_WRITE(reg, RBM_PADU_CTRL);
		reg_clk_src_en |= CG1_CLK_SRC_EN1_SMCC1CLK_SRC_EN_MASK;
		reg_clk_stop &= ~SMCC1CLK_STOP_MASK;
		reg_blk_clk_stop &= ~SMCC1_BLK_STOP_MASK;
		acc_reset &= ~SMCC1_RESET_MASK;
	}

	temp = MMR_READ(CG_DUMMY_REG) & 0xFF00;

	MMR_WRITE(reg_clk_src_en, CG1_CLK_SRC_EN1);
    MMR_WRITE(reg_clk_stop, CG1_CLK_STOP1);
    MMR_WRITE(reg_blk_clk_stop, ACC_BLK_STOP0);
	udelay(100);
    MMR_WRITE(reg, ACC_RESET_REG0);
#else
	reg_clk_src_en = MMR_READ(CG1_CLK_SRC_EN0);
    reg_clk_stop = MMR_READ(CG1_CLK_STOP0);
    reg_blk_clk_stop = MMR_READ(ACC_BLK_STOP0);
	acc_reset = MMR_READ(ACC_RESET_REG0);

	if(init_core0) {
		reg = MMR_READ(GPIO_C_CTRL);
		reg &= ~GPIO_C_CTRL_GPIO_MODE_SEL_MASK;
		MMR_WRITE(reg, GPIO_C_CTRL);
		reg_clk_src_en |= CG1_CLK_SRC_EN0_SMCC0CLK_SRC_EN_MASK;
		reg_clk_stop &= ~SMCC0CLK_STOP_MASK;
		reg_blk_clk_stop &= ~SMCC0_BLK_STOP_MASK;
		acc_reset &= ~SMCC0_RESET_MASK;
	}

	if(init_core1) {
		reg = MMR_READ(RBM_PADU_CTRL);
		reg |= (PADU_CTRL_SD1_MASK);
		MMR_WRITE(reg, RBM_PADU_CTRL);
		reg_clk_src_en |= CG1_CLK_SRC_EN0_SMCC1CLK_SRC_EN_MASK;
		reg_clk_stop &= ~SMCC1CLK_STOP_MASK;
		reg_blk_clk_stop &= ~SMCC1_BLK_STOP_MASK;
		acc_reset &= ~SMCC1_RESET_MASK;
	}

	temp = MMR_READ(CG_DUMMY_REG) & 0xFF00;

	MMR_WRITE(reg_clk_src_en, CG1_CLK_SRC_EN0);
    MMR_WRITE(reg_clk_stop, CG1_CLK_STOP0);
    MMR_WRITE(reg_blk_clk_stop, ACC_BLK_STOP0);
	udelay(100);
    MMR_WRITE(reg, ACC_RESET_REG0);
#endif
    return(0);
}

#if 0
static int xcsdh_poll_rsp(struct mmc_host *mmc,
		struct mmc_request *req,
		int time_out_cnt)
{
	u32 rintsts = 0xffffffff;
	u32 rsp0 = 0;
	int err;
	int rsp_err = 0;
	u32 cmd_done_mask = 1 << SMCC_RINTSTS_CD_SHIFT;
	u32 data_done_mask = 1 << SMCC_RINTSTS_DTO_SHIFT;
	u32 timeout_mask = SMCC_RINTSTS_HTO_MASK |
		SMCC_RINTSTS_DRTO_MASK |
		SMCC_RINTSTS_RTO_MASK;
	int i = 0;
	struct xcsdh_host *host = NULL;
	struct mmc_data *data;

	host = mmc_priv(mmc);

	data = req->data;

	while (i < time_out_cnt) {
		err = xcsdh_access_smcc_reg(host, SMCC_RINTSTS, &rintsts,
				0,0);
		if (err)
			break;

		if ((rintsts & cmd_done_mask) != 0)
			break;

		i++;
		udelay(10);
	}
	DPRINTK("rintsts 0x%08x\n", rintsts);

	/* XXX: set better ERRNO! */
	if (i >= time_out_cnt) {
		err = -1;
		goto out1;
	}

	if (err) {
		goto out1;
	}

	/* XXX handle other err too */
	/* irq error status */
	if (rintsts & timeout_mask) {
		req->cmd->error = MMC_ERR_TIMEOUT;
		rsp_err = -ETIME;
		goto out1;
	}

	/* We safe here */
	req->cmd->error = MMC_ERR_NONE;

	if (req->cmd->flags & MMC_RSP_PRESENT) {
		if (req->cmd->flags & MMC_RSP_136) {
			for (i=0; i<4; i++) {
				err = xcsdh_access_smcc_reg(host, (SMCC_RESP0 + (i*4)),
						&rsp0, 0, 0);
				if (err)
					goto out1;

				/* XXX:
				 * mmc core expect the lowest bit at
				 * resp[3]
				 */
				req->cmd->resp[3-i] = rsp0;

				DPRINTK("rsp[%d] = 0x%08x\n",
						3 - i,
						req->cmd->resp[3-i]);

			}
		} else {
			err = xcsdh_access_smcc_reg(host, SMCC_RESP0, &rsp0, 0, 0);
			if (err) {
				goto out1;
			}
			DPRINTK("rsp0 = 0x%08x\n", rsp0);

			req->cmd->resp[0] = rsp0;
		}
	}

	/* Data cmd */
	if (req->cmd->data != NULL) {
		if ((req->cmd->data->flags & MMC_DATA_READ) ||
				(req->cmd->data->flags & MMC_DATA_WRITE) ) {

			i = 0;
			while (i < time_out_cnt) {
				err = xcsdh_access_smcc_reg(host, SMCC_RINTSTS, &rintsts,
						0,0);
				if (err) {
					rsp_err = err;
					break;
				}

				if ((rintsts & data_done_mask) != 0)
					break;

				i++;
				udelay(10);
			}

			DPRINTK("rintsts data 0x%08x, err %d\n", rintsts, err);
			if (i >= time_out_cnt) {
				DPRINTK("DATA TIMEOUT\n");
				rsp_err = -ETIME;
				err = rsp_err;
				req->cmd->error = MMC_ERR_TIMEOUT;
				goto out1;
			}

#if 0
			if (req->cmd->data->flags & MMC_DATA_READ) {
				PRINT_16BYTES(host->desc.buf0);
			}

			inv_dcache_range((unsigned long)host->desc.buf0, 512);
#endif

			//			dma_unmap_sg(mmc->dev, data->sg, data->sg_len,
			//				     ((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));
			dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len,
					((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));


			/* let upper our data count */
			data->bytes_xfered = data->blksz * data->blocks;
		}
	}


out1:
	/* clear RINTSTS */
	rintsts = 0xffffffff;
	err = xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, rintsts, 1);
	if (err)
		goto out;

out:
	if (rsp_err)
		err = rsp_err;

	return (err);

}
#endif

#define CHAIN_MODE

#ifdef CHAIN_MODE
#define DESC_BUF_SIZE (4096)
#else
#define DESC_BUF1_SIZE (4096)
#define DESC_BUF2_SIZE (4096)
#define DESC_BUF_SIZE (DESC_BUF1_SIZE+DESC_BUF2_SIZE)
#endif

static noinline int xcsdh_prepare_data(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct xcsdh_host *host = NULL;
	int err = 0;
	int count = 0;
	int i = 0, j;
	struct mmc_data *data = cmd->data;
	struct scatterlist *sg = NULL;
	volatile u32 desc_base = 0;
	u32 data_len = 0;
	u32 seg_len = 0;
	int desc_nr=0;

	host = mmc_priv(mmc);

	if (data == NULL) {
		err = -EINVAL;
		goto out;
	}
	
	host->use_desc=1;

	count = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));

	DPRINTK("count = %d, sg_len = %d, sg_dma_len = %u\n",
			count, data->sg_len, sg_dma_len(data->sg));

	/* write something in poll register to kick off the state machine */
	//xcsdh_access_smcc_reg(SMCC_PLDMND, NULL, 0xffffffff, 1);

	data_len = cmd->data->blksz * cmd->data->blocks;

	DPRINTK("cmd->opcode %d arg 0x%08x blksz 0x%x blocks %d\n", cmd->opcode, cmd->arg, cmd->data->blksz, cmd->data->blocks);
	for (i = 0; i < count; i++) {
		u32 desc_per_block, offset=0;

		sg = &(data->sg[i]);
		seg_len = sg_dma_len(sg);
		desc_per_block=(seg_len+DESC_BUF_SIZE-1)/(DESC_BUF_SIZE);

		if((desc_nr+desc_per_block)>XCSDH_DESC_NUM)
		{
			printk("XCSDH: Block layer passing data larger than descriptors can hold\n");
			err=count-i;
			break;
		}

		for(j=0;j<desc_per_block;j++)
		{
			u32 hbuf;

			host->desc[desc_nr].d0 =
#ifdef CHAIN_MODE
				SMCC_DESC0_CH_MASK |
#endif
				SMCC_DESC0_OWN_MASK;

			if (desc_nr == 0)
				host->desc[desc_nr].d0 |= SMCC_DESC0_FS_MASK;

 			host->desc[desc_nr].d0 |= SMCC_DESC0_DIC_MASK;

			hbuf = min((u32)(seg_len-offset), (u32)DESC_BUF_SIZE);

			host->desc[desc_nr].d2 = (sg_dma_address(sg)+offset);
			WARN_ON(host->desc[desc_nr].d2 & 0x1f);
#ifdef CHAIN_MODE
			host->desc[desc_nr].d1 = hbuf;
			host->desc[desc_nr].d3 = (u32)virt_to_phys((void*)(&(host->desc[desc_nr+1])));
#else
			if(hbuf>DESC_BUF1_SIZE)
			{
				host->desc[desc_nr].d1 = DESC_BUF1_SIZE | ((hbuf - DESC_BUF2_SIZE) << 13);
				host->desc[desc_nr].d3 = host->desc[desc_nr].d2 + DESC_BUF1_SIZE;
			}
			else
			{
				int size1, size2;
				/* JASON XXX:
				 * The hardware seems do not like a
				 * single buffer descriptor while
				 * other descriptors are using dual buffers
				 * If the DMA segment is smaller than 4K,
				 * we spilt it into 2 smaller buffers in this
				 * descriptor. Need some hacks.
				 */
				size1=hbuf>>1;
				size1=(size1+cmd->data->blksz-1)&~(cmd->data->blksz-1);
				size2=hbuf-size1;

				host->desc[desc_nr].d1 = (size2<<13) | size1;
				host->desc[desc_nr].d3 = host->desc[desc_nr].d2 + size1;
			}
#endif

			offset+=hbuf;

#if 0
			/* This checking is not necessary for Viper and Corsa as the last page is reserved to fix oth     er hardware issue */

			/* DMA die at this addr?? */
			/* SDHC hardware will prefech the data
			 * and if the kernel memory is on the
			 * boundary the prefech can over the mem
			 * address limit. Catch this error and tell
			 * user
			 */
			if (host->desc[desc_nr].d2 == 0x8fffe000) {
				printk("See 0x8fffe000, now d2 0x%08x, d3 0x%08x, we are going to die\n",
						host->desc[desc_nr].d2, host->desc[desc_nr].d3);
			}
#endif
			desc_nr++;
			if(desc_nr>XCSDH_DESC_NUM)
			{
				unsigned int k;
				struct scatterlist *sg;
				u32 total_sg_len=0;

				for_each_sg(data->sg, sg, data->sg_len, k)
					total_sg_len += sg->length;
				ERRMSG("XSDUH: Warning! Descriptor not enough. SG count=%d, Total SG len=%d, Number of block=%d, Blksz=%d\n", count, total_sg_len, host->data->blocks,host->data->blksz);
				for_each_sg(data->sg, sg, data->sg_len, k)
					printk("SG segment %d, address 0x%08x len %d\n", k, sg_dma_address(sg), sg->length);
			}
		}
	}
	if(desc_nr>0)
	{
		host->desc[desc_nr-1].d0 |= SMCC_DESC0_ER_MASK | SMCC_DESC0_LD_MASK;
		host->desc[desc_nr-1].d0 &= ~SMCC_DESC0_DIC_MASK;

		/* Let the hardware see the descripters */
		flush_dcache_range((unsigned long)(host->desc),
			(unsigned long)(((host->desc)) + (sizeof(struct xcsdh_idmac_desc) * desc_nr)));
		wmb();
	}
	host->desc_nr = desc_nr;

	for(j=0;j<desc_nr;j++)
		DPRINTK("host->desc[%d].desc[0123] 0x%08x 0x%08x 0x%08x 0x%08x\n",
				j,
				host->desc[j].d0,
				host->desc[j].d1,
				host->desc[j].d2,
				host->desc[j].d3);

	desc_base = virt_to_phys((void*)(&(host->desc[0])));
	DPRINTK("dsec_base = 0x%08x, host->desc[0] = 0x%08x\n",
			desc_base, &(host->desc[0]));

	xcsdh_access_smcc_reg(host, SMCC_DBADDR, NULL, desc_base, 1);

out:
	return (err);
}

void check_count(char *func, int line)
{
#ifdef XCSDH_DEBUG
	int err1, err2;
	u32 tcbcnt, tbbcnt;

	err1 = xcsdh_access_smcc_reg(master->sd_host[0], SMCC_TCBCNT, &tcbcnt, 0, 0);
	err2 = xcsdh_access_smcc_reg(master->sd_host[0], SMCC_TBBCNT, &tbbcnt, 0, 0);

	if(tcbcnt>tbbcnt)
	{
		printk("========%x %x======= %d %d ====%s %d============\n", tcbcnt, tbbcnt, err1, err2, func, line);
		smcc_register_dump_all();
		printk("===================================================\n");
	}
#endif
}

static void xcsdh_err_dump(void)
{
	struct xcsdh_host *host;
	int i;

	printk("=============================================================\n");
	smcc_register_dump_all();

	host = mmc_priv(xcsdh_mmc_host);
	if(host && !in_irq() && !in_softirq())
		inv_dcache_range((unsigned long)(host->desc),
			(unsigned long)(((host->desc)) + (sizeof(struct xcsdh_idmac_desc) * host->desc_nr)));

	if(host->cmd)
	{
		printk("host->cmd->opcode %d arg 0x%08x\n", host->cmd->opcode, host->cmd->arg);
		if(host->cmd->data)
			printk("blksz 0x%x blocks %d\n", host->cmd->data->blksz, host->cmd->data->blocks);
	}
	for(i=0;i<host->desc_nr;i++)
	{
		printk("Desc %d: ", i);
		printk("%08x %08x %08x %08x\n", host->desc[i].d0, host->desc[i].d1, host->desc[i].d2, host->desc[i].d3);
	}
	printk("REQ %x USE_DESC %x CD %x DTO %x\n", host->request_start, host->use_desc,  host->cd_received, host->dto_received);

	printk("=============================================================\n");
	return;
}
/*
 * called by the function mmc_wait_cmd_done,
 * it is in the early stage of kernel init.
 * only applied on host 0, the "direct_host"
 */
static noinline int xcsdh_send_stop(void)
{
	u32 reg = 0;
	u32 flags = 0;
	u32 opcode;
	u32 arg;
	int err = 0;
	err = xcsdh_wait_ciu(&direct_host, 1000);
	if (err)
	{
		err=-1000;
		goto out;
	}

	reg = opcode = 12;
	arg = 0;

	reg = 1 << SMCC_CMD_START_CMD_SHIFT;


	flags = 1 << SMCC_CMD_RSP_EXP_SHIFT;


	reg |= flags |
    	1 << SMCC_CMD_USE_HOLD_REG_SHIFT |
//		1 << SMCC_CMD_WAIT_PRVDATA_CMP_SHIFT |
		//		1 << SMCC_CMD_CHK_RSP_CRC_SHIFT |
		opcode;


	/* DB p178 */
	err = xcsdh_access_smcc_reg(&direct_host, SMCC_CMDARG, NULL, arg, 1);
	if (err)
	{
		err = -1004;
		goto out;
	}

	//printk("====>>>> send cmd opcode %d :write cmd reg: 0x%08x, arg: 0x%08x\n", cmd->opcode, reg, arg);

	err = xcsdh_access_smcc_reg(&direct_host, SMCC_CMD, NULL, reg, 1);
	if (err)
	{
		err = -1005;
		goto out;
	}

	err = xcsdh_wait_ciu(&direct_host, 1000);
	if (err)
	{
		err = -1006;
		goto out;
	}


	/* disable and reset the iDMA */
	xcsdh_access_smcc_reg(&direct_host, SMCC_CTRL,&reg, 0,0);
	reg &= ~SMCC_CTRL_USE_IDMAC;
	reg |= SMCC_CTRL_DMA_RESET;
	xcsdh_access_smcc_reg(&direct_host, SMCC_CTRL, NULL, reg, 1);

	/* Stop the iDMA */
	xcsdh_access_smcc_reg(&direct_host, SMCC_BMOD, &reg, 0, 0);
	reg &= ~SMCC_BMOD_DE;
	reg |= SMCC_BMOD_SWR;
	xcsdh_access_smcc_reg(&direct_host, SMCC_BMOD, NULL, reg, 1);

#ifdef VIRT_SLOT
	/* send to virtual slot */
	if (xcsdh_virt_slot.card != NULL) {
		xcsdh_virt_slot.vsd_ops->write_cmd(mmc, host->mrq);
	}
#endif

	return (0);

out:
#ifdef XCSDH_DEBUG
	xcsdh_err_dump();
#endif
	return(err);
}

static noinline int xcsdh_send_cmd(struct mmc_host *mmc, struct mmc_command *cmd)
{
	u32 reg = 0;
	u32 flags = 0;
	u32 opcode;
	u32 arg;
	volatile u32 sts_reg, data_busy = 0;
	u32 busy_cnt = 0;
	int err = 0;
	struct xcsdh_host *host;

	host = mmc_priv(mmc);

	DPRINTK("opcode %d\n", cmd->opcode);
	PRINT_MMC_COMMAND("Send", host->state, cmd);
	
#ifdef CONFIG_MMC_XCODE_ENCRYPTION
	if (cmd->opcode == MMC_CMD_6_SWITCH) {
		track_selected_partition(cmd->arg);
	}

	if (check_wr_encrypt_rd_decrypt(host, cmd, MMC_DATA_WRITE)) {
		ENCRYPT_DEBUG_PRINTK("%s start write encryption\n", __func__);
		encrypt_wr_decrypt_rd(mmc, cmd);
	}

	recordCommand(cmd);
#endif
	
	err = xcsdh_wait_ciu(host, 1000);
	if (err)
	{
		err=-1000;
		goto out;
	}
	TRACE;
	reg = opcode = cmd->opcode;
	arg = cmd->arg;

	if (opcode == 0) {
		/* special init bit for CMD0 */
		reg = 1 << SMCC_CMD_START_CMD_SHIFT |
			1 << SMCC_CMD_SEND_INIT_SHIFT;
	} else {
		reg = 1 << SMCC_CMD_START_CMD_SHIFT;
	}

	if (opcode == SD_SWITCH_VOLTAGE)
		ERRMSG("CMD11 support not implemented\n");

	if (cmd->flags & MMC_RSP_PRESENT) {
		flags = 1 << SMCC_CMD_RSP_EXP_SHIFT;
	}
	if (cmd->flags & MMC_RSP_136) {
		flags = 1 << SMCC_CMD_RSP_LEN_SHIFT |
			1 << SMCC_CMD_RSP_EXP_SHIFT;
	}

	reg |= flags |
    	1 << SMCC_CMD_USE_HOLD_REG_SHIFT |
//		1 << SMCC_CMD_WAIT_PRVDATA_CMP_SHIFT |
		//		1 << SMCC_CMD_CHK_RSP_CRC_SHIFT |
		opcode;
	TRACE;

	/* Data R/W CMD? */
	if (cmd->data != NULL) {
		DPRINTK("data->blksz %d, blocks %d, flags 0x%x\n",
				cmd->data->blksz, cmd->data->blocks,
				cmd->data->flags);

		xcsdh_prepare_data(mmc, cmd);

		WARN_ON((cmd->data->blksz * cmd->data->blocks)==0);
		err = xcsdh_access_smcc_reg(host, SMCC_BYTCNT, NULL,
				(cmd->data->blksz * cmd->data->blocks), 1);
		if (err)
		{
			err=-1001;
			goto out;
		}

		err = xcsdh_access_smcc_reg(host, SMCC_BLKSIZ, NULL,
				cmd->data->blksz, 1);
		if (err)
		{
			err=-1002;
			goto out;
		}

		if (opcode != 52 && opcode != 53 && opcode != 18 && opcode != 25) {
			/* XXX: check is this a SD card? */
			/* assume block > 1 is using multi-block commands */
			if (cmd->data->blocks > 1) {
				reg |= 1 << SMCC_CMD_SEND_AUTO_STOP_SHIFT;
			}
		}
		TRACE;

#ifdef CONFIG_VIXS_CPRM
		//ichan: this bit will cause data time out (hw issue?)
		//only special handle for 55, 43-48 command
		if (opcode == 55 ||( opcode >= 43 && opcode <= 48))
		{
			reg &= ~(1<<SMCC_CMD_SEND_AUTO_STOP_SHIFT);
		}
#endif

		reg |= 1 << SMCC_CMD_DATA_EXP_SHIFT;
		if (cmd->data->flags & MMC_DATA_WRITE)
			reg |= 1 << SMCC_CMD_DATA_RW_SHIFT;

		/* XXX:
		 * Check the data access size and boundary!
		 */
		DPRINTK("mmc->ocr 0x%08x\n", mmc->ocr);
		if (mmc->ocr & MMC_OCR_CCC) {
			/* SDHC, count with block (512) */
			arg = arg >> 9;
		}
#define BUSY_CNT_MAX 30000
		/* XXX: the DATA line do not sync */
		for (busy_cnt = 0; busy_cnt < BUSY_CNT_MAX; busy_cnt++) {
			err = xcsdh_access_smcc_reg(host, SMCC_STATUS, &sts_reg, 0, 0);
			data_busy = sts_reg & (1 << SMCC_STATUS_DATA_BUSY_SHIFT);
			if (data_busy == 0)
				break;

			udelay(10);
		}

		if(busy_cnt > 10000)
			WARNMSG("host data busy busy_cnt = %d, smcc status = 0x%08x\n", busy_cnt, (unsigned)sts_reg);

		if (busy_cnt > BUSY_CNT_MAX) {
			err = -1003;
			goto out;
		}
	}
	TRACE;

	/* Specially handle the SDIO cmd52,53
	 * They can write register without attaching
	 * data buffer
	 */

	switch (opcode) {
		case 52:
			if ((cmd->arg & 0x80000000) != 0) {
				/* Bit32 RW bit on, write */
				reg |= 1 << SMCC_CMD_DATA_RW_SHIFT;
			}
			break;
		case 53:
			break;
	}
	TRACE;

	/* DB p178 */
	err = xcsdh_access_smcc_reg(host, SMCC_CMDARG, NULL, arg, 1);
	if (err)
	{
		err = -1004;
		goto out;
	}
	TRACE;

	//printk("====>>>> send cmd opcode %d :write cmd reg: 0x%08x, arg: 0x%08x\n", cmd->opcode, reg, arg);

	err = xcsdh_access_smcc_reg(host, SMCC_CMD, NULL, reg, 1);
	if (err)
	{
		err = -1005;
		goto out;
	}
	TRACE;

	err = xcsdh_wait_ciu(host, 1000);
	if (err)
	{
		err = -1006;
		goto out;
	}
	TRACE;

#ifdef VIRT_SLOT
	/* send to virtual slot */
	if (xcsdh_virt_slot.card != NULL) {
		xcsdh_virt_slot.vsd_ops->write_cmd(mmc, host->mrq);
	}
#endif

	return (0);

out:
#ifdef XCSDH_DEBUG
	xcsdh_err_dump();
#endif
	TRACE;

	return(err);
}

static void turn_on_led(void)
{
	u32 temp;

	temp = readl((void*)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT));
	if(!(temp & gpio_led_mask))
	{
		temp |= gpio_led_mask;
		writel(temp, (void*)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT));
	}
}

static void clear_flash_scheduled(unsigned long data)
{
	flash_scheduled = 0;
}

static void turn_off_led(unsigned long msec)
{
	u32 temp;

	msec /= (1000 / HZ);
	if(msec == 0)
		msec = 1;

	temp = readl((void*)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT));
	if(temp & gpio_led_mask)
	{
		temp &= ~gpio_led_mask;
		writel(temp, (void*)(XC_SOC_PROC_MMREG_BASE + GPIO_DEDICATED_OUT));
	}

	init_timer(&led_off_timer);
	led_off_timer.function = clear_flash_scheduled;
	led_off_timer.data = 0;
	led_off_timer.expires = jiffies + msec;
	add_timer(&led_off_timer);
}

static void flash_led(unsigned long msec)
{
	turn_on_led();

	msec /= (1000 / HZ);
	if(msec == 0)
		msec = 1;

	init_timer(&led_off_timer);
	led_off_timer.function = turn_off_led;
	led_off_timer.data = msec;
	led_off_timer.expires = jiffies + msec;
	add_timer(&led_off_timer);
}

static void xcsdh_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct xcsdh_host *host;
	int err = 0;
	unsigned long flags;
	bool hwLockObtained = false;

	if (hwLockRequired(req->cmd)) {
		emmcHw_lock();
		hwLockObtained = true;
	}

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);
	ENCRYPT_DEBUG_PRINTK("---> lock [%d]\n", smp_processor_id());
	
	host->mrq = req;
	host->cmd = req->cmd;
	host->data = host->cmd->data;
	host->request_start=1;
	host->cd_received=0;
	host->dto_received=0;
	host->use_desc=0;

	host->cmd_status = 0;
	host->data_status = 0;
	host->pending_event = 0;
	host->rawint_sts = 0;

	DPRINTK("mmc %p, req %p, host %p, core_id %d\n",
			mmc, req, host, host->core_id);

	DPRINTK("start request, get cmd op 0x%x, arg 0x%x retries %d, req 0x%p\n",
			req->cmd->opcode, req->cmd->arg,
			req->cmd->retries, req);

#ifdef XCT_PROFILE
	xct_start_timer(__func__);
#endif


	/* Check for card present before send command to HW */
	if (!xcsdh_check_card_present(host)) {
		DPRINTK("XCSDH: host[%d] send command with no card in slot\n", host->core_id);
		if (hwLockObtained)
			emmcHw_unlock();
		spin_unlock_irqrestore(&host->lock, flags);
		host->mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, req);
		return;
	}

	if (host->state == STATE_FATAL_ERROR) {
		WARNMSG("XCSDH: host[%d] controller in reset\n", host->core_id);
		if (hwLockObtained)
			emmcHw_unlock();
		spin_unlock_irqrestore(&host->lock, flags);
		host->mrq->cmd->error = -EIO;
		mmc_request_done(mmc, req);
		return;
	}
	
	if (host->state != STATE_IDLE) {
		WARNMSG("XCSDH: host[%d] controller not idle\n", host->core_id);
		if (hwLockObtained)
			emmcHw_unlock();
		spin_unlock_irqrestore(&host->lock, flags);
		host->mrq->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, req);
		return;
	}

	host->state = STATE_SENDING_CMD;
	err = xcsdh_send_cmd(mmc, req->cmd);
	TRACE;

	if (err) {
		ERRMSG("send command %d err, error code %d\n", host->cmd->opcode, err);
		if (hwLockObtained)
			emmcHw_unlock();
		spin_unlock_irqrestore(&host->lock, flags);
		req->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, req);
		return;
	}

	if(gpio_led_mask)
	{
		if(!flash_scheduled)
		{
			flash_led(500);
			flash_scheduled = 1;
		}
	}

	TRACE;

	del_timer(&host->cmd_timeout_timer);
    host->cmd_timeout_timer.expires = jiffies + 5 * HZ;	//Some SD card take more than 3 seconds during data transfer (specially for ext2 FS).
	add_timer(&host->cmd_timeout_timer);

	// The hardware lock is released in tasklet if obtained in this function.
	
	ENCRYPT_DEBUG_PRINTK("<--- unlock [%d]\n", smp_processor_id());
	spin_unlock_irqrestore(&host->lock, flags);
	TRACE;
	return;
}


static void xcsdh_data_irq(struct xcsdh_host *host, u32 irq_state)
{
	//xcsdh_poll_rsp(host->mmc, host->mrq, 1000);
	struct mmc_request *mrq = NULL;
	struct mmc_data *data = NULL;

	mrq = host->mrq;
	if (host->data != NULL) {
		if ((host->data->flags & MMC_DATA_READ) ||
				(host->data->flags & MMC_DATA_WRITE)) {
			data = host->data;

#ifdef CONFIG_MMC_XCODE_ENCRYPTION
			if (host->decryptFlag) {
				set_bit(EVENT_START_DECRYPTION, &host->pending_event);
			}
#endif
			dma_unmap_sg(mmc_dev(host->mmc),
					data->sg,
					data->sg_len,
					((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));

			data->bytes_xfered = data->blksz * data->blocks;

			ENCRYPT_DEBUG_PRINTK("\tdata_irq CMD:%d bytes_xfered:%d\n", 
					host->cmd ? host->cmd->opcode : 1234, data->bytes_xfered);
		}
	}else {
		ERRMSG("DTO interrupt without data pointer!\n");
		if(host->cmd)
			ERRMSG("cmd opcode %d\n", host->cmd->opcode);
	}

	same_tcbcnt_nr = 0;
	return;
}



/* XXX: need handle different error from IRQ */
static int post_data_complete(struct xcsdh_host *host, struct mmc_data *data)
{
	u32 status = host->data_status;
	if (status & SMCC_RINTSTS_DAT_ERR_MASK) {
		if (status & SMCC_RINTSTS_DRTO_MASK) {
			data->error = -ETIMEDOUT;
		} else if (status & SMCC_RINTSTS_DCRC_MASK) {
			data->error = -EILSEQ;
		} else if (status & SMCC_RINTSTS_EBE_MASK) {
			if (data->flags & MMC_DATA_WRITE) {
				/*
				 * No data CRC status was returned.
				 * The number of bytes transferred
				 * will be exaggerated in PIO mode.
				 */
				data->bytes_xfered = 0;
				data->error = -ETIMEDOUT;
			} else if (data->flags & MMC_DATA_READ) {
				data->error = -EIO;
			}
		} else {
			/* SMCC_INT_SBE is included */
			data->error = -EIO;
		}
	}
	return data->error;
}

static int post_command_done(struct xcsdh_host *host, struct mmc_command *cmd)
{
	u32 status = host->cmd_status;
	host->cmd_status = 0;

	DPRINTK("%s: opcode %d cmd status 0x%x\n", __func__, cmd->opcode, (unsigned)status); /* JDEBUG */

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* Long respond */
			xcsdh_access_smcc_reg(host, SMCC_RESP0, &cmd->resp[3], 0, 0);
			xcsdh_access_smcc_reg(host, SMCC_RESP1, &cmd->resp[2], 0, 0);
			xcsdh_access_smcc_reg(host, SMCC_RESP2, &cmd->resp[1], 0, 0);
			xcsdh_access_smcc_reg(host, SMCC_RESP3, &cmd->resp[0], 0, 0);
		} else {
			xcsdh_access_smcc_reg(host, SMCC_RESP0, &cmd->resp[0], 0, 0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
		}
	}

	if (status & SMCC_RINTSTS_RTO_MASK)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & SMCC_RINTSTS_RCRC_MASK))
		cmd->error = -EILSEQ;
	else if (status & SMCC_RINTSTS_RE_MASK)
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
			mdelay(20);
	}

	PRINT_MMC_COMMAND("Done", host->state, cmd);

#ifdef CONFIG_MMC_XCODE_ENCRYPTION
	if (check_wr_encrypt_rd_decrypt(host, cmd, MMC_DATA_READ)) {
		ENCRYPT_DEBUG_PRINTK("%s start read decryption cmd:%d\n", __func__, cmd->opcode);
		cmd->error = encrypt_wr_decrypt_rd(host->mmc, cmd);
	}
#endif

	return cmd->error;
}

#if 0				/* only for polling backup */
static void __xcsdh_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct xcsdh_host *host;
	int err = 0;

	host = mmc_priv(mmc);

	printk("%s: start request, get cmd op 0x%x, arg 0x%x retries %d \n",
			__func__, req->cmd->opcode, req->cmd->arg, req->cmd->retries);


	err = xcsdh_send_cmd(mmc, req->cmd);
	DPRINTK("after send cmd err = %d\n", err);
	if (err)
		goto out;

	if ( (req->cmd->flags & MMC_RSP_PRESENT) ||
			(req->cmd->flags & MMC_RSP_136)) {
		/* wait for rsp */

		err = xcsdh_poll_rsp(mmc, req, 1000);
		DPRINTK("after poll err %d\n", err);
	}

	/* XXX: we have only 1 card. must be selected... */
	/* Special for CMD7 */
	if (req->cmd->opcode == MMC_SELECT_CARD)
		host->transfer_mode = 1;


	/* may sched to tasklet for real thing? */
	mmc_request_done(mmc, req);

	printk("%s: done request op %d\n", __func__, req->cmd->opcode);


	return;

out:
	req->cmd->error = MMC_ERR_TIMEOUT;
	return;
}
#endif	/* #if 0 */

static int xcsdh_reset_fifo(struct xcsdh_host *host)
{
	int err;
	int fifoth_reg = 0x300f0010;
	int ctrl_reg = 0;

	DPRINTK("start here for core %d\n", host->core_id);

	err = xcsdh_access_smcc_reg(host, SMCC_FIFOTH, NULL,
			fifoth_reg, 1);
	if (err)
		goto out;

	err = xcsdh_access_smcc_reg(host, SMCC_CTYPE, NULL, 0, 1);
	if (err)
		goto out;

	/* read the ctrl reg then reset the fifo */
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, &ctrl_reg, 0, 0);
	if (err)
		goto out;
	ctrl_reg |= 1 << SMCC_CTRL_FIFO_RESET_SHIFT;
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, ctrl_reg, 1);
	if (err)
		goto out;


	/* XXX:
	 * If enable own bit the SD core will generate
	 * another IRQ when the core fetched the descripter,
	 * i.e. after clear the own bit in descripter
	 * we don't want 2 IRQs come now
	 */
	/* Own bit enable */
	//MMR_WRITE(0x10, SMCC_INT_EN_REG);

out:
	return (err);
}

static void xcsdh_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct xcsdh_host *host;
	unsigned long flags;
	u32 clk;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);


	if (ios->power_mode == MMC_POWER_OFF) {
		xcsdh_set_power(host, 0);
	}

	/* HACK for PUSH-PULL mode. let our IP works */
	if (ios->bus_mode == MMC_BUSMODE_PUSHPULL) {
		xcsdh_reset_fifo(host);
	}

	/* XXX if clock == 0, should disable clock */
	if (ios->clock != 0) {
		/*
			It is found that after sending CMD41 (arg: 0), 50us is needed
			before setting the controller clock. This may be related
			to the voltage stabilization of card after inserting.
		*/
		udelay(50);
		clk = host->max_clock / ios->clock;
		clk = (int) (clk / 2); /* its the clock devider */
		DPRINTK("ios->clock = 0x%x, clk = 0x%x\n",
				ios->clock, clk);

		xcsdh_set_clk(host, clk);
	}

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		xcsdh_set_bus_width(host, 4);
	else
		xcsdh_set_bus_width(host, 1);

	if (ios->power_mode == MMC_POWER_ON) {
		xcsdh_set_power(host, 1);
	}

	spin_unlock_irqrestore(&host->lock, flags);

	return;
}

int xcsdh_get_ro(struct mmc_host *mmc)
{
	/* The Micro SD slot has no WP signal */
#if 0
    u32 cd_reg = 0;
	struct xcsdh_host *host;

	host = mmc_priv(mmc);

	DPRINTK("%s: start get_ro \n", __func__);

    xcsdh_access_smcc_reg(host, SMCC_WRTPRT, &cd_reg, 0, 0);
    if (cd_reg != 0)
        return 1;
#endif
	return (0);
}

static void xcsdh_set_cap(struct mmc_host *mmc)
{
    mmc->ocr_avail |=
		MMC_VDD_34_35 | MMC_VDD_35_36 |
		MMC_VDD_33_34 | MMC_VDD_32_33 ;

	mmc->ocr_avail |= 0xc0000000; /* b30,31 for CCC and status */


	//	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_BYTEBLOCK;
    mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MULTIWRITE |
		MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ;
}

static int xcsdh_set_bus_width(struct xcsdh_host *host, u32 w)
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

	err = xcsdh_access_smcc_reg(host, SMCC_CTYPE, NULL, reg, 1);

	return(err);

}

static int xcsdh_set_power(struct xcsdh_host *host, u32 on)
{
	volatile u32 reg;
	int err;

	if (on)
		reg = 1;
	else
		reg = 0;

	err = xcsdh_access_smcc_reg(host, SMCC_PWREN, NULL, reg, 1);

	return(err);
}

static int xcsdh_check_card_present(struct xcsdh_host *host)
{
	volatile u32 reg = 0;
	u32 bid = MMR_READ(CG_DUMMY_REG1);

	xcsdh_access_smcc_reg(host, SMCC_CDETECT, &reg, 0, 0);
	/* card detect reg '0' means card present, we have to host  */
	host->card_present = (u8)(~reg) & 0x3;

	/* Board features */
	switch(bid) {
	case 0x1600:
	case 0x1601:
		/* Turn on/off Card present LED */
		if(reg & 0x1) {
			if (MMR_READ(GPIO_D_OUT) & 0x4)
				MMR_WRITE(MMR_READ(GPIO_D_OUT) & ~0x4, GPIO_D_OUT);
		} else {
			if(!(MMR_READ(GPIO_D_OUT) & 0x4))
				MMR_WRITE(MMR_READ(GPIO_D_OUT) | 0x4, GPIO_D_OUT);
		}
		break;

	default:
		break;
	}

	return(host->card_present & (u8)(1 << host->core_id));
}

static int xcsdh_enable_dma(struct xcsdh_host *host)
{
	volatile u32 reg;
	int err;

	host->use_idmac = 1;
	host->use_dmac = 1;

	/* Software reset the DMA controller internal registers */
	reg = 1 << SMCC_BMOD_SWR_SHIFT;
	err = xcsdh_access_smcc_reg(host, SMCC_BMOD, NULL, reg, 1);
	if (err)
		goto out;

	reg = 1 << SMCC_BMOD_DE_SHIFT;
	/* for dual buffer descriptor */
	//reg |= (sizeof(struct xcsdh_idmac_desc) * XCSDH_DESC_NUM) << 2;
	//reg |= (sizeof(struct xcsdh_idmac_desc) / 16) << 2;
	DPRINTK("%s: BMOD reg 0x%08x\n", __func__, reg);
	DPRINTK("%s: sizeof(struct xcsdh_idmac_desc) / 16 = %d, << 2 = 0x%x, OR reg = 0x%08x\n",
			__func__,
			(sizeof(struct xcsdh_idmac_desc) / 16),
			((sizeof(struct xcsdh_idmac_desc) / 16) << 2),
			(reg | ((sizeof(struct xcsdh_idmac_desc) / 16) << 2)));
	err = xcsdh_access_smcc_reg(host, SMCC_BMOD, NULL, reg, 1);
	if (err)
		goto out;


	reg = 1 << SMCC_CTRL_IDMAC_SHIFT |
		1 << SMCC_CTRL_INT_EN_SHIFT;
	err = xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, reg, 1);
	if (err)
		goto out;

	reg = SMCC_IDMAC_INT_TI | SMCC_IDMAC_INT_RI | SMCC_IDMAC_INT_NI;
	err = xcsdh_access_smcc_reg(host, SMCC_IDINTEN, NULL, reg, 1);
	if (err)
		goto out;

	/* fix this! size not correct */
	memset(&(host->desc[0].d0), 0xffff, (4*sizeof(u32)));

	/* nolonger need buf pointer ourself */
#if 0
	/* init the descriptor */
	host->desc.buf0 = kmalloc(host->max_block, GFP_DMA);
	if (host->desc.buf0 == NULL) {
		err = -ENOMEM;
		goto out;
	}

	PRINT_16BYTES(host->desc.buf0);

	host->desc.buf1 = kmalloc(host->max_block, GFP_DMA);
	if (host->desc.buf1 == NULL) {
		kfree(host->desc.buf0);
		err = -ENOMEM;
		goto out;
	}
#endif

out:
	return(err);
}

static void xcsdh_dma_cleanup (struct xcsdh_host *host)
{
	struct mmc_data *data;
	if(host->data) {
		data = host->data;
		if ((host->data->flags & MMC_DATA_READ) ||
				(host->data->flags & MMC_DATA_WRITE)) {
			dma_unmap_sg(mmc_dev(host->mmc),
					data->sg,
					data->sg_len,
					((data->flags & MMC_DATA_READ)?DMA_FROM_DEVICE:DMA_TO_DEVICE));
		}
	}
}

static void xcsdh_stop_idma (struct xcsdh_host *host)
{
	volatile u32 reg;
	/* disable and reset the iDMA */
	xcsdh_access_smcc_reg(host, SMCC_CTRL,&reg, 0,0);
	reg &= ~SMCC_CTRL_USE_IDMAC;
	reg |= SMCC_CTRL_DMA_RESET;
	xcsdh_access_smcc_reg(host, SMCC_CTRL, NULL, reg, 1);

	/* Stop the iDMA */
	xcsdh_access_smcc_reg(host, SMCC_BMOD, &reg, 0, 0);
	reg &= ~SMCC_BMOD_DE;
	reg |= SMCC_BMOD_SWR;
	xcsdh_access_smcc_reg(host, SMCC_BMOD, NULL, reg, 1);
}

static int xcsdh_enable_irq(struct xcsdh_host *host)
{
	int err = 0;
	volatile u32 irq_reg = 0;

#ifdef USE_IIA
    /* enable irq */
    IIALocalSetMask(IIA_SMCC_INT);
#endif

	/* Core 0 */
	irq_reg = MMR_READ(SMCC_INT_EN_REG);
	irq_reg |= SMCC_INT_EN_REG_INT_EN_MASK;
	MMR_WRITE(irq_reg, SMCC_INT_EN_REG);

	/* Core 1 */
	irq_reg = MMR_READ(SMCC_INT_EN_REG1);
	irq_reg |= INT_EN1_MASK;
	MMR_WRITE(irq_reg, SMCC_INT_EN_REG1);

	/* cmd done ,card detect and DTO */
	err = xcsdh_access_smcc_reg(host, SMCC_INT_MASK, NULL, SMCC_INTERRUPTS_MASK, 1);
	xcsdh_access_smcc_reg(host, SMCC_INT_MASK, &irq_reg, 0, 0);

	if ( ! host->irq_requested) {
		err = request_irq(XCSDH_IRQ,
				xcsdh_irq_handler,
				IRQF_SHARED | IRQF_DISABLED,
				//DEV_NAME,
				mmc_hostname(host->mmc),
				(void *)host);
		host->irq_requested = 1;
	}

	DPRINTK("request_irq err = %d\n", err);

	return (err);
}

static void xcsdh_disable_irq(struct xcsdh_host *host)
{
	volatile u32 irq_reg = 0;

	xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, 0xffffffff, 1);
	xcsdh_access_smcc_reg(host, SMCC_INT_MASK, NULL, 0x0, 1);

	/* Core 0 */
	irq_reg = MMR_READ(SMCC_INT_EN_REG);
	irq_reg &= ~SMCC_INT_EN_REG_INT_EN_MASK;
	MMR_WRITE(irq_reg, SMCC_INT_EN_REG);

	/* Core 1 */
	irq_reg = MMR_READ(SMCC_INT_EN_REG1);
	irq_reg &= ~INT_EN1_MASK;
	MMR_WRITE(irq_reg, SMCC_INT_EN_REG1);

	return;
}


static void xcsdh_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct xcsdh_host *host = NULL;

	host = mmc_priv(mmc);

	/* XXX should have lock for multi slots */
	if (enable)
		xcsdh_sdio_card_set_irq(0, 1); /* assume only 1 SDIO */
	else
		xcsdh_sdio_card_set_irq(0, 0);
}


static void xcsdh_sdio_card_set_irq(int cardnum, int on)
{
	int sdio_shift = SMCC_INT_MASK_SDIO_INT_SHIFT + cardnum;
	u32 intmask_reg = 0;
	u32 reg = 0;

	/* XXX: Fix the S_CORE */
	xcsdh_access_smcc_reg(master->sd_host[S_CORE], SMCC_INT_MASK, &intmask_reg, 0, 0);
	//	xcsdh_access_smcc_reg(host->core_id, SMCC_INT_MASK, &intmask_reg, 0, 0);
	//	printk("%s: intmask_reg 0x%x, cardnum %d, on %d\n",
	//	       __func__, intmask_reg, cardnum, on);

	reg = 1 << sdio_shift;
	if (on) {
		reg |= intmask_reg;
	} else {
		reg = ~reg;
		reg &= intmask_reg;
	}
	/* XXX: Fix the S_CORE */
	//	xcsdh_access_smcc_reg(host->core_id, SMCC_INT_MASK, NULL, reg, 1);
	xcsdh_access_smcc_reg(master->sd_host[S_CORE], SMCC_INT_MASK, NULL, reg, 1);
}

static void xcsdh_request_end(struct xcsdh_host *host)
{
	struct mmc_request *mrq = host->mrq;

	WARN_ON(host->cmd || host->data);
	del_timer(&host->cmd_timeout_timer);

	host->cmd = NULL;
	host->data = NULL;
	host->mrq = NULL;
	host->state = STATE_IDLE;
	host->pending_event = 0;
	host->cmd_status = 0;
	host->data_status = 0;
	host->rawint_sts = 0;
	if ((host->mmc) && mrq) {
	spin_unlock(&host->lock);
	mmc_request_done(host->mmc, mrq);
	spin_lock(&host->lock);
	} else {
		ERRMSG("!!!! no request done performed\n");
	}
}

static void xcsdh_tasklet_finish(unsigned long param)
{
	struct xcsdh_host *host = (struct xcsdh_host *) param;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	enum xcsdh_host_state state, prev_state;
	ulong delay;
	u32 err;
	//ERRMSG(">>>> in\n");

	ENCRYPT_DEBUG_PRINTK(">>> in %d\n", host->state);

	spin_lock(&host->lock);

#ifdef WORKAROUND_JIRA_ASIC_STINGRAY_392
	//According to eleung, this bug affects all Eagle/Viper/Corsa/Stingray
	{
		int i;
		for(i=0;i<host->desc_nr;i++)
			if(host->desc[i].d0 & SMCC_DESC0_OWN_MASK)
			{
				spin_unlock&host->lock);
				tasklet_schedule(&host->finish_tasklet);
				return;
			}
	}
#endif

	if(!xcsdh_check_card_present(host)) {
		WARNMSG("host[%d] card not in the slot, pending cmd %d\n", host->core_id, (host->cmd)? host->cmd->opcode : -1);
	}

	mrq = host->mrq;
	data = host->data;
	state = host->state;

	do {
		prev_state = state;
		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
				TRACE;
			if(!test_and_clear_bit(EVENT_CMD_COMPLETE, &host->pending_event))
				break;
				TRACE;

			cmd = host->cmd;
			host->cmd = NULL;
			err = post_command_done(host, cmd);

			if (cmd->data && err){
				WARNMSG("send command [%d] with data error\n", mrq->cmd->opcode);
				xcsdh_stop_idma(host);
				xcsdh_dma_cleanup(host);
				if (data->stop) {
					ERRMSG("Send stop command\n");
					xcsdh_send_cmd(host->mmc, data->stop);
						TRACE;
						delay = msecs_to_jiffies(100);
						del_timer(&host->cmd_timeout_timer);
						host->cmd_timeout_timer.expires = jiffies +delay;
						add_timer(&host->cmd_timeout_timer);
				}
				host->state = state = STATE_SENDING_STOP;
			}

			if (!cmd->data || err) {
				if(err == -EIO) {
					state = STATE_FATAL_ERROR;
					break;
				}
				xcsdh_request_end(host);
				goto unlock;
			}

			host->state = prev_state = state = STATE_SENDING_DATA;

		case STATE_SENDING_DATA:
				TRACE;
			/* check data err*/
			if (test_and_clear_bit(EVENT_DATA_ERROR, &host->pending_event)) {
					TRACE;
				xcsdh_stop_idma(host);
				xcsdh_dma_cleanup(host);
#if 1
				state = STATE_FATAL_ERROR;
				break;
#else
				/* should send stop command when data error happened*/
				if(data->stop) {
					xcsdh_send_cmd(host->mmc, data->stop);
					state = STATE_SENDING_STOP;
				} else {
					state = STATE_FATAL_ERROR;
				}
				break;
#endif
			}

			if (!test_and_clear_bit(EVENT_XFER_COMPLETE, &host->pending_event)){
					TRACE;
				break;
			}

			/* check data err again */
			if (test_and_clear_bit(EVENT_DATA_ERROR, &host->pending_event)) {
					TRACE;
				xcsdh_stop_idma(host);
				xcsdh_dma_cleanup(host);
#if 1
				host->state = state = STATE_FATAL_ERROR;
				break;
#else
				if(data->stop) {
					xcsdh_send_cmd(host->mmc, data->stop);
					state = STATE_SENDING_STOP;
				} else {
					state = STATE_FATAL_ERROR;
				}
				break;
#endif
			}

			host->data = NULL;
			err = post_data_complete(host, data);
			if (!err) {
				if(!data->stop) {
					xcsdh_request_end(host);
					goto unlock;
				} else {
					xcsdh_send_cmd(host->mmc, data->stop);
						TRACE;
						delay = msecs_to_jiffies(100);
						del_timer(&host->cmd_timeout_timer);
						host->cmd_timeout_timer.expires = jiffies + delay;
						add_timer(&host->cmd_timeout_timer);
				}
			} else {
				host->state = state = STATE_FATAL_ERROR;
				break;
			}

			host->state = prev_state = state = STATE_SENDING_STOP;

		case STATE_SENDING_STOP:
				TRACE;
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE, &host->pending_event)) {
					TRACE;
				break;
			}
				TRACE;

			if(mrq->data->error) {
					TRACE;
				state = STATE_FATAL_ERROR;
				break;
			}
				TRACE;

			if (mrq->stop) {
				post_command_done(host, mrq->stop);
			} else {
				host->cmd_status = 0;
			}

			xcsdh_request_end(host);
			goto unlock;

		case STATE_FATAL_ERROR:
				TRACE;
			WARNMSG("reset host, opcode %d cmd sts 0x%x data sts 0x%x.\n", \
					mrq->cmd->opcode, (unsigned)host->cmd_status, (unsigned)host->data_status);
			xcsdh_smcc_fbe_reset(host);
			xcsdh_reset_smcc_core(host);
			xcsdh_enable_dma(host);
			xcsdh_enable_irq(host);
			mmc_detect_change(host->mmc, msecs_to_jiffies(500));
			xcsdh_request_end(host);
			goto unlock;
		}
	}while (state != prev_state);
	host->state = state;
	TRACE;

unlock:
	spin_unlock(&host->lock);
	//ERRMSG("<<<< out\n");

	ENCRYPT_DEBUG_PRINTK("<<< out %d\n", host->state);
}

/*
 * The timer function monitor the command stauts, if controller does not response or local irq disabled
 * the timer will goes off, handle the command timeout, if no error happened, process the last command 
 * otherwise reset the core.
 */
static void xcsdh_cmd_timeout_timer_fn(unsigned long param)
{
	struct xcsdh_host *host = (struct xcsdh_host *) param;
	struct mmc_request *mrq = NULL;
	ulong flags;
	volatile u32 tmp;

	if(!host) {
		ERRMSG("XCSDH: NULL host pointer\n");
		return;
	}
	if(!host->mmc) {
		ERRMSG("XCSDH: host timeout without mmc struct\n");
		return;
	}
	if(!host->mrq) {
		ERRMSG("XCSDH: host timeout without request\n");
		return;
	}

	mrq = host->mrq;
	WARNMSG("XCSDH: core %d cmd %d time out\n", host->core_id, mrq->cmd->opcode);

	PRINT_MMC_COMMAND("Timeout", host->state, host->cmd);

	/*
	 * Reset the core if:
	 * 1. can't access core register;
	 * 2. controller error
	 */
	if (xcsdh_access_smcc_reg(host,SMCC_MINTSTS,&tmp, 0, 0)) {
		ERRMSG("XCSDH: can't access contorller register\n");
		goto reset_core;
	}

	if (tmp & SMCC_RINTSTS_ALL_ERR_MASK) {
		ERRMSG("XCSDH: cmd %d error, interrupt status 0x%x\n", mrq->cmd->opcode, tmp);
		goto reset_core;
	}

	/*
	 * No error, handle the command done and data transfer for last command
	 * all interrupts should already assert when command timeout.
	 */
	spin_lock_irqsave(&host->lock, flags);
	if (tmp & SMCC_RINTSTS_CD_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_CD_MASK, 1);
		if(!host->cmd_status)
			host->cmd_status = tmp;
		smp_wmb();
		set_bit(EVENT_CMD_COMPLETE, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s EVENT_CMD_COMPLETE %d\n", __func__, host->state);
	}

	if (tmp & SMCC_RINTSTS_DTO_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_DTO_MASK, 1);
		if(!host->data_status)
			host->data_status = tmp;
		smp_wmb();
		set_bit(EVENT_XFER_COMPLETE, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s EVENT_XFER_COMPLETE %d\n", __func__, host->state);

		xcsdh_data_irq(host, tmp);		
	}	
	tasklet_schedule(&host->finish_tasklet);
	spin_unlock_irqrestore(&host->lock, flags);
	return;

reset_core:
	spin_lock_irqsave(&host->lock, flags);
	xcsdh_smcc_fbe_reset(host);
	xcsdh_reset_smcc_core(host);
	xcsdh_enable_dma(host);
	xcsdh_enable_irq(host);

	if (host->data)
		host->data->error = -ETIMEDOUT;
	else if (host->cmd)
		host->cmd->error = -ETIMEDOUT;

	host->cmd_status = 0;
	host->data_status = 0;
	host->pending_event = 0;
	host->rawint_sts = 0;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;
	host->state = STATE_IDLE;

	host->request_start=0;
	spin_unlock_irqrestore(&host->lock, flags);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));
	mmc_request_done(host->mmc, mrq);
}

static int xcsdh_set_clk(struct xcsdh_host *host, u32 clk)
{
	volatile u32 reg = 0;
	int err = 0;

	/* DB p177 */

#define CLK_CMD							\
	reg = 1 << SMCC_CMD_START_CMD_SHIFT |			\
	1 << SMCC_CMD_UPDATE_CLK_SHIFT |		\
    1 << SMCC_CMD_WAIT_PRVDATA_CMP_SHIFT |		\
    1 << SMCC_CMD_USE_HOLD_REG_SHIFT; \
	err = xcsdh_access_smcc_reg(host, SMCC_CMD, NULL, reg, 1);	\
	if (err)						\
	goto out;					\
	\
	\
	/* disable clock */
	reg = 0;
	err = xcsdh_access_smcc_reg(host, SMCC_CLKENA, NULL, reg, 1);
	if (err)
		goto out;

	CLK_CMD;

#if 0
	//clk = 0;			/* JASON XXX: skip clk div? */
	/* Set clock divison */
	/* HACK!
	 * XC4 SD interface not fast enough for full speed
	 *
	 */
	if (clk == 0) {
		clk = 1;	/* divide by 2 */
	}
#endif
	reg = clk;
	err = xcsdh_access_smcc_reg(host, SMCC_CLKDIV, NULL, reg, 1);
	if (err)
		goto out;
	/* XXX: should we use the value directly? */
	//host->clock = host->max_clock / clk;
	if (clk == 0)
		host->clock = host->max_clock;
	else
		host->clock = host->max_clock / (clk * 2);

	/* Set clock source (always 0) */
	reg = 0;
	err = xcsdh_access_smcc_reg(host, SMCC_CLKSRC, NULL, reg, 1);
	if (err)
		goto out;

	CLK_CMD;

	/* Enable clock */
	reg = 1;
	err = xcsdh_access_smcc_reg(host, SMCC_CLKENA, NULL, reg, 1);
	if (err)
		goto out;

	CLK_CMD;


out:
	return (err);


#undef CLK_CMD

}

static irqreturn_t xcsdh_irq_handler(int irq, void *dev_id)
{
	volatile u32 rintsts, mintsts, idsts;
	struct xcsdh_host *host = dev_id;

#ifdef CONFIG_MMC_XCODE_ENCRYPTION
	volatile u32 cpuId = smp_processor_id();
#endif

#ifdef XCSDH_DEBUG
	volatile u32 sts, dscaddr, bufaddr, tcbcnt, tbbcnt, err;
	struct mmc_request *mrq = host->mrq;
#endif

#ifdef XCT_PROFILE
	char profile_cmt[32];
#endif

	/* check the masked interrupt only */
	xcsdh_access_smcc_reg(host, SMCC_MINTSTS, &mintsts, 0, 0);
	xcsdh_access_smcc_reg(host, SMCC_RINTSTS, &rintsts, 0, 0);
	xcsdh_access_smcc_reg(host, SMCC_IDSTS, &idsts, 0, 0);

	if ((mintsts == 0) && (idsts == 0)) {
		/* not this core */
		DPRINTK("core [%d] int 0x%x raw int 0x%x idsts 0x%x\n", host->core_id, (unsigned)mintsts, (unsigned)rintsts, (unsigned)idsts);
		return(IRQ_NONE);
	}

#ifdef XCSDH_DEBUG
	/* For debug purpose only, turn on will cause the irq handler consume more cpu time */
	err = xcsdh_access_smcc_reg(host, SMCC_STATUS, &sts, 0, 0);
	err = xcsdh_access_smcc_reg(host, SMCC_DSCADDR, &dscaddr, 0, 0);
	err = xcsdh_access_smcc_reg(host, SMCC_BUFADDR, &bufaddr, 0, 0);
	err = xcsdh_access_smcc_reg(host, SMCC_TCBCNT, &tcbcnt, 0, 0);
	err = xcsdh_access_smcc_reg(host, SMCC_TBBCNT, &tbbcnt, 0, 0);
	DPRINTK("core id %d, rintsts 0x%08x, idsts 0x%08x, status 0x%08x DSCADDR 0x%08x, BUFADDR 0x%08x, tcbcnt %d, tbbcnt %d last opcode %d\n",
			host->core_id,
			rintsts, idsts, sts,
			dscaddr, bufaddr,
			tcbcnt, tbbcnt,
			((mrq) ? mrq->cmd->opcode : -1));
	DPRINTK("");

	if ((mintsts & SMCC_RINTSTS_ALL_ERR_MASK) && host->cmd)
		WARNMSG("!!!!>>>> cmd %d got an error 0x%x, handle it first\n", host->cmd->opcode, (unsigned)mintsts);
#endif


#ifdef XCT_PROFILE
	sprintf(profile_cmt, "%s@%d", __func__, __LINE__);
	xct_stamp_timer(profile_cmt);
#endif

	host->rawint_sts = rintsts;


	if (mintsts & SMCC_RINTSTS_CMD_ERR_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_CMD_ERR_MASK, 1);
		host->cmd_status = mintsts;
		smp_wmb();
		set_bit(EVENT_CMD_COMPLETE, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s [%d] EVENT_CMD_COMPLETE %d\n", __func__, cpuId, host->state);
	}

	if (mintsts & SMCC_RINTSTS_DAT_ERR_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_DAT_ERR_MASK, 1);
		host->data_status = mintsts;
		smp_wmb();
		set_bit(EVENT_DATA_ERROR, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s [%d] EVENT_DATA_ERROR %d\n", __func__, cpuId, host->state);

		tasklet_schedule(&host->finish_tasklet);
	}

	if (mintsts & SMCC_RINTSTS_HLE_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_HLE_MASK, 1);
		host->cmd_status = host->data_status =rintsts;
		smp_wmb();
		set_bit(EVENT_DATA_ERROR, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s [%d] EVENT_DATA_ERROR\n", __func__, cpuId);

		tasklet_schedule(&host->finish_tasklet);
	}

	if (mintsts & SMCC_RINTSTS_DTO_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_DTO_MASK, 1);
		if(!host->data_status)
			host->data_status = mintsts;
		smp_wmb();
		set_bit(EVENT_XFER_COMPLETE, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s [%d] EVENT_XFER_COMPLETE %d\n", __func__, cpuId, host->state);

		xcsdh_data_irq(host, mintsts);
		tasklet_schedule(&host->finish_tasklet);
	}

	if (mintsts & SMCC_RINTSTS_CD_MASK) {
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, SMCC_RINTSTS_CD_MASK, 1);
		if(!host->cmd_status)
			host->cmd_status = mintsts;
		smp_wmb();
		set_bit(EVENT_CMD_COMPLETE, &host->pending_event);

		ENCRYPT_DEBUG_PRINTK("%s [%d] EVENT_CMD_COMPLETE2 %d\n", __func__, cpuId, host->state);

		tasklet_schedule(&host->finish_tasklet);
	}

	if(mintsts & SMCC_RINTSTS_CARDDET_MASK) { /* Card insert/remove */
		xcsdh_access_smcc_reg(host, SMCC_RINTSTS,	NULL, SMCC_RINTSTS_CARDDET_MASK, 1);
		if(!xcsdh_check_card_present(host)) {
			xcsdh_smcc_fbe_reset(host);
		}

		if(host->mmc)
			mmc_detect_change(host->mmc, msecs_to_jiffies(500));
		else
			ERRMSG("card detect irq but no mmc structure\n");
	}

	if (idsts & (SMCC_IDMAC_INT_RI | SMCC_IDMAC_INT_TI)) {
		xcsdh_access_smcc_reg(host, SMCC_IDSTS, NULL, SMCC_IDMAC_INT_RI | SMCC_IDMAC_INT_TI, 1);
		xcsdh_access_smcc_reg(host, SMCC_IDSTS, NULL, SMCC_IDMAC_INT_NI, 1);
	}

	if (mintsts & SMCC_RINTSTS_ALLSDIO_IRQ_MASK) {
		/* SDIO give us interrupt */
		ERRMSG("SDIO irq 0x%x\n", (unsigned)(mintsts & SMCC_RINTSTS_ALLSDIO_IRQ_MASK));
		if (host->mmc && host->mmc->card) {
			if (host->mmc->card->type == MMC_TYPE_SDIO) {
				xcsdh_access_smcc_reg(host, SMCC_RINTSTS, NULL, mintsts & SMCC_RINTSTS_ALLSDIO_IRQ_MASK, 1);
				mmc_signal_sdio_irq(host->mmc);
			}
		}
	}

	return(IRQ_HANDLED);
}



static struct mmc_host_ops xcsdh_mmc_ops = {
	.request = xcsdh_request,
	.set_ios = xcsdh_set_ios,
	.get_ro = xcsdh_get_ro,
	.enable_sdio_irq = xcsdh_enable_sdio_irq,
};

static struct xcsdh_sdio_ops xcsdh_sdio_ops = {
	.sdio_card_set_irq = xcsdh_sdio_card_set_irq,
};

DECLARE_WAIT_QUEUE_HEAD(xcsdh_wait);
int mmc_wait_cmd_done(void);
static int xcsdh_probe_host(struct platform_device *pdev,
		struct xcsdh_master *master, int n)
{
	struct mmc_host *mmc;
	struct xcsdh_host *host;
	int err = 0;
	u32 vid = 0, uid = 0;
    u32 reg = 0;

	mmc = mmc_alloc_host(sizeof(struct xcsdh_host), &pdev->dev);
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->master = master;
	host->core_id = n;
	master->sd_host[n] = host;

	host->sd_stack = GENERIC_LINUX;
	sprintf(host->name, "XCode SD host controller %d\n", n);

	/* JDEBUG */
	DPRINTK("%s: mmc_host 0x%p, xcsdh_host 0x%p\n",
			__func__, mmc, host);

	xcsdh_mmc_host = mmc;

	spin_lock_init(&host->lock);
    spin_lock_init(&host->iplock);
	err = xcsdh_reset_smcc_core(host);

    //Increasing (Max) SMC Clock to 50 MHz
#ifdef CONFIG_PLAT_XCODE64xx
    MMR_READ(CG1_CLK_SRC_SEL7);
    reg &= ~(CG1_CLK_SRC_SEL7_SMCC0CLK_SRC_SEL_MASK | CG1_CLK_SRC_SEL7_SMCC1CLK_SRC_SEL_MASK);
    reg |= ((0x01 << SMCC0CLK_SRC_SEL_SHIFT) | (0x01 << SMCC1CLK_SRC_SEL_SHIFT));	//Setting CLK Src PLL5/20 i.e. 1000MHz/20 = 50MHz
    MMR_WRITE(reg, CG1_CLK_SRC_SEL7);
#else
    MMR_READ(CG1_CLK_SRC_SEL4);
    reg &= ~(CG1_CLK_SRC_SEL4_SMCC0CLK_SRC_SEL_MASK | CG1_CLK_SRC_SEL4_SMCC1CLK_SRC_SEL_MASK);
    reg |= ((0x01 << SMCC0CLK_SRC_SEL_SHIFT) | (0x01 << SMCC1CLK_SRC_SEL_SHIFT));	//Setting CLK Src PLL5/20 i.e. 1000MHz/20 = 50MHz
    MMR_WRITE(reg, CG1_CLK_SRC_SEL4);
#endif

	host->pending_event = 0;
	host->cmd_status = 0;
	host->data_status = 0;
	host->rawint_sts = 0;
	host->state = STATE_IDLE;

	host->max_block = 512;
	host->max_clock = 50000000; /* 50Mhz */
	mmc->f_min = 400000;	/* 400Khz */
	mmc->f_max = host->max_clock;

	/* Maximum data size host can handle one time */
	mmc->max_seg_size = PAGE_CACHE_SIZE;
	mmc->max_segs = XCSDH_DESC_NUM / (mmc->max_seg_size / DESC_BUF_SIZE);
	mmc->max_req_size = XCSDH_DESC_NUM * DESC_BUF_SIZE;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = (XCSDH_DESC_NUM * DESC_BUF_SIZE) / 512;

	tasklet_init(&host->finish_tasklet,
			xcsdh_tasklet_finish, (unsigned long)host);

	init_timer(&(host->cmd_timeout_timer));
	host->cmd_timeout_timer.data = (unsigned long) host;
	host->cmd_timeout_timer.function = xcsdh_cmd_timeout_timer_fn;

	xcsdh_set_cap(mmc);

	/* Get the SD controller ID */
	err = xcsdh_access_smcc_reg(host, SMCC_VERID, &vid, 0, 0);
	if (err) {
		printk(KERN_ERR "XCSDH: Access SMCC register failed, please check hardware\n");
		return err;
	} else {
		xcsdh_access_smcc_reg(host, SMCC_USRID, &uid, 0, 0);
		printk("XCSDH: Host controller version <0x%08x:0x%08x>\n", vid, uid);
	}
	xcsdh_check_card_present(host);
	DPRINTK("card_present = %d\n", host->card_present);

	err = xcsdh_enable_dma(host);

	host->irq_requested = 0;
	err = xcsdh_enable_irq(host);

	mmc->ops = &xcsdh_mmc_ops;

	mmc_add_host(mmc);

	/* SDIO ops */
	host->sdio_ops = &xcsdh_sdio_ops;

	return (err);

}

static int xcsdh_probe(struct platform_device *pdev)
{
	int err = 0;

	err = xcsdh_reset_smcc_hw();

	if (err) {
		printk("Failed to probe the SD host controller\n");
		return (err);
	}

	master = kmalloc(sizeof(struct xcsdh_master), GFP_KERNEL);
	master->sd_host = kmalloc(sizeof(struct xcsdh_host *) * XCSDH_HOST_NR, GFP_KERNEL);

	master->host_nr = 0;
	if (init_core0) {
		err = xcsdh_probe_host(pdev, master, 0);
		if(err) {
			printk("Failed to init mmc host 0\n");
			return err;
		}
		master->host_nr++;
	}

	if (init_core1) {
		err = xcsdh_probe_host(pdev, master, 1);
		if(err) {
			printk("Failed to init mmc host 1\n");
			return err;
		}
		master->host_nr++;
	}

	mmc_encryption_init();

	printk("%s %d host probed\n", __func__, master->host_nr);
	return (0);
}

/*
 * return the mmc_host instance for another
 * SDIO stack
 */
struct mmc_host *xcsdh_get_mmc_host(void)
{
	return (xcsdh_mmc_host);
}
EXPORT_SYMBOL(xcsdh_get_mmc_host);

#if 0
struct xcsdh_host *xcsdh_register_sd_bridge(struct xcsdh_sd_bridge_drv *bdrv)
{
	struct xcsdh_host *host;

	host = mmc_priv(xcsdh_mmc_host);
	host->bridge_drv = bdrv;
	host->sd_stack = bdrv->sd_stack;

	printk("%s: connected to the bridge driver %s using SDIO stack %d\n",
			__func__, bdrv->name, bdrv->sd_stack);

	return(host);
}
EXPORT_SYMBOL(xcsdh_register_sd_bridge);
#endif



static struct platform_driver xcsdh_driver = {
	.probe = xcsdh_probe,
	.driver = {
		.name = DEV_NAME,
	},
};

static int xcsdh_bind(struct platform_driver *pdrv)
{
	int err;

	err = platform_driver_register(pdrv);

	return (err);
}

static ssize_t mmc_proc_read(struct file *file, char __user *buf, size_t count, loff_t *offs)
{
	xcsdh_err_dump();
	return -1;
}
int mmc_rintsts(void){
        int err;
	u32 rintsts;
	err = xcsdh_access_smcc_reg(master->sd_host[0], SMCC_RINTSTS, &rintsts, 0, 0);
	printk("%s:%d rintst:%x\n", __func__,__LINE__,rintsts);
	return 0;
}

static noinline int xcsdh_send_stop(void);


//This function is called rather early in the boot process from another module.
//In this case, here we will use the global 'iplock' and directly access
//SD host 0.
int mmc_wait_cmd_done(void)
{
	int timeout= 5000;
	u32 rintsts;

    spin_lock_init(&iplock);
    spin_lock_init(&direct_host.iplock);
	//xcsdh_access_smcc_reg(master->sd_host[0], SMCC_RINTSTS, &rintsts, 0, 0);
	rintsts = xcsdh_read_smcc_reg_direct(SMCC_RINTSTS);

    //printk("%s:%d rintst:%x\n", __func__,__LINE__,rintsts);
	if (rintsts == 0){
		g_ready_to_init = 1;
                wake_up(&xcsdh_wait);
		return  1;
	}

    /*
     * wait for DTO
     */
	while ( !(rintsts & SMCC_RINTSTS_DTO_MASK)) {
        //printk("%s:%d rintst:%x\n", __func__,__LINE__,rintsts);
		rintsts = xcsdh_read_smcc_reg_direct(SMCC_RINTSTS);
		//printk("#");
		mdelay(1);
		timeout--;
		if(timeout == 0){
            printk("%s:%d rintst:%x\n", __func__,__LINE__,rintsts);
			printk(" timeout\n");
            break;
		}
	}

    xcsdh_send_stop();
    g_ready_to_init = 1;
    wake_up(&xcsdh_wait);
	//printk("done\n");
	return 0;
}
extern int sdio_reset(struct mmc_host *host);
extern int my_sdio_reset(struct mmc_host *host);
static ssize_t mmc_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *offs)
{
	struct xcsdh_host *host;
	u32 ctrl;

	host = mmc_priv(xcsdh_mmc_host);
#if 1
	printk("Reset!\n");
	if(host->mrq)
	{
		if(host->mrq->data)
			host->mrq->data->error = MMC_ERR_TIMEOUT;
		else
			printk("proc no data\n");
	}
	else
		printk("proc no request\n");
	xcsdh_access_smcc_reg(master->sd_host[0], 0, &ctrl, 0, 0);
	ctrl|=0x6;
	xcsdh_access_smcc_reg(master->sd_host[0], 0, NULL, ctrl, 1);
	udelay(100);
#if 1
	smcc_register_dump_all();
	printk("===========================================\n");
	MMR_WRITE(MMR_READ(SMCC_CTRL_REG)|1, SMCC_CTRL_REG);
	udelay(1000);
	MMR_WRITE(MMR_READ(SMCC_CTRL_REG)&(~1), SMCC_CTRL_REG);
	udelay(1000);
#endif
	xcsdh_tasklet_finish((unsigned long)host);

#if 0
	xcsdh_enable_dma(host);
	udelay(100);
	xcsdh_enable_irq(host);
	udelay(100);
	xcsdh_reset_fifo(host);
	udelay(100);
	xcsdh_set_clk(host, 0);
	udelay(100);
	xcsdh_set_bus_width(host, 4);
	udelay(100);
	xcsdh_set_power(host, 1);
	udelay(100);
#endif

//	my_sdio_reset(xcsdh_mmc_host);
#endif
	return -1;
}

static const struct file_operations mmc_proc_fops = {
	.read = mmc_proc_read,
	.write = mmc_proc_write,
};

static int __init xcsdh_init(void)
{
	int err;
	struct proc_dir_entry *ent = NULL;

	printk("%s: Initializing XCode SD controller\n", __func__);
#ifdef XCSDH_DEBUG
	xcsdh_print_board_ver();
#endif

	xcsdh_device = platform_device_register_simple(DEV_NAME,
			-1, NULL, 0);
	if (!xcsdh_device) {
		printk("%s: cannot register platform device %s\n",
				__func__, DEV_NAME);
		return (-ENODEV);
	}


	DPRINTK("%s: finish adding platform device\n", __func__);

	atomic_set(&xcsdh_irq_cnt, 0);
	xcsdh_bind(&xcsdh_driver);

#ifdef XCT_PROFILE
	xct_cal_sys_cnt();	/* profile */
	printk("delay 2s testing the timer\n");
	xct_start_timer(__func__);
	mdelay(2000);
	xct_stamp_timer(__func__);
	xct_print_all_diff();
#endif

	ent = proc_create("mmc_debug", 0, NULL, &mmc_proc_fops);
	if (!ent)
		printk(KERN_WARNING "mmc: Failed to register with procfs.\n");
	
	return (0);

	platform_device_put(xcsdh_device);
	return(err);
}

static void xcsdh_wq_handler(struct work_struct *w);
static struct workqueue_struct *xcsdh_wq = 0;
static DECLARE_DELAYED_WORK(xcsdh_work, xcsdh_wq_handler);

static void __exit xcsdh_exit(void)
{
	if(init_core0) {
		struct xcsdh_host *host = master->sd_host[0];

		xcsdh_disable_irq(host);
		if (host->irq_requested) {
			free_irq(XCSDH_IRQ, (void *)host);
			host->irq_requested = 0;
		}
		platform_driver_unregister(&xcsdh_driver);
		platform_device_unregister(xcsdh_device);
		if (xcsdh_wq)
			destroy_workqueue(xcsdh_wq);
	}

	if(init_core1) {
		struct xcsdh_host *host = master->sd_host[1];

		xcsdh_disable_irq(host);
		if (host->irq_requested) {
			free_irq(XCSDH_IRQ, (void *)host);
			host->irq_requested = 0;
		}
		platform_driver_unregister(&xcsdh_driver);
		platform_device_unregister(xcsdh_device);
		if (xcsdh_wq)
			destroy_workqueue(xcsdh_wq);
	}

	mmc_encryption_exit();

	printk("%s: XCode SD controller exit\n", __func__);
}

static void
xcsdh_wq_handler(struct work_struct *w)
{
    wait_event(xcsdh_wait, g_ready_to_init);
    xcsdh_init();
}

static int xcsdh_init_wq(void)
{
    if (!xcsdh_wq)
        xcsdh_wq = create_singlethread_workqueue("xcsdh_wq");
    if (xcsdh_wq)
        queue_delayed_work(xcsdh_wq, &xcsdh_work, msecs_to_jiffies(10));

    return 0;
}

module_init(xcsdh_init_wq);
module_exit(xcsdh_exit);


