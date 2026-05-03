#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>


struct xcode_gpio_chip {
	struct gpio_chip	chip;
	struct xcode_gpio_chip	*next;
	unsigned int regbase;	
};

#define to_xcode_gpio_chip(c) container_of(c, struct xcode_gpio_chip, chip)

static int xcode_gpiolib_request(struct gpio_chip *chip, unsigned offset);
static void xcode_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip);
static void xcode_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val);
static int xcode_gpiolib_get(struct gpio_chip *chip, unsigned offset);
static int xcode_gpiolib_direction_output(struct gpio_chip *chip,
					 unsigned offset, int val);
static int xcode_gpiolib_direction_input(struct gpio_chip *chip,
					unsigned offset);
static int xcode_gpiolib_to_irq(struct gpio_chip *chip, unsigned offset);


#define XCODE6_GPIO_CHIP(name)						\
	{								\
		.chip = {						\
			.label		  = name,			\
			.request	  = xcode_gpiolib_request,	\
			.direction_input  = xcode_gpiolib_direction_input, \
			.direction_output = xcode_gpiolib_direction_output, \
			.get		  = xcode_gpiolib_get,		\
			.set		  = xcode_gpiolib_set,		\
			.dbg_show	  = xcode_gpiolib_dbg_show,	\
			.to_irq		  = xcode_gpiolib_to_irq,	\
			.ngpio		  = ARCH_NR_GPIOS,	\
		},							\
	}


#define HOST_MUTEX_START				0x08
#define XC_GPIO_HW_MUTEX				MIPS_INTERLOCK1
#define XC_GPIO_HW_MUTEX_ID_SHIFT		MIPS_INTERLOCK1_ID5_SHIFT
#define XC_GPIO_HW_MUTEX_ID_MASK		MIPS_INTERLOCK1_ID5_MASK

#define XC_GPIO_INTERLOCK_ID	(HOST_MUTEX_START + 2)


static int xcode_gpio_lock(void){
	unsigned int reg_val;
	int count=0;
	reg_val=mmr_read (XC_GPIO_HW_MUTEX);
	
	while (((reg_val >> XC_GPIO_HW_MUTEX_ID_SHIFT) & 0xF) != XC_GPIO_INTERLOCK_ID){
		pr_debug("val:%x reg:%x\n",XC_GPIO_INTERLOCK_ID,XC_GPIO_HW_MUTEX);
		mmr_write (((XC_GPIO_INTERLOCK_ID & 0xF) << XC_GPIO_HW_MUTEX_ID_SHIFT),XC_GPIO_HW_MUTEX);
		reg_val=mmr_read (XC_GPIO_HW_MUTEX);
		pr_debug("val:%x \n",reg_val);
		if(count++ > 100){
			pr_err("gpio lock");
			break;
		}
	}
 

}

static int xcode_gpio_unlock(void){
	unsigned int reg_val;
	reg_val=mmr_read (XC_GPIO_HW_MUTEX);
    if (((reg_val >> XC_GPIO_HW_MUTEX_ID_SHIFT) & 0xF) == XC_GPIO_INTERLOCK_ID){
		mmr_write (((XC_GPIO_INTERLOCK_ID & 0xF) << XC_GPIO_HW_MUTEX_ID_SHIFT),XC_GPIO_HW_MUTEX);
   	}
    return 0;
}

EXPORT_SYMBOL(xcode_gpio_lock);
EXPORT_SYMBOL(xcode_gpio_unlock);



static int xcode_gpiolib_request(struct gpio_chip *chip, unsigned offset){
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);

	return 0;
};
static void xcode_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip){
	pr_debug("[%s:%d]chip:%x\n",__func__,__LINE__,chip);

};
static void xcode_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val){
	struct xcode_gpio_chip *xcode_gpio = to_xcode_gpio_chip(chip);
	unsigned int out_val=0;
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);
	xcode_gpio_lock();
	out_val=mmr_read (xcode_gpio->regbase);	
	if(val)
		out_val |= (1<<offset);
	else
		out_val &= ~(1<<offset);
	mmr_write (out_val,xcode_gpio->regbase);	
	xcode_gpio_unlock();
};
static int xcode_gpiolib_get(struct gpio_chip *chip, unsigned offset){

	struct xcode_gpio_chip *xcode_gpio = to_xcode_gpio_chip(chip);
	unsigned int out_val=0;	
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);
	xcode_gpio_lock();
	out_val=mmr_read (xcode_gpio->regbase);	
	out_val &= (1<<offset);
	xcode_gpio_unlock();

	return (out_val>>offset);

};
static int xcode_gpiolib_direction_output(struct gpio_chip *chip, unsigned offset, int val){
	struct xcode_gpio_chip *xcode_gpio = to_xcode_gpio_chip(chip);
	unsigned int oe_val=0;
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);
	xcode_gpio_lock();
	oe_val=mmr_read (xcode_gpio->regbase+4);	
	oe_val |= (1<<offset);
	mmr_write (oe_val,xcode_gpio->regbase+4);	
	xcode_gpio_unlock();


	return 0;
};
static int xcode_gpiolib_direction_input(struct gpio_chip *chip,unsigned offset){
	struct xcode_gpio_chip *xcode_gpio = to_xcode_gpio_chip(chip);
	unsigned int oe_val=0;
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);
	xcode_gpio_lock();
	oe_val=mmr_read (xcode_gpio->regbase+4);	
	oe_val &= ~(1<<offset);
	mmr_write (oe_val,xcode_gpio->regbase+4);	
	xcode_gpio_unlock();

	return 0;
};
static int xcode_gpiolib_to_irq(struct gpio_chip *chip, unsigned offset){
	pr_debug("[%s:%d]chip:%x offset:%x\n",__func__,__LINE__,chip,offset);
	return 0;
};





static struct xcode_gpio_chip gpio_chip[] = {
	XCODE6_GPIO_CHIP("DEDICATED"),
};

static inline void __iomem *pin_to_controller(unsigned pin)
{
	if (likely(pin < ARCH_NR_GPIOS))
		return gpio_chip[pin].regbase;

	return NULL;
}

static void __init xcode_gpio_init_one(int idx,u32 regbase)
{
	struct xcode_gpio_chip *xcode_gpio = &gpio_chip[idx];
	switch(idx){
		case 0:
			xcode_gpio->regbase = GPIO_DEDICATED_OUT;//ioremap(regbase, 512);
		break;
		default:
			pr_err("[%s:%d]idx:%x regbase:%x\n",__func__,__LINE__,idx,regbase);
	}
}


static int __init xcode_gpio_init(void)
{
	int ret=0;
	xcode_gpio_init_one(0,GPIO_DEDICATED_OUT);
	ret= gpiochip_add(&gpio_chip[0].chip);
	pr_debug("ret:%x\n",ret);
	return 0;
}

subsys_initcall(xcode_gpio_init);

