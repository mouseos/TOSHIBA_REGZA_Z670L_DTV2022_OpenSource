#ifndef __ARCH_ARM_XCODE_CLOCK_H
#define __ARCH_ARM_XCODE_CLOCK_H

struct clk {
	struct list_head	node;
	const struct clkops	*ops;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	void __iomem		*enable_reg;
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
	u8			enable_bit;
	s8			usecount;
	u8			fixed_div;
	u8			flags;
	void __iomem		*clksel_reg;
	u32			clksel_mask;
	const struct clksel	*clksel;
	struct dpll_data	*dpll_data;
	const char		*clkdm_name;
	struct clockdomain	*clkdm;
};


struct clk_functions {
	int		(*clk_enable)(struct clk *clk);
	void		(*clk_disable)(struct clk *clk);
	long		(*clk_round_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_parent)(struct clk *clk, struct clk *parent);
	void		(*clk_allow_idle)(struct clk *clk);
	void		(*clk_deny_idle)(struct clk *clk);
	void		(*clk_disable_unused)(struct clk *clk);
};

#endif
