/*
 * (C) Copyright 2008
 * Texas Instruments
 *
 * Richard Woodruff <r-woodruff2@ti.com>
 * Syed Moahmmed Khasim <khasim@ti.com>
 *
 * (C) Copyright 2002
 * Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Marius Groeger <mgroeger@sysgo.de>
 * Alex Zuepke <azu@sysgo.de>
 *
 * (C) Copyright 2002
 * Gary Jennejohn, DENX Software Engineering, <garyj@denx.de>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

#define GLOBAL_TIMER_COUNTER_LOWER_OFFSET   0x0
#define GLOBAL_TIMER_COUNTER_UPPER_OFFSET   0x4
#define GLOBAL_TIMER_CONTROL_OFFSET         0x8
#define GLOBAL_TIMER_TIMER_EN           1
#define GLOBAL_TIMER_COMP_EN            2
#define GLOBAL_TIMER_IRQ_EN             4
#define GLOBAL_TIMER_AUTO_INC_EN        8
#define GLOBAL_TIMER_PRESCALE_MASK      0x0000ff00
#define GLOBAL_TIMER_INT_STATUS_OFFSET      0xc
#define GLOBAL_TIMER_COMP_LOWER_OFFSET      0x10
#define GLOBAL_TIMER_COMP_UPPER_OFFSET      0x14
#define GLOBAL_TIMER_AUTO_INC_OFFSET        0x18

#define PRIVATE_TIMER_LOAD_OFFSET       0x00
#define PRIVATE_TIMER_COUNTER_OFFSET    0x04
#define PRIVATE_TIMER_CONTROL_OFFSET    0x08
#define PRIVATE_TIMER_TIMER_EN          1
#define PRIVATE_TIMER_AUTO_RELOAD       2
#define PRIVATE_TIMER_IRQ_EN            4
#define PRIVATE_TIMER_PRESCALE_MASK     0x0000ff00
#define PRIVATE_TIMER_INT_STATUS_OFFSET 0x0c

#define WATCHDOG_LOAD_OFFSET            0x20
#define WATCHDOG_COUNTER_OFFSET         0x24
#define WATCHDOG_CONTROL_OFFSET         0x28
#define WATCHDOG_EN         1
#define WATCHDOG_MODE       8
#define WATCHDOG_INT_STATUS_OFFSET      0x2c
#define WATCHDOG_RESET_OFFSET           0x30
#define WATCHDOG_DISABLE_OFFSET         0x34

#define PRIVATE_TIMER_REG(offset)   *(volatile unsigned int *)(CORE_PERIPHERAL_BASE+CORE_PRIVATE_TIMER_OFFSET+(offset))
#define GLOBAL_TIMER_REG(offset)   *(volatile unsigned int *)(CORE_PERIPHERAL_BASE+CORE_GLOBAL_TIMER_OFFSET+(offset))

/*
 * Nothing really to do with interrupts, just starts up a counter.
 */

#ifdef FPGA_BUILD
#define XCODE6_FPGA_PERIPH_CLOCK    1250000UL
#else
#define XCODE6_FPGA_PERIPH_CLOCK    475000000UL
#endif

#define TIMER_CLOCK 		XCODE6_FPGA_PERIPH_CLOCK
#define TIMER_OVERFLOW_VAL  0xffffffffUL
#define TIMER_LOAD_VAL      0

int timer_init(void)
{
	GLOBAL_TIMER_REG(GLOBAL_TIMER_CONTROL_OFFSET)&=~GLOBAL_TIMER_TIMER_EN;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET)=0;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_UPPER_OFFSET)=0;
	GLOBAL_TIMER_REG(GLOBAL_TIMER_CONTROL_OFFSET)=GLOBAL_TIMER_TIMER_EN;

	/* reset time, capture current incrementer value time */

	gd->lastinc = GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET);
	gd->tbl = 0;		/* start "advancing" time stamp from 0 */

	return 0;
}

/*
 * timer without interrupts
 */
ulong get_timer(ulong base)
{
	return get_timer_masked() - base;
}

/* delay x useconds */
void __udelay(unsigned long usec)
{
	long tmo = usec * (TIMER_CLOCK / 1000000);
	unsigned long now, last;


	last = GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET);

	while (tmo > 0) {
		now = GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET);
		if (last > now) /* count up timer overflow */
			tmo -= (TIMER_OVERFLOW_VAL - last + now + 1);
		else
			tmo -= (now - last);
		last = now;
	}
}

ulong get_timer_masked(void)
{
	/* current tick value */
	ulong now =  GLOBAL_TIMER_REG(GLOBAL_TIMER_COUNTER_LOWER_OFFSET) / (TIMER_CLOCK / CONFIG_SYS_HZ);

	if (now >= gd->lastinc)	/* normal mode (non roll) */
		/* move stamp fordward with absoulte diff ticks */
		gd->tbl += (now - gd->lastinc);
	else	/* we have rollover of incrementer */
		gd->tbl += ((TIMER_OVERFLOW_VAL / (TIMER_CLOCK / CONFIG_SYS_HZ))
			     - gd->lastinc) + now;
	gd->lastinc = now;
	return gd->tbl;
}

/*
 * This function is derived from PowerPC code (read timebase as long long).
 * On ARM it just returns the timer value.
 */
unsigned long long get_ticks(void)
{
	return get_timer(0);
}

/*
 * This function is derived from PowerPC code (timebase clock frequency).
 * On ARM it returns the number of timer ticks per second.
 */
ulong get_tbclk(void)
{
	return CONFIG_SYS_HZ;
}
