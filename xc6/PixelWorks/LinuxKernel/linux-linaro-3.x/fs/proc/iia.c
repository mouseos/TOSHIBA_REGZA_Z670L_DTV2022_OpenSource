#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <mach/iia.h>

/*
 * /proc/iia
 */

static int show_iia(struct seq_file *p, void *v)
{
    int i,j; 

	j=*(loff_t *)v;

	if(j>0)
		return 0;

    for(i=0;i<=GET_INT_GROUP(IIA_INT_END);i++)
        seq_printf(p, "IIA Grp %d: Status %08x Mask %08x\n", i, IIALocalReadStatus(i*32), IIALocalReadMask(i*32)); 

	return 0;
}

static void *int_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos <= nr_irqs) ? pos : NULL;
}

static void *int_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos > nr_irqs)
		return NULL;
	return pos;
}

static void int_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static const struct seq_operations int_seq_ops = {
	.start = int_seq_start,
	.next  = int_seq_next,
	.stop  = int_seq_stop,
	.show  = show_iia,
};

static int iia_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &int_seq_ops);
}

static const struct file_operations proc_iia_operations = {
	.open		= iia_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proc_iia_init(void)
{
	proc_create("iia", 0, NULL, &proc_iia_operations);
	return 0;
}
module_init(proc_iia_init);
