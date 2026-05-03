#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <mach/system.h>
#include <plat/common.h>
#include <plat/irqs.h>

static void xcode6_arch_reset(char mode, const char *cmd)
{
}

void (*arch_reset)(char, const char *) = xcode6_arch_reset;

