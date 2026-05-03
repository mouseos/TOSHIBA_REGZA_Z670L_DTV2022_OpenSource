#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <asm/mach/time.h>
#include <asm/localtimer.h>
#include <asm/sched_clock.h>
#include <plat/common.h>

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/clkdev.h>

#include <mach/hardware.h>

#include <asm/div64.h>


#include <linux/vixs.h>

#ifdef CONFIG_FPGA_BUILD
#define XCODE6_FPGA_PERIPH_CLOCK	(CONFIG_FPGA_ARM_CLK / 2)
#else
#ifdef CONFIG_PLAT_XCODE64xx
#define XCODE6_FPGA_PERIPH_CLOCK	475000000
#else
#ifdef CONFIG_PLAT_XCODE68xx
/*
 * Fpll = Fxtl * (IDIV + FDIV / 2^20) / MDIV
 * Fxtl is 25MHz, MDIV is 1
 * CG2_PLL1_FREQ (MM:0x01D0) RW WRITE_POST_TRIG 
 *    FB_DIV_FRAC      19:0   def=0x0;     # PLL0 feedback divider ratio (FDIV), fractional portion
 *    FB_DIV_INT      25:20   def=0x0;     # PLL0 feedback divider ratio (IDIV), integer portion. Fpll = Fxtl * (IDIV + FDIV / 2^20) / MDIV
 *    PRE_DIV         30:28   def=0x0;     # PLL0 prescaler divider ratio (MDIV)  $0: MDIV = 1  $1: ILLEGAL $2-$7: MDIV = /PRE_DIV
 */
#define XCODE6_FPGA_PERIPH_CLOCK	 ( ( (xcode_readl(CG2_PLL1_FREQ) & 0xFFFFF) + ((xcode_readl(CG2_PLL1_FREQ) & 0x3F00000) >> 20) ) * 25000000 / 2 ) 
#else
#error PLATFORM is not defined
#endif
#endif
#endif


static struct clock_event_device clkevt;

static irqreturn_t xcode6_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * irqs should be disabled here, but as the irq is shared they are only
	 * guaranteed to be off if the timer irq is registered first.
	 */
	WARN_ON_ONCE(!irqs_disabled());

	/* periodic mode should handle delayed ticks */
	if (!smp_processor_id()) {
		clkevt.event_handler(&clkevt);
		return IRQ_HANDLED;
	}

	/* this irq is shared ... */
	return IRQ_NONE;
}
#if 0
static irqreturn_t xcode6_test_interrupt(int irq, void *dev_id)
{
printk("test: irq %d\n", irq);
	return IRQ_HANDLED;
}
#endif
static struct irqaction xcode6_timer_irq = {
	.name       = "xcode6_tick",
	.flags      = IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler    = xcode6_timer_interrupt
};

static int clkevt_next_event(unsigned long delta, struct clock_event_device *dev)
{
	return 0;
}

static void clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	switch (mode) {
		case CLOCK_EVT_MODE_PERIODIC:
			break;
		case CLOCK_EVT_MODE_ONESHOT:
			panic("Oneshot timer is not supported\n");
			break;
		case CLOCK_EVT_MODE_SHUTDOWN:
		case CLOCK_EVT_MODE_UNUSED:
		case CLOCK_EVT_MODE_RESUME:
			break;
	}
}

static struct clock_event_device clkevt = {
	.name       = "xcode6_tick",
	.features   = CLOCK_EVT_FEAT_PERIODIC,
	.shift      = 32,
	.rating     = 50,
	.set_next_event = clkevt_next_event,
	.set_mode   = clkevt_mode,
};

static cycle_t read_clk(struct clocksource *cs)
{
	cycle_t ret;
	uint32_t upper1;
	uint32_t upper2;

#if 0
	ret=0xffffffffUL-PRIVATE_TIMER_REG(PRIVATE_TIMER_COUNTER_OFFSET);
#else
	do {
		upper1 = GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_UPPER_OFFSET);
		ret = upper1;
		ret=ret<<32;
		ret|=GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET);
		upper2 = GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_UPPER_OFFSET);
	} while (upper2 != upper1);
#endif

	return ret;
}

static struct clocksource sysclk = {
	.name       = "ARM Core Global Timer",
	.rating     = 150,
	.read       = read_clk,
	.mask       = CLOCKSOURCE_MASK(64),
	.flags      = CLOCK_SOURCE_IS_CONTINUOUS,
};

void __init xcode6_timer_init(void)
{
    printk("xcode6_timer_init, clock=%dHz\n", XCODE6_FPGA_PERIPH_CLOCK*2);
    
	//WatchDog as timer tick
	PRIVATE_TIMER_REG(WATCHDOG_CONTROL_OFFSET)=PRIVATE_TIMER_IRQ_EN|PRIVATE_TIMER_AUTO_RELOAD|PRIVATE_TIMER_TIMER_EN;
#ifdef CONFIG_CPU_DCACHE_DISABLE
	PRIVATE_TIMER_REG(WATCHDOG_LOAD_OFFSET)=XCODE6_FPGA_PERIPH_CLOCK;   //We are too slow to handle 100Hz
	PRIVATE_TIMER_REG(WATCHDOG_COUNTER_OFFSET)=XCODE6_FPGA_PERIPH_CLOCK;
#else
	PRIVATE_TIMER_REG(WATCHDOG_LOAD_OFFSET)=XCODE6_FPGA_PERIPH_CLOCK/HZ;  
	PRIVATE_TIMER_REG(WATCHDOG_COUNTER_OFFSET)=XCODE6_FPGA_PERIPH_CLOCK/HZ;   
#endif

#if 0
	//Private timer as clock source
	PRIVATE_TIMER_REG(PRIVATE_TIMER_CONTROL_OFFSET)=PRIVATE_TIMER_AUTO_RELOAD|PRIVATE_TIMER_TIMER_EN;
	PRIVATE_TIMER_REG(PRIVATE_TIMER_LOAD_OFFSET)=0xffffffffUL;
	PRIVATE_TIMER_REG(PRIVATE_TIMER_COUNTER_OFFSET)=0xffffffffUL;
#endif

	//Global timer
	GLOBAL_TIMER_REG(GLOBAL_TIMER_CONTROL_OFFSET)&=~GLOBAL_TIMER_TIMER_EN;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET)=0;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_UPPER_OFFSET)=0;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_CONTROL_OFFSET)=GLOBAL_TIMER_TIMER_EN;

	/* Make IRQs happen for the system timer */
	setup_percpu_irq(30, &xcode6_timer_irq);
	enable_percpu_irq(30, 0);
#if 0
{
	int i;

	for(i=31;i<376;i++)
	{
		ll_printhex(i);
		setup_irq(i, &xcode6_test_irq);
	}
}
#endif

	/* Setup timer clockevent, with minimum of two ticks (important!!) */
	clkevt.mult = div_sc(XCODE6_FPGA_PERIPH_CLOCK, NSEC_PER_SEC, clkevt.shift);
	clkevt.max_delta_ns = clockevent_delta2ns(-1, &clkevt);
	clkevt.min_delta_ns = clockevent_delta2ns(1, &clkevt);
	clkevt.cpumask = cpumask_of(0);
	clockevents_register_device(&clkevt);

	/* register clocksource */
	clocksource_register_hz(&sysclk, XCODE6_FPGA_PERIPH_CLOCK);
}

unsigned long clk_get_rate(struct clk *clk)
{
	return XCODE6_FPGA_PERIPH_CLOCK;
}
EXPORT_SYMBOL(clk_get_rate);

