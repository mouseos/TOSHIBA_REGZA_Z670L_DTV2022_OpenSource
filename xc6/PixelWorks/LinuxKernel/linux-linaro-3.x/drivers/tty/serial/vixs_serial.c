/******************************************************************************
 * Copyright 2017 Pixelworks (www.pixelworks.com) 
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <plat/xcodeRegDef.h>

#include <linux/vixs.h>
#include <mach/xcode6-common.h>
#include "vixs_serial.h"

//#undef SERIAL_PARANOIA_CHECK
//#define VIXS_IOCTL

//#define VIXS_SERIAL_DEBUG

#ifdef VIXS_SERIAL_DEBUG
#define DPRINTK(_fmt, _args...)	do{ printk("%s: " _fmt, __func__,  ## _args); }while(0)
#else

#define DPRINTK(_fmt, _args...)

#endif	/* VIXS_SERIAL_DEBUG */

//#define UART_DBG_ON
#ifdef UART_DBG_ON
static char dbg_buf[SERIAL_XMIT_SIZE];
static int dbg_tail = 0;
static int dbg_count = 0;
static unsigned char *dbg_buf_ptr = dbg_buf;
#endif

extern LogInfoStruct *xc_log_info;
static unsigned char *xc_xmit_dump_buf = NULL;

#ifdef CONFIG_FPGA_BUILD
#define DEFAULT_UART_CLK CONFIG_FPGA_APPCLK_SPEED //CLK2
#define UART_CLK_DIVIDER CONFIG_FPGA_APPCLK_DIVIDER
int console_baud = CONFIG_FPGA_UART_BAUDRATE;
#else //CONFIG_FPGA_BUILD
#define UART_CLK_DIVIDER	1
int console_baud = 115200;

/* Platform */
#ifdef CONFIG_PLAT_XCODE64xx
#define DEFAULT_UART_CLK 144000000  //CLK2
#elif CONFIG_PLAT_XCODE68xx
#define DEFAULT_UART_CLK 162000000  //CLK2
#endif
#endif //CONFIG_FPGA_BUILD

/*
 * To debug optimize boot time, if superquiet is enabled
 * after SUPERQUIET_REENABLE_OUTPUT seconds, reenable uart console output if superquiet is on
 * SUPERQUIET_REENABLE_OUTPUT 0, never reenable 
 */
#ifdef CONFIG_VIXS_DYNAMIC_DISABLE
#define SUPERQUIET_REENABLE_OUTPUT    (0)
#else
#define SUPERQUIET_REENABLE_OUTPUT    (6)       // set to 6, to check xc5, xc6 drivers load are finished after 6 seconds.
#endif

#define DEV_NAME "vixs_serial"

/* XCode UART hardware registers */
static xcode_uart *uart_addr[NUM_PORTS] = UART_BASEADDR;
static struct platform_device *vixs_serial_device = NULL;
static struct xcode_serial xcode_info[NUM_PORTS];

static int uart_irqs[NUM_PORTS] = UART_IRQ;
static int superquiet=0;
static int spinlock_inited=0;
static int console_inited = 0;		/* have we initialized the console already? */
static int console_port;
static int serial_starts_tx = 0;
static unsigned int tx_done_erased[NUM_PORTS]={0};
static spinlock_t uart_lock[NUM_PORTS];

struct tty_driver *serial_driver, callout_driver;
static struct termios *serial_termios[NUM_PORTS];

#ifdef UART_DMA_MODE
static unsigned char *tx_buf[NUM_PORTS];
static unsigned char *rx_buf[NUM_PORTS];
static dma_addr_t dma_tx_phys[NUM_PORTS];
static dma_addr_t dma_rx_phys[NUM_PORTS];  
#endif

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

#ifdef CONFIG_VIXS_DYNAMIC_DISABLE
#include <linux/sysctl.h>
#endif

static unsigned int get_uart_clk(void)
{
	return		DEFAULT_UART_CLK;
}

static inline int serial_paranoia_check(struct xcode_serial *info, char *name, const char *routine)
{
#if 0
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null async_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, kdevname(device), routine);
		return 1;
	}
#endif
#endif
	return 0;
}

/*
 * ------------------------------------------------------------
 * serial_stop() and serial_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */

static void serial_stop(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;

    DPRINTK("Enter\n");
	local_irq_save(flags);

	uart->int_mask = 0;
	local_irq_restore(flags);
}

static void serial_start(struct tty_struct *tty)
{
    DPRINTK("Enter\n");
#if 0
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	unsigned long flags;
	xcode_uart *uart = uart_addr[info->line];

	local_irq_save(flags);
	if (info->xmit_cnt && info->xmit_buf && !(uart->int_mask & UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK))
		uart->int_mask |= UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;	
	local_irq_restore(flags);
#endif
}

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static inline void serial_sched_event(struct xcode_serial *info, int event)
{
	info->event |= 1 << event;
}

#ifdef UART_DMA_MODE
/* 
 * This function copies data in a ring buffer to a target buffer
 * dst: address of the destination
 * src: base address of the ring buffer
 * start_pos: position of the ring buffer to start this copy 
 * size: bytes to copy
 * buf_size: the size of src buffer, which is the location the pointer
 *           needs to wrap around 
 */
static int uart_buf_cpy(unsigned char *dst, unsigned char *src, int start_pos, int size, int buf_size)
{
	int count = 0;
	unsigned char last;

	/*
	 * This variable is defined to check the copied message goes to memory
	 * before UART engine try to read them back for transmission.
	 * This is required since the dst (which is UART TX buffer) is non-cached.
	 */
	volatile unsigned char *pVerify;

	while(count < size){
		dst[count++] = src[start_pos++];
		start_pos &= (buf_size - 1);
	}
	dst[count] = '\0';

	/*
	 * Make sure message is written to memory.
	 */
	if (count > 0) {
		if (--start_pos < 0)
			start_pos += buf_size;

		last = src[start_pos];
		pVerify = &dst[count - 1];

		if (*pVerify == last); /* Reading the last character once is suffice */
	}
	return count;
}
#endif

#ifdef UART_DMA_MODE
static void receive_chars(struct xcode_serial *info)
{
	struct tty_struct *tty = info->tty;
	xcode_uart *uart = uart_addr[info->line];
	unsigned int head, tail;

	if (!tty)
		return;

	head = uart->ring_head;
	tail = uart->ring_tail;
	/* 
	 * Don't remove this check on XC4. Somehow an interrupt was generated
	 * when head is equal to tail. It happens on slow speed like 9600
	 */
	if(head == tail)
		return;
	if(head > tail){
		tty_insert_flip_string(tty->port, rx_buf[info->line]+tail, head-tail);
	}
	else{
		tty_insert_flip_string(tty->port, rx_buf[info->line]+tail, SERIAL_XMIT_SIZE-tail);
		if(head){
			tty_insert_flip_string(tty->port, rx_buf[info->line], head);
		}
	}
	uart->ring_tail = head;

	//    memset(rx_buf[info->line], '\0', SERIAL_XMIT_SIZE);

	tty_flip_buffer_push(tty->port);
	return;
}
#else
static void receive_chars(struct xcode_serial *info)

{
	struct tty_struct *tty = info->tty;
	xcode_uart *uart = uart_addr[info->line];
	unsigned char ch, flag;
	volatile int status = 0;

	if (!tty)
		return;

	do {
		ch = uart->data & 0x00ff;
		flag = TTY_NORMAL;

		if(uart->line_status & UART0_LINE_STATUS_OVERRUN_MASK){
			printk("\t serial overflow error\n");
			flag = TTY_OVERRUN;
			uart->line_status = UART0_LINE_STATUS_OVERRUN_MASK;
		}
		else if (uart->line_status & UART0_LINE_STATUS_FRAME_ERR_MASK) {
			printk("\t serial framing error\n");
			flag = TTY_FRAME;
			uart->line_status = UART0_LINE_STATUS_FRAME_ERR_MASK;
		}
		else if (uart->line_status & UART0_LINE_STATUS_PARITY_ERR_MASK) {
			printk("\t serial parity error\n");
			flag = TTY_PARITY;
			uart->line_status = UART0_LINE_STATUS_PARITY_ERR_MASK;
		}

		tty_insert_flip_char(tty->port, ch, flag);
		
		/* keep reading characters till the RxFIFO becomes empty */
		status = uart->int_status & UART0_INT_STATUS_RECIEVE_DATA_RDY_MASK;
	} while (status);

	tty_flip_buffer_push(tty->port);

	return;
}
#endif

static void transmit_chars(struct xcode_serial *info)
{
	xcode_uart *uart = uart_addr[info->line];

#ifdef UART_DMA_MODE
	int bytes;
#endif

	spin_lock(&uart_lock[info->line]);

	if ((info->xmit_cnt <= 0) || !info->tty || info->tty->stopped) {
		/* Done Tx of xmit_buf, no more Tx interrupt will come in. We clear
		  * UART_STATUS_TX_BUSY flag so serial_write can start Tx
		  */
		info->status &= ~UART_STATUS_TX_BUSY;
		/* We also disable Tx mask so when hardware finishes console's prints
		 * or previous serial print, interrupts don't keep coming.
		 * We can't clear tx_done bit in int_status because we rely on this bit
		 * to know hardware is ready for next tx or not.
		 */
		uart->int_mask &= ~UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;
		spin_unlock(&uart_lock[info->line]);
		return;
	}

	uart->int_status = UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK;
	uart->int_mask |= UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;
#ifdef UART_DMA_MODE
	//    memset(tx_buf[console_port], '\0', SERIAL_XMIT_SIZE);
	bytes = uart_buf_cpy(tx_buf[info->line], info->xmit_buf, info->xmit_tail, info->xmit_cnt, SERIAL_XMIT_SIZE);
	info->xmit_tail = (info->xmit_tail + bytes) & (SERIAL_XMIT_SIZE - 1);
	info->xmit_cnt -= bytes;
	uart->xmit_size = bytes;
#else
	/* Send char */
	uart->data = info->xmit_buf[info->xmit_tail++];
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
	info->xmit_cnt--;
#endif
	if (info->xmit_cnt < WAKEUP_CHARS)
		tasklet_schedule(&info->tlet);
//		schedule_work(&info->tqueue);

	spin_unlock(&uart_lock[info->line]);

	return;
}

/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t serial_interrupt(int irq, void *dev_id,
					struct pt_regs *regs)
{
	struct xcode_serial *info = (struct xcode_serial *)dev_id;
	xcode_uart *uart;

	if (!info) {
		printk("%s: no uart_port for irq %d?\n", __func__, irq);
		return IRQ_NONE;
	}

#ifdef USE_IIA
	if(info->line) { // UART1
        if(!IIALocalReadInt(IIA_UART1_INT))
			return IRQ_NONE;
	}
	else {  // UART0
        if(!IIALocalReadInt(IIA_UART0_INT))
			return IRQ_NONE;
	}
#endif
	uart = uart_addr[info->line];
	if(uart->int_mask & uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK){
		transmit_chars(info);
	}

	if(uart->int_status & UART0_INT_STATUS_RECIEVE_DATA_RDY_MASK){
		receive_chars(info);
	}
	if (uart->line_status & UART0_LINE_STATUS_FRAME_ERR_MASK) {
		uart->line_status = UART0_LINE_STATUS_FRAME_ERR_MASK;
	}
	if (uart->line_status & UART0_LINE_STATUS_OVERRUN_MASK) {
		uart->line_status = UART0_LINE_STATUS_OVERRUN_MASK;
	}
    if (uart->int_status & UART0_LINE_STATUS_PARITY_ERR_MASK) {
		uart->int_status = UART0_LINE_STATUS_PARITY_ERR_MASK;
	}
	if (uart->line_status & UART0_LINE_STATUS_BREAK_IND_MASK) {
		uart->line_status = UART0_LINE_STATUS_BREAK_IND_MASK;
	}
    if (uart->int_status & UART0_INT_STATUS_RECIVER_LINE_MASK) {
		uart->int_status = UART0_INT_STATUS_RECIVER_LINE_MASK;
	}

	return IRQ_HANDLED;
}

static void change_speed(struct xcode_serial *info)
{
	xcode_uart *uart = uart_addr[info->line];
	unsigned short port;
	unsigned int baudflag, baudrate;
	unsigned int divisor, clk_speed;
	int tmp;

	if (!info->tty)
		return;
	if (!(port = info->port))
		return;

	if( !info->line ) //don't change the baudrate of ttyS0
		return;

	baudflag = info->tty->termios.c_cflag & CBAUD;
	switch (baudflag){
	case B2400:
		baudrate = 2400;
		break;
	case B4800:
		baudrate = 4800;
		break;
	case B9600:
		baudrate = 9600;
		break;
	case B19200:
		baudrate = 19200;
		break;
	case B38400:
		baudrate = 38400;
		break;
	case B57600:
		baudrate = 57600;
		break;
	case B115200:
		baudrate = 115200;
		break;
	default:
		return;
	}

	clk_speed = get_uart_clk();
	divisor = (unsigned long)(clk_speed*10/((baudrate << 4)*UART_CLK_DIVIDER));
	tmp = (unsigned long)(clk_speed/(baudrate<<4)*UART_CLK_DIVIDER) * 10;
	if(divisor - tmp >= 5){
		divisor /= 10;
		++divisor;
	}
	else
		divisor /= 10;

	uart->linectrl |= UART0_LINE_CTRL_DIVISOR_ACCESS_MASK;
	/* update divisor */
	uart->data = divisor & 0x000000FF; /* rx_tx_divlsb reg */
	uart->divmsb = (divisor >> 8) & 0x000000FF;
	uart->linectrl &= ~UART0_LINE_CTRL_DIVISOR_ACCESS_MASK;
	info->baud = baudrate;
	
}

static void xcode_console_irq_enable(int port, int enable)
{
	xcode_uart *uart = uart_addr[port];

    if(enable)
    {
	    uart->int_mask = (0x00000000 | UART0_INT_STATUS_RECIEVE_DATA_RDY_MASK
#ifdef UART_DMA_MODE
        | UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK
#endif
        );
    }
    else
    {
        uart->int_mask = 0;
    }
}

static void xcode_console_config(int port, int baudrate, int char_len,
				int stop_bits, int parity, int hw_ctl)
{
	xcode_uart *uart = uart_addr[port];
	unsigned int clk_speed=DEFAULT_UART_CLK, divisor_flt, divisor_lsb, divisor_msb;
	int tmp;

	/* take UART out of soft reset */
	uart->ctrl_reg &= ~UART0_CTRL_REG_SOFT_RESET_MASK;

#ifndef CONFIG_FPGA_BUILD
#if 1
	tmp = ((clk_speed/UART_CLK_DIVIDER)  / (baudrate * 16))*32;
	if ((tmp >> 5) < 2) 
		tmp = 2;
	tmp = (tmp + 1) >> 1;
#else
	clk_speed = get_uart_clk();
	/* HUECLK_DIV = HUECLK / DIVISOR
	 * DIVISOR = 1 on Corsa chip, so HUECLK_DIV = HUECLK
	 * DIVISOR_TMP = ( HUECLK_DIV ) / ( BAUD_RATE )
	 */
	tmp = clk_speed / baudrate;
#endif
#else
	tmp = ((clk_speed/UART_CLK_DIVIDER) << 5) / (baudrate * 16);
	if ((tmp >> 5) < 2) 
		tmp = 2;
	tmp = (tmp + 1) >> 1;
#endif
	divisor_flt = tmp & 0xF;
	divisor_lsb = (tmp >> 4) & 0xFF;
	divisor_msb = (tmp >> 12) & 0xFF;

	uart->linectrl |= UART0_LINE_CTRL_DIVISOR_ACCESS_MASK;
	uart->data = divisor_lsb;
	uart->divmsb = divisor_msb;

	/* lock divisor access */
	uart->linectrl &= ~UART0_LINE_CTRL_DIVISOR_ACCESS_MASK;

	tmp = uart->linectrl & ~UART0_LINE_CTRL_DIVISOR_FLT_MASK;
	uart->linectrl = tmp | (divisor_flt << UART0_LINE_CTRL_DIVISOR_FLT_SHIFT);
	/* UART0_INFO \ BAUD_DIVLSB == DIVISOR_LSB */
	while(((uart->info & UART0_INFO_BAUD_DIVLSB_MASK) >> UART0_INFO_BAUD_DIVLSB_SHIFT) != divisor_lsb);
	/* UART0_INFO \ BAUD_DIVMSB == DIVISOR_MSB */
	while(((uart->info & UART0_INFO_BAUD_DIVMSB_MASK) >> UART0_INFO_BAUD_DIVMSB_SHIFT) != divisor_msb);
	/* UART0_LINE_CTRL \ DIVISOR_FLT == DIVISOR_FLT */
	while(((uart->linectrl & UART0_LINE_CTRL_DIVISOR_FLT_MASK) >> UART0_LINE_CTRL_DIVISOR_FLT_SHIFT) != divisor_flt);
#ifdef UART_DMA_MODE
	if((uart->ctrl_reg & (UART0_CTRL_REG_CTRL_TX_MASK|UART0_CTRL_REG_CONTROL_RX_MASK)) && // if register mode
		(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK)){ // if tx_done bit is 1
		tx_done_erased[port] = 1;
	}
	/* set uart at DMA mode */
	uart->ctrl_reg &= ~UART0_CTRL_REG_CTRL_TX_MASK;
	uart->ctrl_reg &= ~UART0_CTRL_REG_CONTROL_RX_MASK;

	uart->xmit_base = (u32)dma_tx_phys[port];
	uart->ring_base = (u32)dma_rx_phys[port];
	uart->ring_size = SERIAL_XMIT_SIZE;
	/* set dma mode bit as BYTE */
//	uart->fifoctrl |= DMA_MODE_BYTE;
	/* set dma mode bit as DWORD */
	uart->fifoctrl &= ~DMA_MODE_BYTE;

#else
	/* set uart at register mode */
	uart->ctrl_reg |= UART0_CTRL_REG_CTRL_TX_MASK;
	uart->ctrl_reg |= UART0_CTRL_REG_CONTROL_RX_MASK;
	/* set trigger level as 1 byte */
	/* bit[7:6] - 00: 1byte, 01: 2bytes, 10: 4bytes, 11: 8 bytes */
	uart->fifoctrl &= 0xffffff3f;
//	uart->fifoctrl |= 0x000000c0;
	/* trigger level is only used on register mode */
#endif
	/* character length */
	if((char_len < 5) || (char_len > 8))
		char_len = 8;
	uart->linectrl &= ~UART0_LINE_CTRL_CHAR_LEN_MASK;
	uart->linectrl |= (char_len-5);
	/* stop bits */
	if(stop_bits > 1)
		uart->linectrl |= UART0_LINE_CTRL_STOP_BITS_MASK;
	else
		uart->linectrl &= ~UART0_LINE_CTRL_STOP_BITS_MASK;
	/* parity */
	uart->linectrl &= ~(UART0_LINE_CTRL_PARITY_EN_MASK|UART0_LINE_CTRL_EVEN_PARITY_MASK);
	uart->linectrl |= (parity << UART0_LINE_CTRL_PARITY_EN_SHIFT);
	/* hardware control */
	if(hw_ctl)
		uart->ctrl_reg |= UART0_CTRL_REG_HW_FLOW_CTRL_EN_MASK;
	else
		uart->ctrl_reg &= ~UART0_CTRL_REG_HW_FLOW_CTRL_EN_MASK;
	/* invert CTSN/RTSN */
//	uart->ctrl_reg |= UART0_CTRL_REG_CTSN_INVERT_MASK;
//	uart->ctrl_reg |= UART0_CTRL_REG_RTSN_INVERT_MASK;

}

static void retrieve_control_flags(struct tty_struct *tty, int *char_len, int *stop_bits, int *parity, int *hw_flow_ctrl)
{
	*char_len = ((tty->termios.c_cflag & CSIZE) >> 4) + 5;
	*stop_bits = (tty->termios.c_cflag & CSTOPB)? 2 : 1;
	*hw_flow_ctrl = (tty->termios.c_cflag & CRTSCTS)? 1 : 0;
	*parity = NONE_PARITY;
	if(tty->termios.c_cflag & PARENB){
		if(tty->termios.c_cflag & PARODD)
			*parity = ODD_PARITY;
		else
			*parity = EVEN_PARITY;
	}
}

static int startup(struct xcode_serial *info)
{
	xcode_uart *uart = uart_addr[info->line];
	int char_len, stop_bits, parity, hw_flow_ctrl;
	unsigned long flags;
    unsigned int reg_gpio_ctrl;
    unsigned int reg_padu_ctrl;
    unsigned int gpio_mask, padu_mask;
	
	dma_addr_t phy_handle;	

	DPRINTK("Enter port %d\n", info->line);
	if (info->flags & ASYNC_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		/* 
		 * There is one xmit buffer per uart port, no need to allocate and free per open/close.
		 * The xmit_buf is used to store the serial data, it is cacheable memory.
 		 * the size of the buffer is SERIAL_XMIT_SIZE = PAGE_SIZE
		 */
		info->xmit_buf = get_zeroed_page(GFP_KERNEL);

		if (!info->xmit_buf) {
			printk("serial port %d Not able to allocate the dma transmit buffer.\n", info->line);
			return -ENOMEM;
		}        

		if ((!xc_xmit_dump_buf) && info->line == 0) {			
			xc_xmit_dump_buf = dma_alloc_coherent(NULL, SERIAL_XMIT_SIZE, &phy_handle, GFP_KERNEL);
			if (!xc_xmit_dump_buf)
				return -ENOMEM;			
			printk("%s: xc_xmit_dump_buf = %p phy_handle = 0x%x\n", __func__, xc_xmit_dump_buf, (u32)phy_handle);		
			xc_log_info->desc[2].addr = (u32)(phy_handle);
			xc_log_info->desc[2].size = SERIAL_XMIT_SIZE;
			xc_log_info->desc[2].phead = 0x000ff06c;
		}
	}

	spin_lock_irqsave(&uart_lock[info->line],flags);

	reg_gpio_ctrl = xcode_readl(GPIO_H_CTRL); 
	reg_padu_ctrl = xcode_readl(RBM_PADU_CTRL);

    switch (info->line) {
        case 0:
#ifdef CONFIG_PLAT_XCODE64xx
		gpio_mask = GPIO_H_CTRL_GPIO_MODE_SEL0_MASK | GPIO_H_CTRL_GPIO_MODE_SEL1_MASK;
		padu_mask = RBM_PADU_CTRL_PADU_CTRL_UART0_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART0_U_MASK;
#endif
#ifdef CONFIG_PLAT_XCODE68xx
		gpio_mask = GPIO_H_CTRL_GPIO_MODE_SEL0_MASK;
		padu_mask = RBM_PADU_CTRL_PADU_CTRL_UART0_MASK;
#endif
		reg_gpio_ctrl &= ~gpio_mask;
		reg_padu_ctrl &= ~padu_mask;
        break;

		case 1:
#ifdef CONFIG_PLAT_XCODE64xx
		gpio_mask = GPIO_H_CTRL_GPIO_MODE_SEL2_MASK | GPIO_H_CTRL_GPIO_MODE_SEL3_MASK;
		padu_mask = RBM_PADU_CTRL_PADU_CTRL_UART0_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART0_U_MASK;
#endif
#ifdef CONFIG_PLAT_XCODE68xx
		gpio_mask = GPIO_H_CTRL_GPIO_MODE_SEL2_MASK;
		padu_mask = RBM_PADU_CTRL_PADU_CTRL_UART1_MASK;
#endif
		reg_gpio_ctrl &= ~gpio_mask;
		reg_padu_ctrl &= ~padu_mask;
        break;

        case 2:
#ifdef CONFIG_PLAT_XCODE64xx
		reg_padu_ctrl = (reg_padu_ctrl & ~(RBM_PADU_CTRL_PADU_CTRL_UART0_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART0_U_MASK));
#endif
#ifdef CONFIG_PLAT_XCODE68xx
		reg_padu_ctrl &= ~RBM_PADU_CTRL_PADU_CTRL_UART0_MASK;
		reg_padu_ctrl |= 1 << RBM_PADU_CTRL_PADU_CTRL_UART0_SHIFT;
#endif
        break;
        
        case 3:
#ifdef CONFIG_PLAT_XCODE64xx
		reg_padu_ctrl = (reg_padu_ctrl & ~(RBM_PADU_CTRL_PADU_CTRL_UART1_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART1_U_MASK)) | 
		                RBM_PADU_CTRL_PADU_CTRL_UART1_U_MASK;
#endif
#ifdef CONFIG_PLAT_XCODE68xx
		reg_padu_ctrl &= ~RBM_PADU_CTRL_PADU_CTRL_UART1_MASK;
		reg_padu_ctrl |= 1 << RBM_PADU_CTRL_PADU_CTRL_UART1_SHIFT;
#endif
        break;
        
        default:
        spin_unlock_irqrestore(&uart_lock[info->line], flags);
        printk("%s: Error: wrong serial port %d\n", __func__, info->line);
        return -ENODEV;
    }
	xcode_writel(reg_gpio_ctrl, GPIO_H_CTRL); 
	xcode_writel(reg_padu_ctrl, RBM_PADU_CTRL);

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	info->xmit_fifo_size = 1;

    uart->int_mask = 0x0;

#ifdef UART_DMA_MODE
	/* set to register mode before taking out reset so FIFO leftover don't get in the ring buffer */
	uart->ctrl_reg |= UART0_CTRL_REG_CTRL_TX_MASK;
	uart->ctrl_reg |= UART0_CTRL_REG_CONTROL_RX_MASK;
#endif
	if(uart->ctrl_reg & UART0_CTRL_REG_SOFT_RESET_MASK){
		uart->ctrl_reg &= ~UART0_CTRL_REG_SOFT_RESET_MASK;
		udelay(10);
	}
	uart->fifoctrl |= 0x6; //clear rx and tx fifo

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);

	retrieve_control_flags(info->tty, &char_len, &stop_bits, &parity, &hw_flow_ctrl);
	xcode_console_config(info->line, console_baud, char_len, stop_bits, parity, hw_flow_ctrl);
    xcode_console_irq_enable(info->line, 1);

	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	info->flags |= ASYNC_INITIALIZED;

	spin_unlock_irqrestore(&uart_lock[info->line], flags);

#ifdef USE_IIA
    IIALocalSetMask(info->line?IIA_UART1_INT:IIA_UART0_INT);
#else
	enable_irq(info->irq);
#endif

	DPRINTK("Exit\n");
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct xcode_serial *info)
{
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;

	uart->int_mask = 0x00000000;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	local_irq_save(flags);

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	tasklet_kill(&info->tlet);

	info->flags &= ~ASYNC_INITIALIZED;

	local_irq_restore(flags);

	uart->ctrl_reg |= UART0_CTRL_REG_SOFT_RESET_MASK;
#ifdef UART_DMA_MODE
	uart->ring_tail = 0;
#endif

#ifdef USE_IIA
    IIALocalClearMask(info->line?IIA_UART1_INT:IIA_UART0_INT);
#else
	disable_irq(info->irq);
#endif
}

static void serial_set_ldisc(struct tty_struct *tty)
{
#if 0
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;

	info->is_cons = (tty->termios->c_line == N_TTY);

	printk("serial driver: ttyS%d console mode %s\n", info->line,
	       info->is_cons ? "on" : "off");
#else
	printk("%s %d\n", __FUNCTION__, __LINE__);
#endif
}

#if 0
static void serial_flush_chars(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;
#ifdef UART_DMA_MODE
	int bytes;
#endif

	while((uart->int_mask & UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK) &&
		!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK))
		msleep(1);

	local_irq_save(flags);
	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
		!info->xmit_buf){
		local_irq_restore(flags);
		return;
	}

	uart->int_status = UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK;
	uart->int_mask |= UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;
#ifdef UART_DMA_MODE
	bytes = uart_buf_cpy(tx_buf[info->line], info->xmit_buf, info->xmit_tail, info->xmit_cnt, SERIAL_XMIT_SIZE);
	flush_dcache_range((unsigned int)tx_buf[info->line], (unsigned int)tx_buf[info->line]+bytes);
	uart->xmit_size = bytes;
	info->xmit_tail = (info->xmit_tail + bytes) & (SERIAL_XMIT_SIZE - 1);
	info->xmit_cnt -= bytes;
#else
	uart->data = info->xmit_buf[info->xmit_tail++];
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
	info->xmit_cnt--;
#endif

	local_irq_restore(flags);

	tty_wakeup(tty);
}
#endif

static int serial_write(struct tty_struct *tty, const unsigned char *buf,
			   int count)
{
	int c, total = 0;
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;

	if(superquiet)
	{	
        u64 ts = local_clock();
        do_div(ts, 1000000000);
        if (SUPERQUIET_REENABLE_OUTPUT > 0 && ts >= SUPERQUIET_REENABLE_OUTPUT) {
			spin_lock_irqsave(&uart_lock[info->line],flags);
    		superquiet = 0;
			spin_unlock_irqrestore(&uart_lock[info->line],flags);
		} else {
		return count;
		}
    }

	if((count <= 0) && (info->x_char != 0))
		return 0;

	if (serial_paranoia_check(info, tty->name, "serial_write"))
		return 0;

	if (!tty || !info->xmit_buf) {
		printk("\t no tty or no xmit_buf fail\n");
		return 0;
	}

	spin_lock_irqsave(&uart_lock[info->line],flags);

	/* software flow control */
	if(info->x_char) {
		info->xmit_buf[info->xmit_head] = info->x_char;
		info->xmit_head = (info->xmit_head + 1) & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt++;
	}

	while (1) {
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					  SERIAL_XMIT_SIZE - info->xmit_head));

		if (c <= 0)
			break;
		
		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		if(xc_xmit_dump_buf) {
			memcpy(xc_xmit_dump_buf + info->xmit_head, buf, c);
			xc_log_info->desc[2].head = info->xmit_head + xc_log_info->desc[2].addr;
		}

		info->xmit_head =
		    (info->xmit_head + c) & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt += c;

		buf += c;
		count -= c;
		total += c;
	}

	if(tty->stopped || tty->hw_stopped){
		spin_unlock_irqrestore(&uart_lock[info->line],flags);
		return total;
	}

	/* If UART_STATUS_TX_BUSY flag is set, let interrupt handler take
	  * care of sending data until nothing left in xmit_buf (xmit_cnt = 0)
	  */
	if(!info->xmit_cnt || (info->status & UART_STATUS_TX_BUSY)) {
		spin_unlock_irqrestore(&uart_lock[info->line],flags);
		return total;
	}

	if((uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK) ||
		tx_done_erased[info->line]){
		info->status |= UART_STATUS_TX_BUSY;
		uart->int_status = UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK;
		uart->int_mask |= UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;
	#ifdef UART_DMA_MODE
		c = uart_buf_cpy(tx_buf[info->line], info->xmit_buf, info->xmit_tail, info->xmit_cnt, SERIAL_XMIT_SIZE);
		info->xmit_tail = (info->xmit_tail + c) & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt -= c;
		uart->xmit_size = c;
	#else
		/*
		 * The TX_FIFO_ENTRY represents the number if filled entries in the Tx FIFO
		 * TX_FIFO can store up to 32 x 8 bits entries, and it used as 2x16 bytes FIFOs
		 * fill the FIFO with 16 bytes data and
		 */
		while(((uart->info & UART0_INFO_TX_FIFO_ENTRIES_MASK)>>UART0_INFO_TX_FIFO_ENTRIES_SHIFT) > 16) {
		uart->data = info->xmit_buf[info->xmit_tail++];
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt--;
		}
	#endif
		if( !serial_starts_tx && (info->line == console_port))
			serial_starts_tx = 1;
	}

	spin_unlock_irqrestore(&uart_lock[info->line],flags);

	return total;
}

static int serial_write_room(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	int ret;

	ret = (SERIAL_XMIT_SIZE - info->xmit_cnt) - 1;
	if (ret < 0)
		return 0;
	else
		return ret;
}

static int serial_chars_in_buffer(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;

	return info->xmit_cnt;
}

static void serial_flush_buffer(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	unsigned long flags;

	local_irq_save(flags);

	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	local_irq_restore(flags);

	tty_wakeup(tty);
}

/*
 * ------------------------------------------------------------
 * serial_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */

static void serial_throttle(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;

	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line (do this atomic) */
}

static void serial_unthrottle(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}

	/* Assert RTS line (do this atomic) */
}

/*
 * ------------------------------------------------------------
 * serial_ioctl() and friends
 * ------------------------------------------------------------
 */

// TODO: not implementted yet
#ifdef VIXS_IOCTL
static int get_serial_info(struct xcode_serial *info,
			   struct serial_struct *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;

	if(copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

static int set_serial_info(struct xcode_serial *info,
			   struct serial_struct *new_info)
{
	struct serial_struct new_serial;
	struct xcode_serial old_info;
	int retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial, new_info, sizeof(new_serial));
	old_info = *info;

#if 0
	if (!suser()) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}
#endif

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
		       (new_serial.flags & ASYNC_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = startup(info);
	return retval;
}
#endif
static int serial_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
#ifdef VIXS_IOCTL
	void __user *uarg = (void __user *)arg;
	struct xcode_serial *info = tty->driver_data;
	int ret = -ENOIOCTLCMD;

	switch (cmd) {
	case TIOCGSERIAL:
		ret = get_serial_info(info, uarg);
		break;
	case TIOCSSERIAL:
		ret = set_serial_info(info, uarg);
		break;
	case TIOCSERCONFIG:
		ret = serial_autoconfig(info);
		break;
	case TIOCSERGWILD: /* obsolete */
	case TIOCSERSWILD: /* obsolete */
		ret = 0;
		break;
	}

	if(ret != -ENOIOCTLCMD)
		return ret;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	return ret;
#else
	return n_tty_ioctl_helper(tty, NULL, cmd, arg);
#endif
}

static int serial_tiocmget(struct tty_struct *tty)
{
	unsigned int value;
    struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
    unsigned long flags;

    local_irq_save(flags);

	value =
    #ifdef CONFIG_PLAT_XCODE64xx
	((UART0_MODEM_STATUS_RTSN_MASK & uart->modem_status) ? 0 : TIOCM_RTS) |
	((UART0_MODEM_STATUS_CLEAR_TO_SEND_MASK & uart->modem_status) ? TIOCM_CTS : 0) |
	((UART0_MODEM_STATUS_DATA_SEND_REQ_MASK & uart->modem_status) ? TIOCM_DSR : 0) |
	((UART0_MODEM_STATUS_DATA_CAR_DETECT_MASK & uart->modem_status) ? TIOCM_CAR : 0) |
	((UART0_MODEM_STATUS_RING_IND_MASK & uart->modem_status) ? TIOCM_RNG : 0);
    #endif
    
    #ifdef CONFIG_PLAT_XCODE68xx
	((UART0_MODEM_STATUS_RTSN_MASK & uart->modem_status) ? 0 : TIOCM_RTS) |
	((UART0_MODEM_STATUS_CLEAR_TO_SEND_MASK & uart->modem_status) ? TIOCM_CTS : 0);
    #endif
    
    local_irq_restore(flags);


	return value;
}

static int serial_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
    struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
    unsigned long flags;

    local_irq_save(flags);

	if (set & TIOCM_RTS)
		uart->modem_ctrl &= ~UART0_MODEM_CTRL_REQ_TO_SEND_MASK;
	if (clear & TIOCM_RTS)
		uart->modem_ctrl |= UART0_MODEM_CTRL_REQ_TO_SEND_MASK;

    #ifdef CONFIG_PLAT_XC64xx
	if (clear & TIOCM_DTR)
		uart->modem_ctrl |= UART0_MODEM_CTRL_DATA_TERM_RDY_MASK;
	if (set & TIOCM_DTR)
		uart->modem_ctrl &= ~UART0_MODEM_CTRL_DATA_TERM_RDY_MASK;
    #endif

    local_irq_restore(flags);


	return 0;
}

static void serial_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	struct termios *old_termios = (struct termios *)old;
	int char_len, stop_bits, parity, hw_flow_ctrl;
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;

	if (tty->termios.c_cflag == old_termios->c_cflag)
		return;

	retrieve_control_flags(info->tty, &char_len, &stop_bits, &parity, &hw_flow_ctrl);
	/* If hardware is busy, don't change the setting. Wait until Tx is done. */
	while(!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK))
		if(tx_done_erased[info->line])
			break;
	local_irq_save(flags);
	xcode_console_config(info->line, console_baud, char_len, stop_bits, parity, hw_flow_ctrl);
    xcode_console_irq_enable(info->line, 1);
	change_speed(info);
	local_irq_restore(flags);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios.c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		serial_start(tty);
	}
}

/*
 * ------------------------------------------------------------
 * serial_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void serial_close(struct tty_struct *tty, struct file *filp)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;
	xcode_uart *uart = uart_addr[info->line];
	unsigned long flags;

	DPRINTK("Enter port %d\n", info->line);
    
	if (!info || serial_paranoia_check(info, tty->name, "serial_close"))
		return;

	local_irq_save(flags);
	/* Sameer: Couldn't see corresponding sti() call. We might need it. */

	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}

	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("serial_close: bad serial port count; tty->count is 1, info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("serial_close: bad serial port count for ttyS%d: %d\n", info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		local_irq_restore(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	uart->int_mask = 0;
	shutdown(info);

//	if (tty->driver->flush_buffer)
//		tty->driver->flush_buffer(tty);
    local_irq_restore(flags);
	tty_ldisc_flush(tty);
    local_irq_save(flags);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
    info->status &= ~UART_STATUS_TX_BUSY;
#if 0				// member ldisc removed in 2.4.29
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close) (tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open) (tty);
	}
#endif
	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs
					     (info->close_delay));
			/* current->state = TASK_INTERRUPTIBLE; */
/* 			schedule_timeout(info->close_delay); */
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
    DPRINTK("Exit\n");
}

/*
 * serial_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void serial_hangup(struct tty_struct *tty)
{
	struct xcode_serial *info = (struct xcode_serial *)tty->driver_data;

	serial_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
/* 	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE); */
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 *  serial_open and friends 
 */
static int block_til_ready(struct tty_struct *tty, struct file *filp,
			   struct xcode_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int retval;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) || (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * serial_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);

	info->count--;
	info->blocked_open++;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if(!(info->flags & ASYNC_NORMAL_ACTIVE))
			break;
		
		if (tty_hung_up_p(filp)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;

	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static int serial_open(struct tty_struct *tty, struct file *filp)
{
	struct xcode_serial *info;
	int retval, line;

    DPRINTK("Enter\n");

	line = tty->index;
/* 	line = MINOR(tty->device) - tty->driver.minor_start; */
	if ((line < 0) || (line >= NUM_PORTS)) {
		printk("XCode serial driver: check your lines\n");
		return -ENODEV;
	}

	info = &xcode_info[line];

	if (serial_paranoia_check(info, tty->name, "serial_open"))
		return -ENODEV;

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);

	if (retval)
		return retval;

    DPRINTK("Exit\n");

	return 0;
}

static void serial_tasklet(unsigned long data)
{
	struct xcode_serial *info = (struct xcode_serial *)data;
	tty_wakeup(info->tty);
}

static void show_serial_version(void)
{
	printk("Xcode serial driver version 2.00\n");
}

static const struct tty_operations serial_ops = {
	.open = serial_open,
	.close = serial_close,
	.write = serial_write,
	/* we have buffered characters to driver in serial_write. no need to use flush_chars */
	//.flush_chars = serial_flush_chars,
	.write_room = serial_write_room,
	.chars_in_buffer = serial_chars_in_buffer,
	.flush_buffer = serial_flush_buffer,
	.ioctl = serial_ioctl,
	.throttle = serial_throttle,
	.unthrottle = serial_unthrottle,
	.set_termios = serial_set_termios,
	.stop = serial_stop,
	.start = serial_start,
	.hangup = serial_hangup,
	.set_ldisc = serial_set_ldisc,
	.tiocmset = serial_tiocmset,
	.tiocmget = serial_tiocmget,
};

#ifdef CONFIG_VIXS_DYNAMIC_DISABLE
static ctl_table serial_vixs_table[] = {
	{ .procname	= "superquiet",
	  .data		= &superquiet,
	  .maxlen	= sizeof(superquiet),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec },
	{ }
};

static ctl_table serial_vixs_dir_table[] = {
	{ .procname	= "serial_vixs",
	  .mode		= 0555,
	  .child	= serial_vixs_table },
	{ }
};

static ctl_table serial_vixs_root_table[] = {
	{ .procname	= "dev",
	  .mode		= 0555,
	  .child	= serial_vixs_dir_table },
	{ }
};

static struct ctl_table_header *serial_vixs_table_header;
#endif

static int __init xcode_serial_init(void)
{
	struct xcode_serial *info;
	int i;

	printk("XCODE_SERIAL_INIT\n");
    
	vixs_serial_device = platform_device_register_simple(DEV_NAME,
			-1, NULL, 0);

	if (!vixs_serial_device) {
		printk("%s: cannot register platform device %s\n", __func__, DEV_NAME);
		return (-ENODEV);
	}    

	/* Initialize the tty_driver structure */

	serial_driver = alloc_tty_driver(NUM_PORTS);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	serial_driver->owner = THIS_MODULE;
//	serial_driver->magic = TTY_DRIVER_MAGIC;
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->num = NUM_PORTS;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;

	serial_driver->init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	serial_driver->termios = (struct ktermios **)serial_termios;
//	serial_driver->termios_locked = (struct ktermios **)serial_termios_locked;

	serial_driver->driver_name = "serial";
	tty_set_operations(serial_driver, &serial_ops);
	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	callout_driver = *serial_driver;
	callout_driver.name = "cua";
	callout_driver.major = TTYAUX_MAJOR;
/* 	callout_driver.subtype = SERIAL_TYPE_CALLOUT; */

	if (tty_register_driver(serial_driver)) {
		put_tty_driver(serial_driver);
		printk("%s: Couldn't register serial driver\n", __FUNCTION__);
		return (-ENOMEM);
	}
	if (tty_register_driver(&callout_driver)) {
		printk("%s: Couldn't register callout driver\n", __FUNCTION__);
		return (-EBUSY);
	}

#ifdef CONFIG_VIXS_DYNAMIC_DISABLE
	serial_vixs_table_header = register_sysctl_table(serial_vixs_root_table);
	if (!serial_vixs_table_header) {
		return (-ENOMEM);
	}
#endif

#ifdef USE_IIA
    IIALocalClearMask(IIA_UART0_INT);
    IIALocalClearMask(IIA_UART1_INT);
#endif

	memset((void *)&xcode_info, 0, NUM_PORTS * sizeof(struct xcode_serial));

	for (i=0; i<NUM_PORTS; i++) {

		info = &xcode_info[i];
//		info->magic = SERIAL_MAGIC;
		info->port = (unsigned int)(uart_addr[i]);
		info->tty = 0;
		info->irq = uart_irqs[i];
			
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;


		tty_port_init(&info->tty_port);
		info->tty_port.tty = NULL;
		tty_port_link_device(&info->tty_port, serial_driver, i);

/* 	    info->tqueue.routine = do_softint; */
/* 	    info->tqueue.data = info; */
/* 	    info->tqueue_hangup.routine = do_serial_hangup; */
/* 	    info->tqueue_hangup.data = info; */
/* 	    info->callout_termios = callout_driver.init_termios; */
/* 	    info->normal_termios = serial_driver.init_termios; */
//		INIT_WORK(&info->tqueue, do_softint, info);
		tasklet_init(&info->tlet, serial_tasklet, (unsigned long)info);
//		INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
		if(!spinlock_inited)
			spin_lock_init(&uart_lock[i]);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->line = i;
//		info->is_cons = ((i == 1) ? 1 : 0);

		printk("%s%d (irq = %d, address = 0x%x)", serial_driver->name,
		       info->line, info->irq, info->port);
		printk(" is a builtin UART\n");

		if (request_irq(info->irq, (irq_handler_t)serial_interrupt,
					IRQF_SHARED, "XC_UART", info))
			panic("Unable to attach XCode serial interrupt\n");

#ifdef USE_IIA
        IIALocalSetMask(info->line?IIA_UART1_INT:IIA_UART0_INT);
#else
		disable_irq(info->irq);
#endif
		
	}
	spinlock_inited=1;
        
#ifdef UART_DBG_ON
	printk("\tdbg_buf addr = 0x%x\n", dbg_buf_ptr);
#endif

	return 0;
}

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
//int register_serial(struct serial_struct *req)
//{
//	return -1;
//}

//void unregister_serial(int line)
//{
//	return;
//}

module_init(xcode_serial_init);

#ifdef CONFIG_VIXS_DYNAMIC_DISABLE
module_param(superquiet, int, 0644);
MODULE_PARM_DESC(superquiet, "dynamic superquiet");
#endif

//#define PROG_FPGA_EN_UART1
int xcode_console_setup(struct console *cp, char *arg)
{
	unsigned int reg;
    unsigned int reg_gpio_ctrl;
    unsigned int reg_padu_ctrl;

	if (!console_inited){
		console_port = cp->index;
#ifdef PROG_FPGA_EN_UART1
		/****** Program FPGA to enable UART1 *****/
		reg = xcode_readl(ACC_RESET_REG0); // clear PPC reset
		reg &= ~ACC_RESET_REG0_PPC_RESET_MASK;
		xcode_writel(reg, ACC_RESET_REG0);
		reg |= ACC_RESET_REG0_PPC_RESET_MASK;
		xcode_writel(reg, ACC_RESET_REG0);
		reg &= ~ACC_RESET_REG0_PPC_RESET_MASK;
		xcode_writel(reg, ACC_RESET_REG0);
		xcode_writel(0xFFFFFFFF, PPC_INT_STATUS); // clear
		/* Configure I2C0 */
		xcode_writel(0xF0000068, I2C0_CONFIG); // 2ms, 7-bit, 100Kbps
		reg = 0x00001302 | (0x18 << 16); // 0x18 = SAVE2_I2C_ADDR
		xcode_writel(0x9b1B, I2C0_DATA_LOAD); // for Tesla board
		xcode_writel(reg, I2C0_COMMAND);
		while(xcode_readl(PPC_INT_STATUS) & 0x00300040);
		if(xcode_readl(I2C0_COMMAND) != reg)
			printk("FPGA programming I2C0_COMMAND failed.\n");
		if((xcode_readl(PPC_INT_STATUS) & 0x00300040) != 0x40) {
			printk("FPGA programming failed. PPC_INT_STATUS: 0x%x\n",
					xcode_readl(PPC_INT_STATUS));
		}
		else
			xcode_writel(0x1, PPC_INT_STATUS);
		/****** end of FPGA programming *********/
#endif

		reg_gpio_ctrl = xcode_readl(GPIO_H_CTRL); 
		reg_padu_ctrl = xcode_readl(RBM_PADU_CTRL);
        if(console_port == 0) {
#ifdef CONFIG_PLAT_XCODE64xx
            reg_gpio_ctrl &= ~(GPIO_H_CTRL_GPIO_MODE_SEL0_MASK | GPIO_H_CTRL_GPIO_MODE_SEL1_MASK);
            reg_padu_ctrl &= ~(RBM_PADU_CTRL_PADU_CTRL_UART0_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART0_U_MASK);
#endif
#ifdef CONFIG_PLAT_XCODE68xx
            reg_gpio_ctrl &= ~(GPIO_H_CTRL_GPIO_MODE_SEL0_MASK);
            reg_padu_ctrl &= ~(RBM_PADU_CTRL_PADU_CTRL_UART0_MASK);
#endif
        }
        else if(console_port == 1) {
#ifdef CONFIG_PLAT_XCODE64xx
            reg_gpio_ctrl &= ~(GPIO_H_CTRL_GPIO_MODE_SEL2_MASK | GPIO_H_CTRL_GPIO_MODE_SEL3_MASK);    
            reg_padu_ctrl &= ~(RBM_PADU_CTRL_PADU_CTRL_UART1_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART1_U_MASK);
#endif
#ifdef CONFIG_PLAT_XCODE68xx
            reg_gpio_ctrl &= ~(GPIO_H_CTRL_GPIO_MODE_SEL2_MASK);    
            reg_padu_ctrl &= ~(RBM_PADU_CTRL_PADU_CTRL_UART1_MASK);
#endif
        }
        else if(console_port == 2) {
#ifdef CONFIG_PLAT_XCODE64xx
            reg_padu_ctrl = (reg_padu_ctrl & ~(RBM_PADU_CTRL_PADU_CTRL_UART0_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART0_U_MASK)) | 0x40;
#endif
#ifdef CONFIG_PLAT_XCODE68xx
            reg_padu_ctrl = (reg_padu_ctrl & ~RBM_PADU_CTRL_PADU_CTRL_UART0_MASK) | (1 << RBM_PADU_CTRL_PADU_CTRL_UART0_SHIFT);
#endif
        }
        else if(console_port == 3) {
#ifdef CONFIG_PLAT_XCODE64xx
            reg_padu_ctrl = (reg_padu_ctrl & ~(RBM_PADU_CTRL_PADU_CTRL_UART1_L_MASK | RBM_PADU_CTRL_PADU_CTRL_UART1_U_MASK)) | 
                            RBM_PADU_CTRL_PADU_CTRL_UART1_U_MASK;
#endif
#ifdef CONFIG_PLAT_XCODE68xx
            reg_padu_ctrl = (reg_padu_ctrl & ~RBM_PADU_CTRL_PADU_CTRL_UART1_MASK) | (1 << RBM_PADU_CTRL_PADU_CTRL_UART1_SHIFT);
#endif
		}
		xcode_writel(reg_gpio_ctrl, GPIO_H_CTRL); 
		xcode_writel(reg_padu_ctrl, RBM_PADU_CTRL);

		reg = xcode_readl(ACC_RESET_REG0);
		reg |= ACC_RESET_REG0_UART_RESET_MASK;
		xcode_writel(reg, ACC_RESET_REG0);
		reg = xcode_readl(ACC_RESET_REG0);
		reg &= ~ACC_RESET_REG0_UART_RESET_MASK;
		xcode_writel(reg, ACC_RESET_REG0);

#ifdef CONFIG_FPGA_STANDALONE
		reg = xcode_readl(UART0_MODEM_CTRL);
		reg = (reg & ~(UART0_MODEM_CTRL_USER_OUTPUT_MASK | UART0_MODEM_CTRL_USER_OUTPUT2_MASK));
        if(console_port==1)
            reg|=(1<<UART0_MODEM_CTRL_USER_OUTPUT_SHIFT);
		xcode_writel(reg, UART0_MODEM_CTRL);
#endif

	 	xcode_console_config(console_port, console_baud, 8, 0, NONE_PARITY, 0);
		console_inited = 1;
	}

	return 0;		/* successful initialization */
}

static struct tty_driver *xcode_console_device(struct console *c, int *index)
{
	*index = console_port;
	return serial_driver;
}

#ifdef UART_DMA_MODE
static void console_write_dma_mode(const char *p, unsigned int len)
{
	xcode_uart *uart = uart_addr[console_port];
	unsigned long flags;
	int c = 0; 

	if(len <= 0)
		return;

	/* 
	 * Once serial starts transmitting, we have to check hardware status before
	 * we start transmit. If we don't wait for hardware ready to transmit and
	 * issue a Tx before UART hardware finishes previous Tx, it will mess up
	 * UART and the hardware behavior is unknown.
	 */
	if( serial_starts_tx ){
		if(tx_done_erased[console_port]){ // TRANSMIT_DONE_EMPTY bit was erased in mode switch
			tx_done_erased[console_port] = 0;
		}
		else{
			while(!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK));
		}
	}
	spin_lock_irqsave(&uart_lock[console_port],flags);

	memset(tx_buf[console_port], '\0', SERIAL_XMIT_SIZE);	

	uart->int_status = UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK;
	/* If serial is up, we need to enable Tx mask to notify serial when
	 * hardware is ready.
	 */
	if( serial_starts_tx )
		uart->int_mask |= UART0_INT_MASK_TRANSMIT_DONE_EMPTY_MASK;
	while (len-- > 0) {
		if (*p == '\n')
			tx_buf[console_port][c++] = '\r';
		tx_buf[console_port][c++] = (*p++);
	}

	uart->xmit_size = c;
	spin_unlock_irqrestore(&uart_lock[console_port], flags);


	/* 
	 * Before serial starts transmit, console is the only source to print. 
	  * We cheak hardware status in the end. This is same as diagnostic
	  * procedure, which doesn't enable/rely on interrupts.
	  */
	if( !serial_starts_tx )
		while(!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK));
}

#else

void reg_put_char(char ch)
{
	xcode_uart *uart = uart_addr[console_port];
	/*
	 * The TX_FIFO_ENTRY represents the number if filled entries in the Tx FIFO
	 * TX_FIFO can store up to 32 x 8 bits entries, and it used as 2x16 bytes FIFOs
	 * fill the FIFO with 16 bytes data and
	 */
	while(((uart->info & UART0_INFO_TX_FIFO_ENTRIES_MASK) >> UART0_INFO_TX_FIFO_ENTRIES_SHIFT) > 16);		
	uart->data = ch;
}


void console_write_reg_mode(const char *p, unsigned int len)
{
	volatile int mask;
	unsigned long flags;
	xcode_uart *uart = uart_addr[console_port];

	/* 
	 * Once serial starts transmitting, we have to check hardware status before
	 * we start transmit. If we don't wait for hardware ready to transmit and
	 * issue a Tx before UART hardware finishes previous Tx, it will mess up
	 * UART and the hardware behavior is unknown.
	 */
	if( serial_starts_tx ) {
		if(tx_done_erased[console_port]){ // TRANSMIT_DONE_EMPTY bit was erased in mode switch
			tx_done_erased[console_port] = 0;
		}
		else{
			while(!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK));
		}	
	}
	
	spin_lock_irqsave(&uart_lock[console_port],flags);	
	while (len-- > 0) {
		if (*p == '\n')
			reg_put_char('\r');
		reg_put_char(*p++);
	}
	while(!(uart->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK));
	uart->int_status = UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK;
	spin_unlock_irqrestore(&uart_lock[console_port],flags);
}
#endif

void xcode_console_write(struct console *cp, const char *p, unsigned len)
{
	unsigned long flags = 0;
	if (!console_inited)
		xcode_console_setup(cp, NULL);
	
	if(superquiet)
	{
        u64 ts = local_clock();
        do_div(ts, 1000000000);
        if (SUPERQUIET_REENABLE_OUTPUT > 0 && ts >= SUPERQUIET_REENABLE_OUTPUT) {
			if(spinlock_inited)
				spin_lock_irqsave(&uart_lock[cp->index],flags);
    		superquiet = 0;
			if(spinlock_inited)
				spin_unlock_irqrestore(&uart_lock[cp->index],flags);
        } else {
            return;
    }
    }   
#ifdef UART_DMA_MODE
	console_write_dma_mode(p, len);
#else
	console_write_reg_mode(p, len);
#endif
}

static struct console xcode_console = {
	.name = "ttyS",
	.write = xcode_console_write,
	.read    = NULL,
	.device = xcode_console_device,
	.unblank = NULL,
	.setup = xcode_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.cflag   = 0,
	.next    = NULL
};

static int vixs_uart_probe(struct platform_device *dev)
{
  printk("PROBE UART\n");
  return 0;
}

static int vixs_uart_remove(struct platform_device *dev)
{
  printk("REMOVE UART\n");
  return 0;
}

#ifdef CONFIG_PM
static int vixs_uart_suspend(struct platform_device *dev, pm_message_t state)
{
  int i;
    struct xcode_serial *info;
    xcode_uart *uart;

    if (!console_suspend_enabled)
        return 0;

  //printk("Suspend UART!!\n");
  for (i = 0; i < NUM_PORTS; i++) {

    info = &xcode_info[i];

        /* FIXME
         * Shutdown will cause data in xmit_buf
         * and HW fifo lost.
         * Keep the xmit_buf and wait DMA finish current
         * transmission might be better, but in worst
         * case this will cause 0.4s delay.
         */
    if(info->count)
    {
      //printk("Suspend port %d\n", i);
      shutdown(info);
    }
  }

    /* Set uart0 to register mode/open interrupt, 
     * so it can work as wake up source.
     * Note! uart in DMA mode can not be
     * a wake up source because DMA will 
     * access memory but memory is in 
     * self-refreshing mode on suspend
     */
    info = &xcode_info[console_port];
    uart = uart_addr[console_port];
    //Set TX_MASK all will prevent fifo leftover printed
  uart->ctrl_reg |= UART0_CTRL_REG_CONTROL_RX_MASK|UART0_CTRL_REG_CTRL_TX_MASK;
    uart->ctrl_reg &= ~UART0_CTRL_REG_SOFT_RESET_MASK;
  uart->int_mask = UART0_INT_STATUS_RECIEVE_DATA_RDY_MASK;

    //Stop console to prevent further printk (which will wake up us)
    console_stop(&xcode_console);
    return 0;
}

static int vixs_uart_resume(struct platform_device *dev)
{
  int i;

  if (!console_suspend_enabled)
    return 0;

  for (i = 0; i < NUM_PORTS; i++) {
    struct xcode_serial *info;

    info = &xcode_info[i];

    if(info->count)
    {
      printk("Resume port %d\n", i);
      startup(info);
    }
  }

    //Start console as it is stopped on suspend
    console_start(&xcode_console);

    printk("UART Resumed!!\n");
    return 0;
}
#endif

static struct platform_driver vixs_uart_driver = {
  .probe    = vixs_uart_probe,
  .remove   = vixs_uart_remove,
#ifdef CONFIG_PM
  .suspend  = vixs_uart_suspend,
  .resume   = vixs_uart_resume,
#endif
  .driver   = {  
  .name = DEV_NAME,
  .owner  = THIS_MODULE,
  },
};

module_platform_driver(vixs_uart_driver);

static int __init xcode_console_init(void)
{
	int i;
#ifdef UART_DMA_MODE 
	//int order=get_order(SERIAL_XMIT_SIZE);

	//printk("order=%d\n", order);
	
	for(i=0;i<NUM_PORTS;i++)
	{
		/*
		 * Tx and Rx buffers are the hardware buffers for uarts
		 * Tx buf is a line buffer for transmit
		 * Rx buf is a ring buffer for receive
		 */
		tx_buf[i] = dma_alloc_coherent(NULL, SERIAL_XMIT_SIZE, &dma_tx_phys[i], GFP_KERNEL);
		if (!tx_buf[i]) {
			printk("Not able to allocate the dma buffer tx_buf[%d]\n",i);
			return -ENOMEM;
		}

		rx_buf[i] = dma_alloc_coherent(NULL, SERIAL_XMIT_SIZE, &dma_rx_phys[i], GFP_KERNEL);
		if (!rx_buf[i]) {
			printk("Not able to allocate the dma buffer rx_buf[%d]\n", i);
			return -ENOMEM;
		}

		printk("UART%d: Rx buffer 0x%p Phys: 0x%x\n", i, rx_buf[i], dma_rx_phys[i]);
		printk("UART%d: Tx buffer 0x%p Phys: 0x%x\n", i, tx_buf[i], dma_tx_phys[i]);
	}
#endif

	if(!spinlock_inited)
	{
		for (i=0; i<NUM_PORTS; i++)
			spin_lock_init(&uart_lock[i]);
		spinlock_inited=1;
	}
        
	register_console(&xcode_console);

	return 0;
}

console_initcall(xcode_console_init);

static int __init superquiet_kernel(char *str)
{
	superquiet=1;
	return 0;
}

early_param("superquiet", superquiet_kernel);

void raw_printk5(const char *str, uint n1, uint n2, uint n3, uint n4)
{
        /* Do nothing */
}
EXPORT_SYMBOL(raw_printk5);
