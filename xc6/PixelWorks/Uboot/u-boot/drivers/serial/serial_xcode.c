#include <common.h>
#include <watchdog.h>
#include <asm/io.h>
#include <serial.h>
#include <linux/compiler.h>
#include <asm/arch/xcodeRegDef.h>

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

#define CONSOLE_PORT 0

static void xcode_putc (int portnum, char c);
static int xcode_getc (int portnum);
static int xcode_tstc (int portnum);
unsigned int baudrate = CONFIG_BAUDRATE;
DECLARE_GLOBAL_DATA_PTR;

static xcode_uart *xcode_get_regs(int portnum)
{
	return (xcode_uart *)(XC_SOC_PROC_MMREG_BASE+UART0_RX_TX_DIVLSB+portnum*0x100);
}

static int xcode_serial_init(void)
{
	xcode_uart *regs = xcode_get_regs(CONSOLE_PORT);

	/* FIXME */

	return 0;
}

static void xcode_serial_putc(const char c)
{
	if(gd->bootfw_param & PARAM_CON_TX_NONE)
		return;

	if (c == '\n')
		xcode_putc (CONSOLE_PORT, '\r');

	xcode_putc (CONSOLE_PORT, c);
}

static int xcode_serial_getc(void)
{
	if(gd->bootfw_param & PARAM_CON_RX_NONE)
		return;

	return xcode_getc (CONSOLE_PORT);
}

static int xcode_serial_tstc(void)
{
	if(gd->bootfw_param & PARAM_CON_RX_NONE)
		return 0;

	return xcode_tstc (CONSOLE_PORT);
}

static void xcode_serial_setbrg(void)
{
	xcode_uart *regs = xcode_get_regs(CONSOLE_PORT);

	baudrate = gd->baudrate;
	/*
	 * Flush FIFO and wait for non-busy before changing baudrate to avoid
	 * crap in console
	 */
	while (!(regs->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK))
		WATCHDOG_RESET();
	serial_init();
}

static void xcode_putc (int portnum, char c)
{
	xcode_uart *regs = xcode_get_regs(portnum);

	/* Wait until there is space in the FIFO */
	while (!(regs->int_status & UART0_INT_STATUS_TRANSMIT_DONE_EMPTY_MASK))
		WATCHDOG_RESET();

	/* Send the character */
	regs->data = c;
}

static int xcode_getc (int portnum)
{
	xcode_uart *regs = xcode_get_regs(portnum);
	unsigned int data;

	/* Wait until there is data in the FIFO */
	while (!(regs->info & UART0_INFO_RX_FIFO_ENTRIES_MASK));
		WATCHDOG_RESET();

	data = regs->data & 0xff;

	return (int) data;
}

static int xcode_tstc (int portnum)
{
	xcode_uart *regs = xcode_get_regs(portnum);

	WATCHDOG_RESET();
	return regs->info & UART0_INFO_RX_FIFO_ENTRIES_MASK;
}

static struct serial_device xcode_serial_drv = {
	.name	= "xcode_serial",
	.start	= xcode_serial_init,
	.stop	= NULL,
	.setbrg	= xcode_serial_setbrg,
	.putc	= xcode_serial_putc,
	.puts	= default_serial_puts,
	.getc	= xcode_serial_getc,
	.tstc	= xcode_serial_tstc,
};

void xcode_serial_initialize(void)
{
	serial_register(&xcode_serial_drv);
}

__weak struct serial_device *default_serial_console(void)
{
	return &xcode_serial_drv;
}
