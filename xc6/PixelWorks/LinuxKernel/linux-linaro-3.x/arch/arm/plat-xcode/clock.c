#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/io.h>

#include <plat/clock.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static struct clk_functions *arch_clock;

/*
 * Standard clock functions defined in include/linux/clk.h
 */

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (!arch_clock || !arch_clock->clk_enable)
		return -EINVAL;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = arch_clock->clk_enable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	if (!arch_clock || !arch_clock->clk_disable)
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->usecount == 0) {
		pr_err("Trying disable clock %s with 0 usecount\n",
		       clk->name);
		WARN_ON(1);
		goto out;
	}

	arch_clock->clk_disable(clk);

out:
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

int clk_set_rate(struct clk * clk, unsigned long rate)
{
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);


