/*
 * vixs_ir.c - IR support for ViXS XCode series
 *
 * Copyright (c) 2015 by ViXS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/version.h>

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/serial_reg.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <asm/system.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/fcntl.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#include <media/rc-core.h>
 
#include <plat/xcodeRegDef.h>

#include "vixs_ir.h"

void rc_keydown_notimeout2(struct rc_dev *dev, int scancode, u32 keycode, u8 toggle);

#define DRIVER_NAME "vixs_ir"

//#define DEBUG

//#define DUMP_SCANCODE      // enable this to dump key sancode, to get scancode from new RC 

#define KINFO(_fmt, _args...)			\
	printk(KERN_INFO "vixs_ir:" _fmt, ## _args)
	
#define KERROR(_fmt, _args...)           \
    printk(KERN_ERR "vixs_ir:" _fmt, ## _args)

#ifdef DEBUG
#define KDEBUG(_fmt, _args...)				\
	printk(KERN_ERR "vixs_ir:" "%s: " _fmt, __func__, ## _args)
  
#else
#define KDEBUG(_fmt, _args...)
#endif

#ifdef DUMP_SCANCODE
#define JDUMPBUF(_buf, _size)						\
	do {									\
		int _i;							\
		char *c;							\
		c = (char *)(_buf);						\
		for (_i=0; _i<_size; _i++) {					\
			if ((_i != 0) && ((_i % 32) == 0) && (_i > 31 )) printk("\n");	\
			printk("0x%02x, ", c[_i]);				\
		}								\
		printk("\n");							\
	} while(0)
#else
#define JDUMPBUF(_buf, _size)
#endif

struct vixs_irc_table avex_nec_rc_parameter = 
{
    "AVEX_NEC",
    38000,
    144000000,
    3,
    562, //456 is used for test app, //562 is specification, // unit is us, 1/1000000 second 
    12,
};

struct vixs_irc_table avex_r6_rc_parameter = 
{
    "AVEX_RC6",
    36000,
    144000000,
    3,
    444, //456 is used for test app, //562 is specification, // unit is us, 1/1000000 second 
    12,
};

struct vixs_irc_table *rc_para = &avex_nec_rc_parameter;    

#define _MMFB_BASE 0x0
#define _MMREG(off)((unsigned long) ((unsigned long)XC_SOC_PROC_MMREG_BASE + ((unsigned long)(off))))


#define _MMFB_LOC(off) ((unsigned long)(_MMFB_BASE + (off)))

#define MMR_READ(reg) 				readl((void *)_MMREG(reg))
#define MMR_WRITE(data, reg)        writel( (data), (void *)(_MMREG(reg)))
#define MMFB_READ(off)              readl((void *)_MMFB_LOC(off))
#define MMFB_WRITE(data, off)       writel( (data), (void *)_MMFB_LOC(off))


/* XXX: this is fixed for FPGA testing. 
 * Will not work if the PLL clock changed
 * Also assuming the protocol using 36KHz
 */
//#define CARR_H 0x200
//#define CARR_L 0x100
/* SONY */
#define CARR_H 125
#define CARR_L 375

/* 
 * Due to a hw bug, even with pattern match on, some "noises" can still
 * come up to the driver space. Originally it can be handled by the user space
 * easily  but someone want to have a
 * "driver of omniscience, omnipresence and omnipotence",
 * so we do everything here be a friendly guy because of someone's laziness
 */
#define SW_PATTERN_MATCH_ENABLE 0


/* 
 * hw bug in Viper Eng board. If the GPIO_C_CTRL:GPIO_MODE_SEL2 set 
 * (GPIO MODE), the TX port will be driven high and the transmitter
 * keep generating signals. Set this flag for forcing the driver
 * retain the GPIO_C_CTRL in HW mode for IRDA ports
 */
#define SW_WORKAROUND_HOLD_GPIO_C_IRDA_MODE 0


DECLARE_COMPLETION(tx_complete);

//static atomic_t vixs_irc_opened;
//struct vixs_irc host;

struct vixs_irc_master master;

static int vixs_irc_reset_hw(struct vixs_irc *host, int forced)
{
	volatile unsigned long s = 0;
	int i;
	int sth_opened = 0;

	for (i = 0; i < XC4IRC_DEV_COUNT; i++) {
		sth_opened |= atomic_read(& (master.host_opened[i]));
	}

	if (! sth_opened || forced) {
		/* Clear interrupt status */
		MMR_WRITE(1, IRC_TX_INT_STATUS);
		MMR_WRITE(3, IRC_RX0_INT_STATUS);

		/* first dev open, lets init the whole block */

		KDEBUG("first open, init whole block\n");

		// for low power mode, enable IRC clk
		s = MMR_READ(ACC_BLK_STOP0);
		clear_bit(IRC_BLK_STOP_SHIFT, &s);
		MMR_WRITE(s, ACC_BLK_STOP0);

		/* Set IRC_RESET */
		s = MMR_READ(ACC_RESET_REG0);
		set_bit(IRC_RESET_SHIFT, &s);
		MMR_WRITE(s, ACC_RESET_REG0);

		/* Retain the GPIO */
		s = MMR_READ(GPIO_C_CTRL);
		//	clear_bit(GPIO_C_CTRL_GPIO_MODE_SEL2_SHIFT, &s);
		clear_bit(29, &s);	/* XXX the RDF in VDS and kernel is NOT sync! */
		MMR_WRITE(s, GPIO_C_CTRL);

		/* disable loopback */
		MMR_WRITE(0, IRC_RX0_LPBK_EN);

		/* Take out IRC_RESET */
		s = MMR_READ(ACC_RESET_REG0);
		clear_bit(IRC_RESET_SHIFT, &s);
		MMR_WRITE(s, ACC_RESET_REG0);

		/* Enable IRQ to MIPS */
#ifdef USE_IIA
        IIALocalSetMask(IIA_IRC_INT);
#endif
	}

	switch (host->core_id) {
		case 0:
			/* Clear interrupt status */
			MMR_WRITE(1, IRC_TX_INT_STATUS);
			MMR_WRITE(3, IRC_RX0_INT_STATUS);

			/* Set RX Config
			 * Demodulate mode, Active Low
			 * Sample all bit times
			 */
			MMR_WRITE(IRC_RX0_CFG_ONE_QUART_SAMP_EN_MASK |
                IRC_RX0_CFG_TWO_QUART_SAMP_EN_MASK |
                IRC_RX0_CFG_THR_QUART_SAMP_EN_MASK
            /* Set this if you want to enable FIFO fill level interrupt */
            /* | (level & IRC_RX0_CFG_FIFO_FILL_THRESH) */
                , IRC_RX0_CFG);
			//		MMR_WRITE(0x71, IRC_RX_CFG);

			/* Only need to setup for host0. as only it has RX */
			/* Set up RX hardware buffer */
			KDEBUG("virt_to_phys(host.rx_buf) %p, host.rx_buf %p\n",
					(void *)virt_to_phys(master.hosts[0]->rx_buf),
					master.hosts[0]->rx_buf);
			MMR_WRITE(virt_to_phys(master.hosts[0]->rx_buf),
					IRC_RX0_LOWER_ADR);
			MMR_WRITE(XC4IRC_RX_BUF_SIZE, IRC_RX0_ADR_SIZE);

			/* XXX:
			 * This reg is protocol depenedance
			 * We should set extra API in IOCTL,
			 * or the hardware has a better way to do this?
			 */
			MMR_WRITE(32, IRC_RX0_MAX_SPACE); /* SONY */

			/* Enable IR block */
			MMR_WRITE(0, IRC_TX_CTRL);
			MMR_WRITE(0, IRC_RX0_CTRL);

			MMR_WRITE(1, IRC_TX_INT_EN);

			if (host->pattern_match_on) {
				/* bit0 RX_INT_EN off
				 * bit1 PATT_MATCH_EN on
				 */
				MMR_WRITE(IRC_RX0_INT_EN_PATT_MATCH_EN_MASK
/*                   | IRC_RX0_INT_EN_FIFO_FILL_THRESH_EN_MASK */,
                    IRC_RX0_INT_EN);
			} else {
				MMR_WRITE(IRC_RX0_INT_EN_INT_EN_MASK, IRC_RX0_INT_EN);
                for (i=0;i<8;i++)
                {
	    			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                        (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                        (0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                        IRC_RX0_PATT_MATCH_IDX);
	    			MMR_WRITE(0, IRC_RX0_PATT_MATCH_WR_DATA);

	    			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                        (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                        (1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                        IRC_RX0_PATT_MATCH_IDX);
	    			MMR_WRITE(0, IRC_RX0_PATT_MATCH_WR_DATA);
                }
			}

			break;

        #ifdef CONFIG_PLAT_XCODE64xx
		case 1:
			/* Clear interrupt status */
			MMR_WRITE(1, IRC_TX1_INT_STATUS);

			/* XXX:
			 * This reg is protocol depenedance
			 * We should set extra API in IOCTL,
			 * or the hardware has a better way to do this?
			 */
			MMR_WRITE(32, IRC_RX0_MAX_SPACE); /* SONY */

			/* Enable IR block */
			MMR_WRITE(0, IRC_TX1_CTRL);

			MMR_WRITE(1, IRC_TX1_INT_EN);


			break;
        #endif

	}


	return (0);
}

static unsigned int vixs_irc_read_data_fb(u32 rx_buf_offset,
		u32 rx_bits_count,
		char **buf)
{
	unsigned int buf_len;
	unsigned int buf_flush_len;
	unsigned int buf_start;
	char *b;


	buf_len = rx_bits_count / 8;
	if ((rx_bits_count % 8) != 0)
		buf_len ++;

	/* test and set 32bits align */
	if ((buf_len & 0x3) == 0) {
		buf_flush_len = buf_len;
	} else {
		buf_flush_len = (buf_len + 4) & 0xfffffffc;
	}
	KDEBUG("bit_cnt %d, buf_len = %d, buf_flush_len = %d\n",
			rx_bits_count,
			buf_len,
			buf_flush_len);


	buf_start = MMR_READ(IRC_RX0_LOWER_ADR) + rx_buf_offset;
	inv_dcache_range((u32)phys_to_virt(buf_start), 
			(u32)phys_to_virt((buf_start + buf_flush_len)));

	/* XXX:
	 * This may be a bug in the ARC kernel
	 * the PHYS_SRAM_OFFSET and PAGE_OFFSET are the same
	 * hence the convertion fail. 
	 * see asm/page.h
	 */
	b = phys_to_virt(buf_start);
	*buf = b;

	return (buf_len);
}

#if SW_PATTERN_MATCH_ENABLE
/* This is what the HW pattern filter should do
 * but it get bug in the irq generation, we make a safe net
 * for them
 */
static int vixs_irc_sw_pattern_match(struct vixs_irc *host, 
		char *buf, unsigned int len)
{
	u32 pattern_match_l;
	u32 pattern_match_h;
	u32 pattern_match_mask_l;
	u32 pattern_match_mask_h;
	u32 pl, ph;

	char buf2[8];
	u32 *word_buf;

	/* 
	 * Can omit the memcpy for speed, but handle the
	 * casting of the buffer smaller than word size is 
	 * messy. Can divide a special case path for this
	 * if memcpy is *strictly prohibited*
	 */
	memset(buf2, 0, 8);
	if (len <= 8) 
		memcpy(buf2, buf, len);
	else
		memcpy(buf2, buf, 8);

	/* XXX: will endian of reg and fb not match? */
	word_buf = (u32 *) buf2;

	KDEBUG("word_buf[0] 0x%08x, word_buf[1] 0x%08x\n",
			word_buf[0], word_buf[1]);

	if (host->pattern_match_on) {
		pattern_match_l = host->pattern_match_l;
		pattern_match_h = host->pattern_match_h;
		pattern_match_mask_l = host->pattern_match_mask_l;
		pattern_match_mask_h = host->pattern_match_mask_h;

		pl = word_buf[0] & pattern_match_mask_l;
		ph = word_buf[1] & pattern_match_mask_h;
		if (pl == pattern_match_l && ph == pattern_match_h)
			return 1;
		else
			return 0;

	} else {
		return 1;       /* no pattern match, always matched */
	}

	return (0);
}
#endif

static void vixs_irc_stop_hw(struct vixs_irc *hostp)
{
	volatile unsigned long s = 0;

	int i;
	int sth_opened = 0;

	switch (hostp->core_id) {
		case 0:
			/* Clear interrupt status */
			MMR_WRITE(1, IRC_TX_INT_STATUS);
			MMR_WRITE(3, IRC_RX0_INT_STATUS);

			/* Disable IR block */
			MMR_WRITE(1, IRC_TX_CTRL);
			MMR_WRITE(1, IRC_RX0_CTRL);
			break;
            
        #ifdef CONFIG_PLAT_XCODE64xx
		case 1:
			/* Clear interrupt status */
			MMR_WRITE(1, IRC_TX1_INT_STATUS);

			/* Disable IR block */
			MMR_WRITE(1, IRC_TX1_CTRL);
			break;
        #endif
	}

	for (i = 0; i < XC4IRC_DEV_COUNT; i++) {
		sth_opened |= atomic_read(& (master.host_opened[i]));
	}

	if (! sth_opened) {
		/* this is the last dev to close. off the hw */

		KDEBUG("Last close core id %d\n", hostp->core_id);

#if ! SW_WORKAROUND_HOLD_GPIO_C_IRDA_MODE
		/* release the GPIO */
		s = MMR_READ(GPIO_C_CTRL);
		//	set_bit(GPIO_C_CTRL_GPIO_MODE_SEL2_SHIFT, &s);
		set_bit(29, &s);
		MMR_WRITE(s, GPIO_C_CTRL);
#endif

		/* Set IRC_RESET */
		s = MMR_READ(ACC_RESET_REG0);
		set_bit(IRC_RESET_SHIFT, &s);
		MMR_WRITE(s, ACC_RESET_REG0);

		// for low power mode, disable IRC clk
		s = MMR_READ(ACC_BLK_STOP0);
		set_bit(IRC_BLK_STOP_SHIFT, &s);
		MMR_WRITE(s, ACC_BLK_STOP0);

	}


}

static void vixs_irc_pop_rx_fifo(struct vixs_irc *hostp,
		int rx_seq_nr)
{
	int i;
	volatile u32 rx_data0 = 0;
	volatile u32 rx_data1 = 0;
	struct vixs_irc_rxbuf_desc *new_rx_desc = NULL;

	u32 rx_buf_offset = 0;
	u32 rx_bits_count = 0;
#if SW_PATTERN_MATCH_ENABLE
	char *buf;
	unsigned int buf_len;
	int sw_patt_matched = 0;
#endif

	for (i = 0; i < rx_seq_nr; i++) {
		/* Pop the data */
		rx_data0 = MMR_READ(IRC_RX0_DATA0);
		set_bit(IRC_RX0_DATA0_ENTRY_POP_SHIFT, (volatile long unsigned int *)&rx_data0);
		MMR_WRITE(rx_data0, IRC_RX0_DATA0);

		/* re-read it */
		rx_data0 = MMR_READ(IRC_RX0_DATA0);
		rx_buf_offset =
			rx_data0 & IRC_RX0_DATA0_OFFSET_MASK;
		rx_bits_count =
			(rx_data0 & IRC_RX0_DATA0_BIT_CNT_MASK) >> IRC_RX0_DATA0_BIT_CNT_SHIFT;

#if SW_PATTERN_MATCH_ENABLE
		buf_len = vixs_irc_read_data_fb(rx_buf_offset,
				rx_bits_count,
				&buf);
		JDUMPBUF(buf, buf_len);

		sw_patt_matched = vixs_irc_sw_pattern_match(hostp, buf, buf_len);
		KDEBUG("sw_patt_matched %s\n", sw_patt_matched ? "yes" : "no");
		if (! sw_patt_matched) {
			continue;
		}
#endif

		new_rx_desc =
			kmalloc(sizeof(struct vixs_irc_rxbuf_desc), GFP_ATOMIC);
		if (!new_rx_desc) {
			KERROR("rxbuf_desc alloc failed!\n");
			break;
			/* TODO: Handle this error better */
		}

		list_add_tail(&(new_rx_desc->list),
				&(hostp->rxbuf_descs));

		new_rx_desc->rx_buf_offset =
			rx_data0 & IRC_RX0_DATA0_OFFSET_MASK;
		new_rx_desc->rx_bits_count =
			(rx_data0 & IRC_RX0_DATA0_BIT_CNT_MASK) >> IRC_RX0_DATA0_BIT_CNT_SHIFT;

		hostp->rxbuf_outstanding ++;

		KDEBUG("RX Data0 [%d] 0x%08x, rx_buf_offset 0x%08x bit cnt %d\n",
				i, rx_data0, new_rx_desc->rx_buf_offset, new_rx_desc->rx_bits_count);

		rx_data1 = MMR_READ(IRC_RX0_DATA1);
		KDEBUG("RX Data1 [%d] 0x%08x\n",
				i, rx_data1);
	}

}

static irqreturn_t vixs_irc_irq_handler(int irq, void *dev_id)
{
	volatile u32 int_sts = 0;
	volatile u32 rx_fifo_sts = 0;
	volatile u32 temp = 0;
	//volatile unsigned long rx_data0 = 0;
	//volatile unsigned long rx_data1 = 0;
	int rx_seq_nr = 0;
	int j;
	//u32 rx_idx = 0;
	irqreturn_t ret=IRQ_NONE;

	//struct vixs_irc_rxbuf_desc *new_rx_desc = NULL;

    KDEBUG("vixs_irc_irq_handler\n");
    
#ifdef USE_IIA
	if (IIALocalReadInt(IIA_IRC_INT)) {
#endif
		for(j=0;j<XC4IRC_DEV_COUNT;j++)
		{
			struct vixs_irc *hostp =
				(struct vixs_irc *) master.hosts[j];

			KDEBUG("Get irq %d with dev %p core id %d\n",
				irq, dev_id, hostp->core_id);

			switch (hostp->core_id) {
				case 0:
					int_sts = MMR_READ(IRC_TX_INT_STATUS);
					break;

                #ifdef CONFIG_PLAT_XCODE64xx
				case 1:
					int_sts = MMR_READ(IRC_TX1_INT_STATUS);
					break;
                #endif
			}

			/* if host 0, we need to check the RX as well */
			if (int_sts == 0 && hostp->core_id != 0) {
				continue;
			}

			if (int_sts == 0x01) {
				KDEBUG("TX_STATUS 0x%08x\n", int_sts);

				switch (hostp->core_id) {
					case 0:
						MMR_WRITE(1, IRC_TX_INT_STATUS);
						break;

                    #ifdef CONFIG_PLAT_XCODE64xx
					case 1:
						MMR_WRITE(1, IRC_TX1_INT_STATUS);
						break;
                    #endif
				}

				complete(&tx_complete);
			} else {
				int_sts = MMR_READ(IRC_RX0_INT_STATUS);
				KDEBUG("RX0_STATUS 0x%08x\n", int_sts);

				temp = MMR_READ(IRC_RX0_INT_EN);
				KDEBUG("RX0_INT_EN 0x%08x\n", temp);

				if ((int_sts & IRC_RX0_INT_STATUS_INT_STATUS_MASK) ||
						(int_sts & IRC_RX0_INT_STATUS_PATT_MATCH_STATUS_MASK)) {

					rx_fifo_sts = MMR_READ(IRC_RX0_DATA_FIFO_STATUS);
					rx_seq_nr = rx_fifo_sts & IRC_RX0_DATA_FIFO_STATUS_NUM_OUTSTANDING_SEQ_MASK;
					KDEBUG("rx_fifo_sts 0x%08x, RX %d seqs\n",
							rx_fifo_sts,
							rx_seq_nr);

#if 0
					/* XXX Hardware delay? */
					/* the reg is not ready? */
					if (rx_seq_nr == 0) {
						int i = 12000;
						while (rx_seq_nr == 0) {
							/* we really need timeout, sorry */
							if (i <= 0)
								break;

							rx_fifo_sts = MMR_READ(IRC_RX_DATA_FIFO_STATUS);
							rx_seq_nr = rx_fifo_sts & RX_NUM_OUTSTANDING_SEQ_MASK;

							i--;
							udelay(10);
							/* XXX total wait 120ms */
						}
						KDEBUG("We polled the fifo %d loops\n",
								12000 - i);
					}
#endif

					if (hostp->pattern_match_on && rx_seq_nr == 0) {
						/* XXX Hardware bug: patt match IRQ 
						 * come too earily*/
						if ((int_sts & IRC_RX0_INT_STATUS_PATT_MATCH_STATUS_MASK)) {

							if (atomic_read(&hostp->pattern_matched) == 0) {
								hostp->patt_bh_timer.expires = jiffies + XC4IRC_PATT_BH_TIMER_DEFAULT;
								add_timer(&hostp->patt_bh_timer);

								atomic_set(&hostp->pattern_matched, 1);
							}
							MMR_WRITE(int_sts, IRC_RX0_INT_STATUS);
							ret=IRQ_RETVAL(IRQ_HANDLED);
							continue;
						}
					}

					if (hostp->rxbuf_outstanding >= 128) {
						KINFO("RX buffer full. No longer receive data\n");
						MMR_WRITE(0x3, IRC_RX0_INT_STATUS);
						ret=IRQ_RETVAL(IRQ_HANDLED);
						continue;
					}

					vixs_irc_pop_rx_fifo(hostp, rx_seq_nr);

					MMR_WRITE(0x3, IRC_RX0_INT_STATUS);

                    KDEBUG("wake_up recv\n");
					wake_up_interruptible(&(hostp->inq));

				} else if (int_sts & IRC_RX0_INT_STATUS_FIFO_FILL_THRESH_STATUS_MASK) {
                    //clear it now...nothing should be done with this interrupt yet
					MMR_WRITE(IRC_RX0_INT_STATUS_FIFO_FILL_THRESH_STATUS_MASK, IRC_RX0_INT_STATUS);
                } else {
					KERROR("We got an IRQ in CPU but not in IRC core!\n");
					continue;
				}
			}

			ret=IRQ_RETVAL(IRQ_HANDLED);
		}
#ifdef USE_IIA
	}
#endif
	return ret;

}

static int vixs_irc_open(struct inode *inode, struct file *file)
{
	unsigned minor;

	KDEBUG("Open with nonblock: %s\n",
			((file->f_flags & O_NONBLOCK) ? "yes" : "no"));

	/* We assume the minor number is matching the core ids */
	minor = iminor(inode);
	KDEBUG("%s opened with minor %d\n",
			XC4IRC_DRV_NAME, minor);

	if (atomic_read(&(master.host_opened[minor]))) {
		KDEBUG("%s is using by another process\n",
				XC4IRC_DRV_NAME);
		return(-EBUSY);
	}


	//	file->private_data = &(host);
	file->private_data = master.hosts[minor];

	/* Set IRC_RESET */
	vixs_irc_reset_hw(master.hosts[minor], 0);

	//	host.rxbuf_outstanding = 0;
	master.hosts[minor]->rxbuf_outstanding = 0;

	atomic_set(&(master.host_opened[minor]), 1);

	return (0);
}

static int vixs_irc_close(struct inode *inode, struct file *filp)
{
	struct list_head *lp;
	struct list_head *n;
//	struct vixs_irc_rxbuf_desc *deadpool[XC4IRC_RX_BUF_DESC_NR];
	struct vixs_irc *hostp;
	int i = 0;

	hostp = (struct vixs_irc *) filp->private_data;

	KINFO("%s closed\n", XC4IRC_DRV_NAME);

	atomic_set(& (master.host_opened[hostp->core_id]), 0);

	vixs_irc_stop_hw(hostp);

	KDEBUG("hostp %p, &(hostp->rxbuf_descs) %p, (&(hostp->rxbuf_descs))->next %p\n",
			hostp , &(hostp->rxbuf_descs), (&(hostp->rxbuf_descs))->next);	  

	list_for_each_safe(lp, n, &(hostp->rxbuf_descs)) {
		list_del(lp);
		kfree(list_entry(lp, struct vixs_irc_rxbuf_desc, list));
		i++;
	}
	if (i > 0)
		KINFO("%d rx data outstanding. Free up now\n", i);

	atomic_set(&hostp->pattern_matched, 0);
	del_timer(&hostp->rx_timer);
	del_timer(&hostp->patt_bh_timer);

	return (0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static int vixs_irc_ioctl(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
#else
static long vixs_irc_ioctl(struct file *filp,	unsigned int cmd, unsigned long arg)
#endif
{
    IRC_PATTERN_MATCH_INFO *info;
	struct vixs_irc *hostp;
	int core_id;
	int err = 0, i;

	hostp = (struct vixs_irc *) filp->private_data;
	core_id = hostp->core_id;

	KDEBUG("IOCTL cmd 0x%08x arg %lu\n",
			cmd, arg);

	switch (cmd) {
		case XC4IRC_IOCT_TX_CARRIER_LOW_CFG:
			hostp->carrier_low_cfg = arg;
			switch (core_id) {
				default:
				case 0:
                    #ifdef CONFIG_PLAT_XCODE68xx
                    hostp->carrier_low_cfg = hostp->carrier_low_cfg*162/144;
                    #endif
					MMR_WRITE(hostp->carrier_low_cfg, 
							IRC_TX_CARRIER_LOW_CFG);
					break;
                    
                #ifdef CONFIG_PLAT_XCODE64xx
				case 1:
					MMR_WRITE(hostp->carrier_low_cfg, 
							IRC_TX1_CARRIER_LOW_CFG);
					break;
                #endif
			}
			break;

		case XC4IRC_IOCT_TX_CARRIER_HIGH_CFG:
			hostp->carrier_high_cfg = arg;
			switch (core_id) {
				default:
				case 0:
                    #ifdef CONFIG_PLAT_XCODE68xx
                    hostp->carrier_high_cfg = hostp->carrier_high_cfg*162/144;
                    #endif
					MMR_WRITE(hostp->carrier_high_cfg,
							IRC_TX_CARRIER_HIGH_CFG);
					break;
                    
                #ifdef CONFIG_PLAT_XCODE64xx
				case 1:
					MMR_WRITE(hostp->carrier_high_cfg,
							IRC_TX1_CARRIER_HIGH_CFG);
					break;
                #endif
			}
			break;

		case  XC4IRC_IOCT_IOC_RX_BIT_TIME:
            #ifdef CONFIG_PLAT_XCODE68xx
            arg=arg*162/144;
            #endif
			if (core_id == 0)
				MMR_WRITE(arg, IRC_RX0_BIT_TIME);
			break;

		case  XC4IRC_IOCT_IOC_RX_MAX_SPACE:
			if (core_id == 0)
				MMR_WRITE(arg, IRC_RX0_MAX_SPACE);
			break;

		case XC4IRC_IOCT_PATTERN_MATCH:
            info=(IRC_PATTERN_MATCH_INFO *)arg;
            if(info->magic!=IRC_MAGIC)
                return -EINVAL;
			/* only core 0 have RX and pattern match */
			if (hostp->core_id == 0) {
				if (hostp->pattern_match_on != info->on) {
					hostp->pattern_match_on = info->on;

					/* interrupt mode need change */
					err = vixs_irc_reset_hw(hostp, 0);
				}
				if (hostp->pattern_match_on) {
					KDEBUG("Start Timer\n");
					hostp->rx_timer.expires = jiffies + 
						XC4IRC_RX_TIMER_DEFAULT;
                    for (i=0;i<8;i++)
                    {
    	    			MMR_WRITE(info->pattern[i], IRC_RX0_PATT_MATCH_WR_DATA);
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (0<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
    
	    			    MMR_WRITE(info->mask[i], IRC_RX0_PATT_MATCH_WR_DATA);
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (0<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
                    }
#if 0
                    for (i=0;i<8;i++)
                    {
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
    	    			KDEBUG("patt %d = 0x%08x\n", i, MMR_READ(IRC_RX0_PATT_MATCH_RD_DATA));
                    }
                    for (i=0;i<8;i++)
                    {
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
    	    			KDEBUG("mask %d = 0x%08x\n", i, MMR_READ(IRC_RX0_PATT_MATCH_RD_DATA));
                    }
#endif
					add_timer(&hostp->rx_timer);
				} else {
					KDEBUG("Del Timer\n");
                    for (i=0;i<8;i++)
                    {
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
    	    			MMR_WRITE(0, IRC_RX0_PATT_MATCH_WR_DATA);
    
	        			MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
                            (1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
                            IRC_RX0_PATT_MATCH_IDX);
	    			    MMR_WRITE(0, IRC_RX0_PATT_MATCH_WR_DATA);
                    }
					del_timer(&hostp->rx_timer);
				}

			}
			break;

#if 0
		case XC4IRC_IOCT_PATTERN_MATCH_L:
			hostp->pattern_match_l = arg;
			KDEBUG("patt low 0x%08x\n", arg);
			MMR_WRITE(hostp->pattern_match_l, IRC_RX0_PATT_MATCH_L);
			break;

		case XC4IRC_IOCT_PATTERN_MATCH_H:
			hostp->pattern_match_h = arg;
			KDEBUG("patt high 0x%08x\n", arg);
			MMR_WRITE(hostp->pattern_match_h, IRC_RX0_PATT_MATCH_H);
			break;

		case XC4IRC_IOCT_PATTERN_MATCH_MASK_L:
			hostp->pattern_match_mask_l = arg;
			MMR_WRITE(hostp->pattern_match_mask_l,
					IRC_RX0_PATT_MATCH_MASK_L);
			break;

		case XC4IRC_IOCT_PATTERN_MATCH_MASK_H:
			hostp->pattern_match_mask_h = arg;
			MMR_WRITE(hostp->pattern_match_mask_h,
					IRC_RX0_PATT_MATCH_MASK_H);
			break;
#endif
	}

	return (0);
}

static ssize_t vixs_irc_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct vixs_irc *hostp;
	unsigned int buf_len;
	//unsigned int buf_flush_len;
	//unsigned int buf_start;
	char *b;
	//u32 rx_idx = 0;
	struct vixs_irc_rxbuf_desc *rx_desc = NULL;
	struct list_head *lp;
	int i = 0;

	hostp = filp->private_data;

	KDEBUG("START\n");

	/* only core 0 have RX */
	if (hostp->core_id != 0) {
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&(hostp->mutex)))
		return -ERESTARTSYS;


	KDEBUG("start rxbuf_outstanding %d\n", 
			hostp->rxbuf_outstanding);

	if (hostp->rxbuf_outstanding <= 0) {

		if (filp->f_flags & O_NONBLOCK) {
			mutex_unlock(&(hostp->mutex));
			return (-EAGAIN);
		}

		/* Must wait */
		if (wait_event_interruptible(hostp->inq,
					(hostp->rxbuf_outstanding > 0))) {
			mutex_unlock(&(hostp->mutex));

			return (-ERESTARTSYS);
		}

	}

	KDEBUG("after wait rxbuf_outstanding %d\n", 
			hostp->rxbuf_outstanding);

	/* return from wait */

	lp = hostp->rxbuf_descs.next;
	rx_desc = list_entry(lp, struct vixs_irc_rxbuf_desc, list);

	buf_len = vixs_irc_read_data_fb(rx_desc->rx_buf_offset,
			rx_desc->rx_bits_count,
			&b);

	JDUMPBUF(b, buf_len);

	/* Some body reported that they saw extra zeros at the end of
	 * a sequence. The sequence length is reported by hardware with 
	 * bits count and I have no idea why hardware likes to generate 
	 * few more zeros at the end, may be noise, may be aligment.
	 * I do not care as the application should always handle their
	 * protocol-engine and parse the raw data. But they expected a
	 * philosopher's stone-like kernel driver, so I do this for the
	 * lamentable case
	 */
	if (buf_len > count)
		buf_len = count;

	for (i = (buf_len-1); i >=0; i--) {
		if (b[i] != 0x00) {
			break;
		}
	}
	KDEBUG("i = %d, chop %d bytes from tail\n", i, buf_len - (i+1));

	buf_len = i + 1;

	copy_to_user(buf, b, buf_len);

	list_del(lp);
	kfree(rx_desc);
	hostp->rxbuf_outstanding --;

	//	hostp->rx_bits_count = 0;

	mutex_unlock(&(hostp->mutex));

	return (buf_len);
}

static ssize_t vixs_irc_write(struct file *filp, const char __user *buf, 
		size_t count, loff_t *f_pos)
{
	struct vixs_irc *hostp;
	unsigned int *instructions;
	unsigned int inst_end;
	//int i;
	int inst_nr = 0;
	u32 tx_base = 0;
	u32 tx_ins_desc = 0;

	int core_id = 0;

	hostp = filp->private_data;
	core_id = hostp->core_id;


	if (mutex_lock_interruptible(&(hostp->mutex)))
		return -ERESTARTSYS;

	KDEBUG("write call");

	instructions = (unsigned int *) kmalloc(count, GFP_KERNEL);
	if (!instructions) {
		KINFO("Cannot allocate buffer for instructions\n");
		mutex_unlock(&(hostp->mutex));
		return (-ENOMEM);
	}


	copy_from_user((void *)instructions, buf, count);
	inst_nr = count / sizeof(unsigned int);

	/*
		 for (i = 0; i < inst_nr; i++) {
		 printk("0x%08x\n", instructions[i]);
		 }
	 */

	/* MIPS kernel cache.h do the macro unsafely
	 * Work around it
	 */
	/*
		 flush_dcache_range(instructions,
		 ((char *)instructions) + length);
	 */
	inst_end = (unsigned int)((char *) instructions) + count;
	flush_dcache_range(((unsigned int)instructions), inst_end);

	tx_base = virt_to_phys(instructions);
	tx_ins_desc = inst_nr << TX_NUM_OF_INS_SHIFT;
	KDEBUG("tx_base 0x%08x, tx_ins_desc 0x%08x\n",
			tx_base, tx_ins_desc);

	switch (core_id) {
		default:
		case 0:
			MMR_WRITE(tx_base, IRC_TX_BASE_ADR);
			MMR_WRITE(tx_ins_desc, IRC_TX_INS_DESC);
			MMR_WRITE(1, IRC_TX_RUN);
			break;

        #ifdef CONFIG_PLAT_XCODE64xx
		case 1:
			MMR_WRITE(tx_base, IRC_TX1_BASE_ADR);
			MMR_WRITE(tx_ins_desc, IRC_TX1_INS_DESC);
			MMR_WRITE(1, IRC_TX1_RUN);
			break;
        #endif
	}


	wait_for_completion(&tx_complete);

	kfree(instructions);

	mutex_unlock(&(hostp->mutex));
	return (count);

}

unsigned int vixs_irc_poll(struct file *filp, poll_table *wait)
{
	struct vixs_irc *hostp;
	unsigned int mask = 0;

	hostp = filp->private_data;

	KDEBUG("Before poll_wait\n");
	poll_wait(filp, &(hostp->inq), wait);
	KDEBUG("After poll_wait, rxbuf_outstanding %d\n",
			hostp->rxbuf_outstanding);

	if (hostp->rxbuf_outstanding > 0) {
		/* have data readable, tell them */
		mask = (POLLIN | POLLRDNORM);
	}

	KDEBUG("return mask 0x%08x\n", mask);

	return (mask);
}

void vixs_irc_patt_bh_timer_fn(unsigned long arg)
{
	struct vixs_irc *host;
	volatile u32 rx_fifo_sts = 0;
	volatile u32 int_sts = 0;
	//volatile unsigned long rx_data0 = 0;
	int rx_seq_nr = 0;
	//int i;

	KDEBUG("PATTERN Matching BH start\n");

	host = (struct vixs_irc *) arg;

	/* sanity test */
	if (host->core_id != 0) {
		goto out;
	}

	if (!host->pattern_match_on) {
		goto out;
	}

	int_sts = MMR_READ(IRC_RX0_INT_STATUS);
	KDEBUG("RX_STATUS 0x%08x\n", int_sts);
	//        if (int_sts & PATT_MATCH_STATUS_MASK) {
	if (atomic_read (&host->pattern_matched)) {

		rx_fifo_sts = MMR_READ(IRC_RX0_DATA_FIFO_STATUS);
		rx_seq_nr = rx_fifo_sts & IRC_RX0_DATA_FIFO_STATUS_NUM_OUTSTANDING_SEQ_MASK;

		if (rx_seq_nr <= 0) {
			KDEBUG("We still have no full seq...\n");
			goto out;
		}

		/* let main irq handler handle this */
		if (host->rxbuf_outstanding >= 128) {
			goto out;
		}

		vixs_irc_pop_rx_fifo(host, rx_seq_nr);

		wake_up_interruptible(&(host->inq));
	}

out:
	//        MMR_WRITE(int_sts, IRC_RX_INT_STATUS);
	//        MMR_WRITE(3, IRC_RX_INT_STATUS);
	atomic_set(&host->pattern_matched, 0);
	return;

}

void vixs_irc_rx_timer_fn(unsigned long arg)
{
	struct vixs_irc *host;
	volatile u32 rx_fifo_sts = 0;
	volatile u32 int_sts = 0;
	volatile unsigned long rx_data0 = 0;
	int rx_seq_nr = 0;
	int i;

	KDEBUG("RX timer start\n");

	host = (struct vixs_irc *) arg;

	/* sanity test */
	if (host->core_id != 0) {
		goto resched;
	}

	if (!host->pattern_match_on) {
		goto resched;
	}

	rx_fifo_sts = MMR_READ(IRC_RX0_DATA_FIFO_STATUS);
	rx_seq_nr = rx_fifo_sts & IRC_RX0_DATA_FIFO_STATUS_NUM_OUTSTANDING_SEQ_MASK;	

	/* nothing in fifo */
	if (rx_seq_nr == 0) {
		goto resched;
	} else {
		int_sts = MMR_READ(IRC_RX0_INT_STATUS);
		KDEBUG("RX_STATUS 0x%08x\n", int_sts);
		if (int_sts & IRC_RX0_INT_STATUS_PATT_MATCH_STATUS_MASK || 
				(atomic_read (&host->pattern_matched))) {
			KDEBUG("Go resched\n");
			/* pattern matched. Let IRQ handler takes care */
			goto resched;
		} else {
			KDEBUG("POP rx_seq_nr %d\n", rx_seq_nr);
#if ! SW_PATTERN_MATCH_ENABLE
			for (i = 0; i < rx_seq_nr; i++) {
				/* Pop the data */
				rx_data0 = MMR_READ(IRC_RX0_DATA0);
				set_bit(IRC_RX0_DATA0_ENTRY_POP_SHIFT, &rx_data0);
				MMR_WRITE(rx_data0, IRC_RX0_DATA0);
			}
#else
			/* XXX: A special case of a correct sequence
			 * can be missed in the normal path
			 * 1. a noise seq arrived, RX_STATUS on
			 * 2. meanwhile a pattern arrived, but the whole 
			 * seq is not yet fin, due to a hw bug, the 
			 * PATT_MATCH is on even the seq is not finished
			 * 3. The irq handler kicks in, see the PATT_MATCH on
			 * with outstanding seq num 1 (as the first noise seq)
			 * this confused the handler thinks that noise seq is
			 * a correct one
			 * 4. pop the fifo, and the sw filter will discard the
			 * noise seq
			 * 5. irq handler clear all irq status, the info of the
			 * half-arrived pattern is lost
			 * 6. the whole seq of this pattern is arrived, but no 
			 * way can know it now as this will not gen an IRQ
			 * 7. rx timer kicks in, see the outstand seq no > 0
			 * when clean up the fifo as treat it as noise
			 * So before we clean the trash can, we take a look 
			 * inside it first to rescue the gems
			 */
			vixs_irc_pop_rx_fifo(host, rx_seq_nr);
            KDEBUG("wake up from recv timer\n");
			wake_up_interruptible(&(host->inq));
#endif
		}
	}



resched:
	host->rx_timer.expires = jiffies + XC4IRC_RX_TIMER_DEFAULT;
	add_timer(&host->rx_timer);

	return;
}


struct file_operations vixs_irc_fops = {
	.owner = THIS_MODULE,
	.open = vixs_irc_open,
	.release = vixs_irc_close,
	.write = vixs_irc_write,
	.read = vixs_irc_read,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	.ioctl = vixs_irc_ioctl,
#else
	.unlocked_ioctl = vixs_irc_ioctl,
#endif
	.poll = vixs_irc_poll,
};

#ifdef CONFIG_PM

#define SAVE_IRC_REG(reg) 	reg_saved[((reg)-IRC_TX_RUN)*4] = MMR_READ((reg))
#define RESTORE_IRC_REG(reg) 	MMR_WRITE(reg_saved[((reg)-IRC_TX_RUN)*4], (reg)) 

static u32 reg_saved[(IRC_DUMMY_REG - IRC_TX_RUN + 1)*4];
static u32 reg_pattern_saved[2][8];
static u32 reg_pattern_mask_saved[2][8];

static int xc_irda_suspend(struct platform_device *dev, pm_message_t state)
{
	int i;

  	KDEBUG("xc_irda: suspend\n");

	SAVE_IRC_REG(IRC_TX_CTRL);
	SAVE_IRC_REG(IRC_TX_CARRIER_LOW_CFG);
	SAVE_IRC_REG(IRC_TX_CARRIER_HIGH_CFG);
	SAVE_IRC_REG(IRC_TX_BASE_ADR);
	SAVE_IRC_REG(IRC_TX_INT_EN);

    #ifdef CONFIG_PLAT_XCODE64xx
	SAVE_IRC_REG(IRC_TX1_CTRL);
	SAVE_IRC_REG(IRC_TX1_CARRIER_LOW_CFG);
	SAVE_IRC_REG(IRC_TX1_CARRIER_HIGH_CFG);
	SAVE_IRC_REG(IRC_TX1_BASE_ADR);
	SAVE_IRC_REG(IRC_TX1_INT_EN);
    #endif

	SAVE_IRC_REG(IRC_RX0_CTRL);
	SAVE_IRC_REG(IRC_RX0_CFG);
	SAVE_IRC_REG(IRC_RX0_CARR_CFG0);
	SAVE_IRC_REG(IRC_RX0_CARR_CFG1);
	SAVE_IRC_REG(IRC_RX0_BIT_TIME);
	SAVE_IRC_REG(IRC_RX0_LOWER_ADR);
	SAVE_IRC_REG(IRC_RX0_ADR_SIZE);
	SAVE_IRC_REG(IRC_RX0_MAX_SPACE);
	SAVE_IRC_REG(IRC_RX0_INT_EN);
	SAVE_IRC_REG(IRC_RX0_PATT_MATCH_START_BIT);
	for (i=0;i<8;i++)
	{
		MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
				(0<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX0_PATT_MATCH_IDX);
		reg_pattern_saved[0][i]=MMR_READ(IRC_RX0_PATT_MATCH_WR_DATA);

		MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
				(0<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX0_PATT_MATCH_IDX);
		reg_pattern_mask_saved[0][i]=MMR_READ(IRC_RX0_PATT_MATCH_WR_DATA);
	}
	SAVE_IRC_REG(IRC_RX0_LPBK_EN);

    #ifdef CONFIG_PLAT_XCODE64xx
	SAVE_IRC_REG(IRC_RX1_CTRL);
	SAVE_IRC_REG(IRC_RX1_CFG);
	SAVE_IRC_REG(IRC_RX1_CARR_CFG0);
	SAVE_IRC_REG(IRC_RX1_CARR_CFG1);
	SAVE_IRC_REG(IRC_RX1_BIT_TIME);
	SAVE_IRC_REG(IRC_RX1_LOWER_ADR);
	SAVE_IRC_REG(IRC_RX1_ADR_SIZE);
	SAVE_IRC_REG(IRC_RX1_MAX_SPACE);
	SAVE_IRC_REG(IRC_RX1_INT_EN);
	SAVE_IRC_REG(IRC_RX1_PATT_MATCH_START_BIT);
	for (i=0;i<8;i++)
	{
		MMR_WRITE((i<<IRC_RX1_PATT_MATCH_IDX_IDX_SHIFT) |
				(0<<IRC_RX1_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(0<<IRC_RX1_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX1_PATT_MATCH_IDX);
		reg_pattern_saved[1][i]=MMR_READ(IRC_RX1_PATT_MATCH_WR_DATA);

		MMR_WRITE((i<<IRC_RX1_PATT_MATCH_IDX_IDX_SHIFT) |
				(0<<IRC_RX1_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(1<<IRC_RX1_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX1_PATT_MATCH_IDX);
		reg_pattern_mask_saved[1][i]=MMR_READ(IRC_RX1_PATT_MATCH_WR_DATA);
	}
	SAVE_IRC_REG(IRC_RX1_LPBK_EN);
    #endif
    
  return 0;
}

static int xc_irda_resume(struct platform_device *dev)
{
	int i;

    KDEBUG("xc_irda: resume\n");

	for (i = 0; i < XC4IRC_DEV_COUNT; i++) 
		vixs_irc_reset_hw(master.hosts[i], 1);

	RESTORE_IRC_REG(IRC_TX_CTRL);
	RESTORE_IRC_REG(IRC_TX_CARRIER_LOW_CFG);
	RESTORE_IRC_REG(IRC_TX_CARRIER_HIGH_CFG);
	RESTORE_IRC_REG(IRC_TX_BASE_ADR);
	RESTORE_IRC_REG(IRC_TX_INT_EN);

    #ifdef CONFIG_PLAT_XCODE64xx
	RESTORE_IRC_REG(IRC_TX1_CTRL);
	RESTORE_IRC_REG(IRC_TX1_CARRIER_LOW_CFG);
	RESTORE_IRC_REG(IRC_TX1_CARRIER_HIGH_CFG);
	RESTORE_IRC_REG(IRC_TX1_BASE_ADR);
	RESTORE_IRC_REG(IRC_TX1_INT_EN);
    #endif
    
	RESTORE_IRC_REG(IRC_RX0_CTRL);
	RESTORE_IRC_REG(IRC_RX0_CFG);
	RESTORE_IRC_REG(IRC_RX0_CARR_CFG0);
	RESTORE_IRC_REG(IRC_RX0_CARR_CFG1);
	RESTORE_IRC_REG(IRC_RX0_BIT_TIME);
	RESTORE_IRC_REG(IRC_RX0_LOWER_ADR);
	RESTORE_IRC_REG(IRC_RX0_ADR_SIZE);
	RESTORE_IRC_REG(IRC_RX0_MAX_SPACE);
	RESTORE_IRC_REG(IRC_RX0_INT_EN);
	RESTORE_IRC_REG(IRC_RX0_PATT_MATCH_START_BIT);
	for (i=0;i<8;i++)
	{
		MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
				(1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(0<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX0_PATT_MATCH_IDX);
		MMR_WRITE(reg_pattern_saved[0][i], IRC_RX0_PATT_MATCH_WR_DATA);

		MMR_WRITE((i<<IRC_RX0_PATT_MATCH_IDX_IDX_SHIFT) |
				(1<<IRC_RX0_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(1<<IRC_RX0_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX0_PATT_MATCH_IDX);
		MMR_WRITE(reg_pattern_mask_saved[0][i], IRC_RX0_PATT_MATCH_WR_DATA);
	}
	RESTORE_IRC_REG(IRC_RX0_LPBK_EN);

    #ifdef CONFIG_PLAT_XCODE64xx
	for (i=0;i<8;i++)
	{
		MMR_WRITE((i<<IRC_RX1_PATT_MATCH_IDX_IDX_SHIFT) |
				(1<<IRC_RX1_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(0<<IRC_RX1_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX1_PATT_MATCH_IDX);
		MMR_WRITE(reg_pattern_saved[1][i], IRC_RX1_PATT_MATCH_WR_DATA);

		MMR_WRITE((i<<IRC_RX1_PATT_MATCH_IDX_IDX_SHIFT) |
				(1<<IRC_RX1_PATT_MATCH_IDX_RD_WRN_SHIFT) |
				(1<<IRC_RX1_PATT_MATCH_IDX_PATT_OR_MASK_SHIFT), 
				IRC_RX1_PATT_MATCH_IDX);
		MMR_WRITE(reg_pattern_mask_saved[1][i], IRC_RX1_PATT_MATCH_WR_DATA);
	}
	RESTORE_IRC_REG(IRC_RX1_LPBK_EN);
    #endif

    return 0;
}

#endif

static struct platform_driver xc_irda_driver = { 
  .driver   = { 
    .name = "xc_irda",
    .owner  = THIS_MODULE,
  },  
  #ifdef CONFIG_PM
  .suspend  = xc_irda_suspend,
  .resume   = xc_irda_resume,
  #endif
};

static struct platform_device *xc_irda_platform_device;

static int __init xc_irda_init(void)
{
  int error;

  KINFO("%s\n", __FUNCTION__);

  error = platform_driver_register(&xc_irda_driver);
  if (error)
    return error;

  xc_irda_platform_device = platform_device_alloc("xc_irda", -1);
  if (!xc_irda_platform_device) {
    error = -ENOMEM;
    goto err_driver_unregister;
  }

  error = platform_device_add(xc_irda_platform_device);
  if (error)
    goto err_free_device;


  return 0;

 err_free_device:
  platform_device_put(xc_irda_platform_device);
 err_driver_unregister:
  platform_driver_unregister(&xc_irda_driver);
  return error;
}

static void __exit xc_irda_exit(void)
{
  platform_device_unregister(xc_irda_platform_device);
  platform_driver_unregister(&xc_irda_driver);
  KINFO("xc_irda: removed.\n");
}

static int vixs_transmit_ir(struct rc_dev *rcdev, unsigned *txbuf,
				unsigned count)
{
	return 0;
}
static int vixs_set_tx_carrier(struct rc_dev *rcdev, u32 carrier)
{
	struct vixs_irc *vixs = rcdev->priv;

	KINFO("Setting modulation frequency to %u", carrier);
	if (carrier == 0)
		return -EINVAL;

	vixs->carrier = carrier;

	return carrier;
}

#define USEC 1000000

static int vixs_irc_init_parameter(struct vixs_irc *host)
{
	u32 clk_per_carr = 0;
	u32 carr_low;
	u32 carr_high;
	u32 clk_per_bittime = 0;
    u32 carr_per_bittime = 24;

    host->freq = rc_para->freq;
    host->sysclk = rc_para->sysclk;
    host->bit_time = rc_para->bit_time * 1000;  // unit is ns
    
    clk_per_carr = rc_para->sysclk / rc_para->freq;
	carr_high = clk_per_carr / rc_para->duty_cycle;
	carr_low = clk_per_carr - carr_high;
    host->carrier_high_cfg = carr_high;
    host->carrier_low_cfg = carr_low;

	MMR_WRITE(carr_low, IRC_TX_CARRIER_LOW_CFG);
	MMR_WRITE(carr_high, IRC_TX_CARRIER_HIGH_CFG);

    carr_per_bittime = (rc_para->freq * rc_para->bit_time) / USEC;
   	/* Num of carriers per bit time * number of clock per carrier */
	clk_per_bittime = carr_per_bittime * clk_per_carr;
	MMR_WRITE(clk_per_bittime, IRC_RX0_BIT_TIME);
    KDEBUG("IRC_RX0_BIT_TIME = 0x%x\n", clk_per_bittime);

    host->max_space = rc_para->max_space;    
	MMR_WRITE(rc_para->max_space, IRC_RX0_MAX_SPACE);
    KDEBUG("IRC_RX0_MAX_SPACE = 0x%x\n", rc_para->max_space);

    return 0;    
}

struct vixs_ir_key_map {
    unsigned int scancode;
    unsigned char rawcode[32];
} rca_keys[] = {    
    //
    // this table must match with struct rc_map_table tivo[] in rc-tivo.c, keycode has to change on two files at same time
    // otherwise keycode will be dropped and not post to evdev
    //
    /*
     * ViXS remote
     */
    /*scancode*/      /*raw code received by this driver on Tesla*/
    { KEY_1,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x44, 0x45, 0x51, 0x45, 0x14, 0x01, 0x00} }, /* 1               */
    { KEY_2,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x44, 0x55, 0x51, 0x11, 0x11, 0x01, 0x00} }, /* 2               */
    { KEY_3,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x15, 0x51, 0x15, 0x51, 0x11, 0x11, 0x01, 0x00} }, /* 3               */
    { KEY_4,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x51, 0x51, 0x14, 0x45, 0x14, 0x01, 0x00} }, /* 4               */
    { KEY_5,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x51, 0x55, 0x44, 0x11, 0x11, 0x01, 0x00} }, /* 5               */
    { KEY_6,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x54, 0x45, 0x44, 0x11, 0x11, 0x01, 0x00} }, /* 6               */
    { KEY_7,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x54, 0x51, 0x54, 0x44, 0x14, 0x01, 0x00} }, /* 7               */
    { KEY_8,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x54, 0x55, 0x14, 0x11, 0x11, 0x01, 0x00} }, /* 8               */
    { KEY_9,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x15, 0x55, 0x45, 0x14, 0x11, 0x11, 0x01, 0x00} }, /* 9               */
    { KEY_0,          {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x01, 0x00} }, /* 9               */
    { KEY_BACKSPACE,  {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x55, 0x14, 0x45, 0x44, 0x14, 0x01, 0x00} }, /* erase           */
    { KEY_SELECT,     {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x11, 0x15, 0x55, 0x45, 0x14, 0x11, 0x01, 0x00} }, /* OK              */
    { KEY_HOME,       {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x44, 0x15, 0x11, 0x15, 0x11, 0x01, 0x00} }, /* house icon      */
    { KEY_UP,         {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x14, 0x55, 0x54, 0x14, 0x11, 0x01, 0x00} }, /* up arrow        */
    { KEY_LEFT,       {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x45, 0x51, 0x11, 0x51, 0x14, 0x01, 0x00} }, /* left arrow      */
    { KEY_RIGHT,      {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x51, 0x14, 0x11, 0x51, 0x14, 0x01, 0x00} }, /* right arrow     */
    { KEY_DOWN,       {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x45, 0x11, 0x55, 0x14, 0x15, 0x11, 0x01, 0x00} }, /* down arrow      */
    { KEY_MUTE,       {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x55, 0x54, 0x44, 0x44, 0x14, 0x01, 0x00} }, /* mute            */
    { KEY_VOLUMEDOWN, {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x15, 0x51, 0x51, 0x44, 0x45, 0x14, 0x01, 0x00} }, /* volume down     */
    { KEY_VOLUMEUP,   {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x45, 0x55, 0x44, 0x14, 0x11, 0x01, 0x00} }, /* volume up       */
    { KEY_POWER,      {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x15, 0x45, 0x44, 0x44, 0x14, 0x01, 0x00} }, /* power           */
    { KEY_MENU,       {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x51, 0x45, 0x44, 0x14, 0x11, 0x01, 0x00} }, /* menu icon       */
    { KEY_WWW,        {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x44, 0x45, 0x45, 0x45, 0x14, 0x01, 0x00} }, /* explorer button */       
    { KEY_ESC,        {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x51, 0x11, 0x55, 0x11, 0x15, 0x11, 0x01, 0x00} }, /* back icon       */    
    { BTN_MOUSE,      {0xff, 0xff, 0x00, 0x51, 0x55, 0x15, 0x11, 0x11, 0x11, 0x55, 0x55, 0x11, 0x11, 0x11, 0x11, 0x01, 0x00} }, /* mouse icon      */
    { KEY_AGAIN,      {0xff, 0xff, 0x10, 0x00, 0x00} },                                                                         /* key is repeated */
    /*scancode*/      /*raw code received by this driver on Capri*/
    { KEY_1,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0x22, 0x2a, 0x8a, 0x2a, 0xa2, 0x08, 0x00} }, /* 1               */
    { KEY_2,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x22, 0xaa, 0x8a, 0x8a, 0x88, 0x08, 0x00} }, /* 2               */
    { KEY_3,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0x88, 0xaa, 0x88, 0x8a, 0x88, 0x08, 0x00} }, /* 3               */
    { KEY_4,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0x8a, 0x8a, 0xa2, 0x28, 0xa2, 0x08, 0x00} }, /* 4               */
    { KEY_5,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x8a, 0xaa, 0x22, 0x8a, 0x88, 0x08, 0x00} }, /* 5               */
    { KEY_6,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0xa2, 0x2a, 0x22, 0x8a, 0x88, 0x08, 0x00} }, /* 6               */
    { KEY_7,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0xa2, 0x8a, 0xa2, 0x22, 0xa2, 0x08, 0x00} }, /* 7               */
    { KEY_8,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0xa2, 0xaa, 0xa2, 0x88, 0x88, 0x08, 0x00} }, /* 8               */
    { KEY_9,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0xa8, 0x2a, 0xa2, 0x88, 0x88, 0x08, 0x00} }, /* 9               */
    { KEY_0,          {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x08, 0x00} }, /* 0               */
    { KEY_BACKSPACE,  {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0xaa, 0xa2, 0x28, 0x22, 0xa2, 0x08, 0x00} }, /* erase           */
    { KEY_SELECT,     {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0xa8, 0xa8, 0x2a, 0xa2, 0x88, 0x08, 0x00} }, /* OK              */
    { KEY_HOME,       {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0x22, 0xaa, 0x88, 0xa8, 0x88, 0x08, 0x00} }, /* house icon      */
    { KEY_UP,         {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0xa2, 0xa8, 0xa2, 0xa2, 0x88, 0x08, 0x00} }, /* up arrow        */
    { KEY_LEFT,       {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x2a, 0x8a, 0x8a, 0x88, 0xa2, 0x08, 0x00} }, /* left arrow      */
    { KEY_RIGHT,      {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0x8a, 0xa2, 0x88, 0x88, 0xa2, 0x08, 0x00} }, /* right arrow     */
    { KEY_DOWN,       {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x28, 0x8a, 0xa8, 0xa2, 0xa8, 0x88, 0x08, 0x00} }, /* down arrow      */
    { KEY_MUTE,       {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0xaa, 0xa2, 0x22, 0x22, 0xa2, 0x08, 0x00} }, /* mute            */
    { KEY_VOLUMEDOWN, {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0x88, 0x8a, 0x22, 0x2a, 0xa2, 0x08, 0x00} }, /* volume down     */
    { KEY_VOLUMEUP,   {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x2a, 0xaa, 0x22, 0xa2, 0x88, 0x08, 0x00} }, /* volume up       */
    { KEY_POWER,      {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0xaa, 0x28, 0x22, 0x22, 0xa2, 0x08, 0x00} }, /* power           */
    { KEY_MENU,       {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0x8a, 0x2a, 0x22, 0xa2, 0x88, 0x08, 0x00} }, /* menu icon       */
    { KEY_WWW,        {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x22, 0x2a, 0x2a, 0x2a, 0xa2, 0x08, 0x00} }, /* explorer button */       
    { KEY_ESC,        {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0x88, 0x8a, 0xa8, 0x8a, 0xa8, 0x88, 0x08, 0x00} }, /* back icon       */    
    { BTN_MOUSE,      {0xff, 0xff, 0x03, 0x88, 0xaa, 0xaa, 0x88, 0x88, 0x88, 0xa8, 0xaa, 0x8a, 0x88, 0x88, 0x88, 0x08, 0x00} }, /* mouse icon      */
    { KEY_AGAIN,      {0xff, 0xff, 0x43, 0x00, 0x00} },                       
    /*
     * Customer Remote
     */
    { 0x708F16E9,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x14, 0x55, 0x54, 0x14, 0x11, 0x01, 0x00} }, /* OK              */
    { 0x708F41BE,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x51, 0x55, 0x54, 0x44, 0x44, 0x14, 0x01, 0x00} }, /* house icon      */
    { 0x708F12ED,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x45, 0x15, 0x45, 0x14, 0x11, 0x01, 0x00} }, /* up arrow        */
    { 0x708F14EB,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x15, 0x45, 0x15, 0x51, 0x14, 0x11, 0x01, 0x00} }, /* left arrow      */
    { 0x708F15EA,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x51, 0x14, 0x55, 0x51, 0x14, 0x11, 0x01, 0x00} }, /* right arrow     */
    { 0x708F13EC,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x11, 0x15, 0x55, 0x45, 0x14, 0x11, 0x01, 0x00} }, /* down arrow      */
    { 0x708F46B9,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x54, 0x51, 0x54, 0x44, 0x14, 0x01, 0x00} }, /* mute            */
    { 0x708F0EF1,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x44, 0x55, 0x54, 0x11, 0x11, 0x01, 0x00} }, /* volume down     */
    { 0x708F0FF0,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x11, 0x11, 0x55, 0x55, 0x11, 0x11, 0x01, 0x00} }, /* volume up       */
    { 0x708F10EF,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x55, 0x51, 0x45, 0x44, 0x14, 0x11, 0x01, 0x00} }, /* power           */
    { 0x708F0AF5,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x51, 0x15, 0x45, 0x11, 0x11, 0x01, 0x00} }, /* gear icon       */
    { 0x708F17E8,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x11, 0x51, 0x54, 0x55, 0x14, 0x11, 0x01, 0x00} }, /* back icon       */    
    { 0x708F40BF,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x55, 0x15, 0x45, 0x44, 0x44, 0x14, 0x01, 0x00} }, /* rewind          */    
    { 0x708F44BB,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x15, 0x55, 0x14, 0x51, 0x44, 0x14, 0x01, 0x00} }, /* fast forward    */    
    { 0x708F42BD,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x45, 0x55, 0x14, 0x45, 0x44, 0x14, 0x01, 0x00} }, /* play/pause      */    
    { 0x708F45BA,     {0xff, 0xff, 0x00, 0x55, 0x11, 0x51, 0x44, 0x44, 0x15, 0x51, 0x54, 0x51, 0x51, 0x44, 0x14, 0x01, 0x00} }, /* stop            */    
};

static void vixs_ir_decode_bytes(struct vixs_irc *hostp, u8 *data, int length)
{
    int leading_zero;
    int i;
  
/*    
    printk("--- raw code: ");
    for( i=0; i < length; i++ )
    {
        printk("0x%02x, ", data[i] );        
    }
    printk( "\n" );
*/    

    // drop leading zero, code must start with pluse
    for (leading_zero = 0; leading_zero < length; leading_zero++)
    {
        if (data[leading_zero] != 0x00) 
            break;
    }

    if (leading_zero == length)
        return;
    
    for (i = 0; i < sizeof(rca_keys)/sizeof(rca_keys[0]); i++)
    {
        if (!memcmp(rca_keys[i].rawcode, data + leading_zero, length - leading_zero))
        {
            if (rca_keys[i].scancode != KEY_AGAIN )
            {
                KINFO("post key down: scancode=0x%x\n", rca_keys[i].scancode);
                rc_keydown(hostp->rc, rca_keys[i].scancode, 0);
            }
            else
            {
                KINFO("post repeat\n");
                rc_repeat(hostp->rc);
            }
        }
    }
}

#define MAX_IR_RECV_LEN 255

static int vixs_ir_evdev_thread(void *irc)  
{
	struct vixs_irc *hostp = irc;
	unsigned int buf_len;
	char *b;
	struct vixs_irc_rxbuf_desc *rx_desc = NULL;
	struct list_head *lp;
    unsigned char buf[MAX_IR_RECV_LEN];
    unsigned total_len = 0;
    unsigned long flags;

	KDEBUG("vixs_ir_recv_thread start\n");

	/* only core 0 have RX */
	if (hostp->core_id != 0) {
		return -EINVAL;
	}

    // start IR receive, minic open call
	vixs_irc_reset_hw(master.hosts[0], 0);

    // setup IR parameters
    vixs_irc_init_parameter(master.hosts[0]);

    while (!kthread_should_stop())
    {
    	if (hostp->rxbuf_outstanding <= 0) {
    		if (wait_event_interruptible(hostp->inq, (hostp->rxbuf_outstanding > 0))) {
                KERROR("wait_event return failure \n");
    			continue;
    		}
    	}

    	KDEBUG("wait rxbuf_outstanding=%d\n", hostp->rxbuf_outstanding);

    	spin_lock_irqsave(&hostp->irc_spinlock, flags);

        memset(buf, 0, sizeof(buf));
        total_len = 0;
        
        // read all input from hw 
        while (hostp->rxbuf_outstanding)
        {
        	lp = hostp->rxbuf_descs.next;
        	rx_desc = list_entry(lp, struct vixs_irc_rxbuf_desc, list);

        	buf_len = vixs_irc_read_data_fb(rx_desc->rx_buf_offset,
        			rx_desc->rx_bits_count,
        			&b);

            if (total_len + buf_len >= MAX_IR_RECV_LEN)
                KERROR("receive buf_len %d is too larger ???\n", buf_len);
            
        	memcpy(buf + total_len, b, (total_len + buf_len) < MAX_IR_RECV_LEN ? buf_len : MAX_IR_RECV_LEN - total_len);
            total_len += buf_len;

        	list_del(lp);
        	kfree(rx_desc);
            hostp->rxbuf_outstanding --;
        }
        
        JDUMPBUF(buf, total_len);
            
        spin_unlock_irqrestore(&hostp->irc_spinlock, flags);

        // post to IR event layer
        vixs_ir_decode_bytes(hostp, buf, total_len);
    }

	return 0;
}


static struct rc_dev *vixs_init_rc_dev(struct vixs_irc *hostp)
{
	struct device *dev = hostp->dev;
	struct rc_dev *rc;
	int ret = -ENODEV;

	rc = rc_allocate_device();
	if (!rc) {
		KERROR("remote input dev allocation failed\n");
		goto out;
	}

	snprintf(hostp->name, sizeof(hostp->name), "ViXS infrared Remote Transceiver");
	snprintf(hostp->phys, sizeof(hostp->phys), "ViXS_IR");

	rc->input_name = hostp->name;
	rc->input_phys = hostp->phys;
	rc->dev.parent = dev;
	rc->priv = hostp;
	rc->driver_type = RC_DRIVER_IR_RAW;
	rc->allowed_protos = RC_BIT_ALL;
	rc->timeout = US_TO_NS(2750);
	rc->tx_ir = vixs_transmit_ir;
	rc->s_tx_carrier = vixs_set_tx_carrier;
	rc->driver_name = DRIVER_NAME;
	rc->rx_resolution = US_TO_NS(2);
	rc->map_name = RC_MAP_TIVO;

	ret = rc_register_device(rc);
	if (ret < 0) {
		KERROR("remote dev registration failed\n");
		goto out;
	}

    hostp->rc = rc;
	return rc;

out:
	rc_free_device(rc);
	return NULL;
}

static int __init vixs_irc_init(void)
{
	int err = 0;
	dev_t devno;
	int i;
	struct vixs_irc *host;

	KINFO("Load LIRC XCODE6\n");

	xc_irda_init();

	err = register_chrdev(XC4IRC_DEV_MAJOR, XC4IRC_DRV_NAME,
			&vixs_irc_fops);
	if (err) {
		KERROR("vixs_irda: register_chrdev failed\n");
		goto _exit1;
	}

	for (i = 0; i < XC4IRC_DEV_COUNT; i++) {
		host = master.hosts[i] = kmalloc(sizeof(struct vixs_irc),
				GFP_KERNEL);

		memset(host, 0, sizeof(struct vixs_irc));

		devno = MKDEV(XC4IRC_DEV_MAJOR, i);
		host->devno = devno;
		atomic_set(&(master.host_opened[i]), 0);

		host->core_id = i;
        host->dev = &xc_irda_platform_device->dev;

		INIT_LIST_HEAD(&(host->rxbuf_descs));

		host->rx_buf = kmalloc(XC4IRC_RX_BUF_SIZE, GFP_KERNEL);
		if (host->rx_buf == NULL) {
			err = -ENOMEM;
			goto _exit2;
		}

		init_waitqueue_head(&(host->inq));
        mutex_init(&host->mutex);
		spin_lock_init(&(host->irc_spinlock));

		/* only host0 have RX */
		if (i == 0) {
			/* init the timer for RX FIFO clean up */
			host->rx_timer.data = (unsigned long) host;
			host->rx_timer.expires = XC4IRC_RX_TIMER_DEFAULT;
			host->rx_timer.function = vixs_irc_rx_timer_fn;
			init_timer(&host->rx_timer);

			host->patt_bh_timer.data = (unsigned long) host;
			host->patt_bh_timer.expires = XC4IRC_PATT_BH_TIMER_DEFAULT;
			host->patt_bh_timer.function = vixs_irc_patt_bh_timer_fn;
			init_timer(&host->patt_bh_timer);
		}

        // register rc as evdev input device 
        vixs_init_rc_dev(host);

        // kernel thread to post ir input event to evdev 
      	kthread_run(vixs_ir_evdev_thread, (void *)host, "ViXS_ir");
	}

	err = request_irq(XC4IRC_IRQ,
			vixs_irc_irq_handler,
			IRQF_SHARED,
			XC4IRC_DRV_NAME,
			(void *) &master);
	if (err) {
		goto _exit2;
	}

	return (0);

_exit2:
	unregister_chrdev(XC4IRC_DEV_MAJOR, XC4IRC_DRV_NAME);

_exit1:
	return (err);

}

static void __exit vixs_irc_exit(void)
{
	int i;

	KINFO("Exit LIRC XCODE6\n");

	free_irq(XC4IRC_IRQ, &master);
	//	unregister_chrdev_region(host.devno, XC4IRC_DEV_COUNT);
	//	unregister_chrdev_region(master.hosts[0]->devno, XC4IRC_DEV_COUNT);
	unregister_chrdev(XC4IRC_DEV_MAJOR, XC4IRC_DRV_NAME);

	//	kfree(host.rx_buf);
	for (i = 0; i < XC4IRC_DEV_COUNT; i++) {
		kfree(master.hosts[i]->rx_buf);
		kfree(master.hosts[i]);
	}
#ifdef CONFIG_PM
	xc_irda_exit();
#endif
}


module_init(vixs_irc_init);
module_exit(vixs_irc_exit);

MODULE_DESCRIPTION("Infra-red driver for XCode4 Viper");
MODULE_AUTHOR("Jason Miu, jmiu@vixs.com");
MODULE_LICENSE("GPL");
