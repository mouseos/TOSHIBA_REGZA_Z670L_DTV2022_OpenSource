/******************************************************************************
 * Copyright Codito Technologies (www.codito.com) Oct 01, 2004
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *****************************************************************************/

/* This driver is modified to use ViXS UART documented in BSD-X2C006-0000.
 *  
 * Author: Amber Lin                                       -- May 2009
 */

#ifndef	_ASM_XC_SERIAL_H
#define	_ASM_XC_SERIAL_H

#ifdef __KERNEL__

#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/console.h>

#ifndef CONFIG_DEBUG_LL
#define UART_DMA_MODE /* commenting this out will use REGISTER mode */
#endif

#define UART0_IRQ_NUM		XCODE6_IRQ_UART0
#define UART1_IRQ_NUM		XCODE6_IRQ_UART1
#define UART2_IRQ_NUM       XCODE6_IRQ_UART2
#define UART3_IRQ_NUM       XCODE6_IRQ_UART3

#if (CONFIG_SERIAL_VIXS_NUM_PORTS == 1)
#define UART_BASEADDR {(xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART0_RX_TX_DIVLSB)}
#define UART_IRQ      {XCODE6_IRQ_UART0}
#elif (CONFIG_SERIAL_VIXS_NUM_PORTS == 2)
#define UART_BASEADDR {(xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART0_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART1_RX_TX_DIVLSB)}
#define UART_IRQ      {XCODE6_IRQ_UART0, XCODE6_IRQ_UART1}
#elif (CONFIG_SERIAL_VIXS_NUM_PORTS == 3)
#define UART_BASEADDR {(xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART0_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART1_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART2_RX_TX_DIVLSB)}
#define UART_IRQ      {XCODE6_IRQ_UART0, XCODE6_IRQ_UART1, XCODE6_IRQ_UART2}
#elif (CONFIG_SERIAL_VIXS_NUM_PORTS == 4)
#define UART_BASEADDR {(xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART0_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART1_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART2_RX_TX_DIVLSB), \
                       (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART3_RX_TX_DIVLSB)}
#define UART_IRQ      {XCODE6_IRQ_UART0, XCODE6_IRQ_UART1, XCODE6_IRQ_UART2, XCODE6_IRQ_UART3}
#else
#error "ViXS Serial ports number setting error"
#endif

#define NUM_PORTS 			CONFIG_SERIAL_VIXS_NUM_PORTS

#define NONE_PARITY 0
#define ODD_PARITY  1
#define EVEN_PARITY 3

/****** Flags to use fors status in struct xcode_serial ******/
#define UART_STATUS_TX_BUSY		0x00000001


/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct xcode_serial {
//	char is_cons;       /* Is this our console. */

	/* We need to know the current clock divisor
	 * to read the bps rate the chip has currently
	 * loaded.
	 */
	int	baud;
	int			magic;
	int			baud_base;
	int			port;
	unsigned int		irq;
	int			flags; 		/* defined in tty.h */
	int			type; 		/* UART type */
	struct tty_struct 	*tty;
	struct tty_port tty_port;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			xmit_fifo_size;
	int			custom_divisor;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	long			session; /* Session of opening process */
	long			pgrp; /* pgrp of opening process */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	int			status;

	struct work_struct	tqueue;   //replaced by tlet;
	struct work_struct	tqueue_hangup;

/* Sameer: Obsolete structs in linux-2.6 */
/* 	struct termios		normal_termios; */
/* 	struct termios		callout_termios; */
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	
	struct tasklet_struct   tlet;
};

typedef volatile struct {
   volatile int data;
   volatile int divmsb;
   volatile int fifoctrl;
   volatile int linectrl;
   volatile int modem_status;
   volatile int line_status;
   volatile int modem_ctrl;
   volatile int ctrl_reg;
   volatile int ring_base;
   volatile int ring_head;
   volatile int ring_tail;
   volatile int ring_size;
   volatile int xmit_base;
   volatile int xmit_size;
   volatile int int_status;
   volatile int int_mask;
   volatile int info;
} xcode_uart;

//#define SERIAL_MAGIC 0x5301

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
//#define ARCSERIAL_EVENT_WRITE_WAKEUP	0

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS   2048 //256

/* in INTID_FIFOCTRL */
#define DMA_MODE_BYTE	0x00000008

#endif /* __KERNEL__ */
#endif	/* _ASM_SERIAL_H */
