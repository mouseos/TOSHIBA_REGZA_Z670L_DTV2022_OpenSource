#include <linux/init.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <mach/hardware.h>
#include <mach/xcode6-common.h>

/* SCU base address */
static void __iomem *scu_base;
static bool cpu_has_died = 0;

static DEFINE_SPINLOCK(boot_lock);

void arch_disable_smp_support(void)
{
	u32 temp;

	temp = mmr_read(PROC5_ARM_RESET);
	temp &= ~(NCPURESET_MASK | NDBGRESET_MASK | NWDRESET_MASK | NNEONRESET_MASK);
	temp |= 0x11011; //Only Core 0 to run
	mmr_write(temp, PROC5_ARM_RESET);
}

static void xcode6_secondary_init(unsigned int cpu)
{
	/*
	 * If any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	//gic_secondary_init(0);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int xcode6_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
    u32 temp;

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

    if (cpu_has_died) {
		static u8 __iomem *zero;
	    u32 trampoline_code_size = &xcode6_secondary_trampoline_end -
						&xcode6_secondary_trampoline;
		u32 trampoline_size = &xcode6_secondary_trampoline_jump -
						&xcode6_secondary_trampoline;

        zero = (__force u8 __iomem *)PAGE_OFFSET;

        /*
                * This is elegant way how to jump to any address
                * 0x0: Load address at 0x8 to r0
                * 0x4: Jump by mov instruction
                * 0x8: Jumping address
                */
        memcpy((__force void *)zero, &xcode6_secondary_trampoline,
                        trampoline_size);
        writel(virt_to_phys(secondary_startup), zero + trampoline_size);
        flush_cache_all();
        outer_flush_range(virt_to_phys((void*)PAGE_OFFSET), trampoline_code_size);
        smp_wmb();

        smp_wmb();
        temp = mmr_read(PROC5_ARM_RESET);
        temp &= (~0x2);
        mmr_write(temp, PROC5_ARM_RESET);
        smp_wmb();
        temp = mmr_read(PROC5_ARM_CLK);
        temp |= 2;
        mmr_write(temp, PROC5_ARM_CLK);
        smp_wmb();
        temp = mmr_read(PROC5_ARM_RESET);
        temp |= 0x2;
        mmr_write(temp, PROC5_ARM_RESET);
        smp_wmb();
        temp = 0;
        mmr_write(temp, PROC5_ARM_CLK); //restore clock for cpu1
        smp_wmb();
        cpu_has_died = 0;
    }

	flush_cache_all();
	mmr_write(0xcafebabe, CG_DUMMY_REG);
	smp_wmb();
	//arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	/*
	 * Now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

static void wakeup_secondary(void)
{
	printk("Wake secondary....do nothing now!\n");
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init xcode6_smp_init_cpus(void)
{
	unsigned int i, ncores;

	scu_base = (void __iomem *)(CORE_PERIPHERAL_BASE + CORE_SCU_OFFSET);
	ncores = scu_get_core_count(scu_base);

	/* sanity check */
	if (ncores > NR_CPUS) {
		printk(KERN_WARNING
		       "XCODE: no. of cores (%d) greater than configured "
		       "maximum of %d - clipping\n",
		       ncores, NR_CPUS);
		ncores = NR_CPUS;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

//	set_smp_cross_call(gic_raise_softirq);
}

static void xcode6_smp_prepare_cpus(unsigned int max_cpus)
{

	/*
	 * Initialise the SCU and wake up the secondary core using
	 * wakeup_secondary().
	 */
	scu_enable(scu_base);
	wakeup_secondary();
}

/*
 * platform-specific code to shutdown a CPU
 *  
 * Called with IRQs disabled
 */
void xcode6_platform_cpu_die(unsigned int cpu)
{
    cpu_has_died = 1;

        /*
                * there is no power-control hardware on this platform, so all
                * we can do is put the core into WFI; this is safe as the calling
                * code will have already disabled interrupts
                */
        for (;;)
                cpu_do_idle();
}

struct smp_operations __initdata xcode_smp_ops = { 
    .smp_init_cpus      = xcode6_smp_init_cpus,
    .smp_prepare_cpus   = xcode6_smp_prepare_cpus,
    .smp_secondary_init = xcode6_secondary_init,
    .smp_boot_secondary = xcode6_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
    .cpu_die            = xcode6_platform_cpu_die,
#endif
};

