#ifndef __XC4_IRDA_h__
#define __XC4_IRDA_h__

#include <linux/semaphore.h>
#include <linux/list.h>
#include <linux/timer.h>


#define IRC_MAGIC   0xfe12ab89

typedef struct {
    unsigned int magic;
    int on;
    unsigned int pattern[8];
    unsigned int mask[8];
} IRC_PATTERN_MATCH_INFO;

#define XC4IRC_IOC_MAGIC 'i'
/*
 * S means "Set" through a ptr
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */
#define XC4IRC_IOCT_TX_CARRIER_LOW_CFG \
	_IOW(XC4IRC_IOC_MAGIC, 0x0001, unsigned int)
#define XC4IRC_IOCT_TX_CARRIER_HIGH_CFG \
	_IOW(XC4IRC_IOC_MAGIC, 0x0002, unsigned int)
#define XC4IRC_IOCT_IOC_RX_BIT_TIME \
	_IOW(XC4IRC_IOC_MAGIC, 0x0003, unsigned int)
#define XC4IRC_IOCT_IOC_RX_MAX_SPACE \
	_IOW(XC4IRC_IOC_MAGIC, 0x0004, unsigned int)
#define XC4IRC_IOCT_PATTERN_MATCH \
	_IOW(XC4IRC_IOC_MAGIC, 0x0005, unsigned int)


#define XC4IRC_IRQ XCODE6_IRQ_IRC
#define XC4IRC_DRV_NAME "xc_irda"

#ifdef CONFIG_ARCH_ARC
#define CPU_INTERRUPT1_MASK MIPS3_INTERRUPT1_MASK
#define CPU_INTERRUPT1_MASK_IRC_INT_SHIFT MIPS3_INTERRUPT1_MASK_IRC_INT_SHIFT
#define CPU_INTERRUPT1 MIPS3_INTERRUPT1
#define CPU_INTERRUPT1_IRC_INT_MASK MIPS3_INTERRUPT1_IRC_INT_MASK
#else
#ifdef CONFIG_CPU_MIPS32
#define CPU_INTERRUPT1_MASK MIPS5_INTERRUPT1_MASK
#define CPU_INTERRUPT1_MASK_IRC_INT_SHIFT MIPS5_INTERRUPT1_MASK_IRC_INT_SHIFT
#define CPU_INTERRUPT1 MIPS5_INTERRUPT1
#define CPU_INTERRUPT1_IRC_INT_MASK MIPS5_INTERRUPT1_IRC_INT_MASK
#endif	/* CONFIG_CPU_MIPS32 */
#endif	/* CONFIG_ARCH_ARC */




/* stole from LIRC */
#define XC4IRC_DEV_MAJOR 61
#define XC4IRC_DEV_COUNT 1

#define XC4IRC_RX_BUF_SIZE 8192
#define XC4IRC_RX_BUF_DESC_NR 128

#define XC4IRC_RX_TIMER_DEFAULT HZ

/* 1/10s, 100ms */
#define XC4IRC_PATT_BH_TIMER_DEFAULT (HZ / 10)

struct vixs_irc_rxbuf_desc {
	u32 rx_buf_offset;
	u32 rx_bits_count;
	struct list_head list;
};

struct vixs_irc 
{
	struct  cdev cdev;
    struct  device *dev;
    char    name[64];
    char    phys[64];
	dev_t   devno;

	int core_id;

	u32 send_carries;
	u32 send_duty_cycle;

	u8  current_tx_port;
	u32 carrier_low_cfg;
	u32 carrier_high_cfg;
	int features;
    u32 carrier;
    
    u32 freq;
    u32 sysclk;
    u32 duty_cycle;
    u32 bit_time;
    u32 max_space;

    struct rc_dev *rc;

	char *rx_buf;
	wait_queue_head_t inq;

	struct list_head rxbuf_descs;
	u32 rxbuf_outstanding;

	u32 pattern_match_on;
	u32 pattern_match_l;
	u32 pattern_match_h;
	u32 pattern_match_mask_l;
	u32 pattern_match_mask_h;

	struct mutex mutex;
    spinlock_t irc_spinlock;

	struct timer_list rx_timer;
    struct timer_list patt_bh_timer;

    atomic_t pattern_matched;
};

struct vixs_irc_master {
	struct vixs_irc *hosts[XC4IRC_DEV_COUNT];
	atomic_t host_opened[XC4IRC_DEV_COUNT];
};

struct vixs_irc_table {
    char name[64];
    u32 freq;
    u32 sysclk;
    u32 duty_cycle;
    u32 bit_time;
    u32 max_space;
};
    

#endif

