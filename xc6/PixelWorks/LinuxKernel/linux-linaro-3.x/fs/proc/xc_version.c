#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>

char bootver[32];

static int __init bootver_setup(char *line)
{
    strcpy(bootver, line);
    return 1;
}

__setup("bootver=", bootver_setup);

static int xc_version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s:%08x\n", bootver, KNLVER);
	return 0;
}

static int xc_version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, xc_version_proc_show, NULL);
}

static const struct file_operations xc_version_proc_fops = {
	.open		= xc_version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_xc_version_init(void)
{
	proc_create("xc_version", 0, NULL, &xc_version_proc_fops);
	return 0;
}
module_init(proc_xc_version_init);
