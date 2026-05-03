/**\file
 *  The top most file which makes use of synopsys GMAC driver code.
 *
 *  This file can be treated as the example code for writing a application driver
 *  for synopsys GMAC device using the driver provided by Synopsys.
 *  This exmple is for Linux 2.6.xx kernel 
 *  - Uses 32 bit 33MHz PCI Interface as the host bus interface
 *  - Uses Linux network driver and the TCP/IP stack framework
 *  - Uses the Device Specific Synopsys GMAC Kernel APIs
 *  \internal
 * ---------------------------REVISION HISTORY--------------------------------
 * Synopsys 			01/Aug/2007			Created
 * Stanley                      11/Aug/2008                     Revised
 */

//#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <linux/pci.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>

#include "synopGMAC_Host.h"
#include "synopGMAC_banner.h"
#include "synopGMAC_plat.h"

#include "synopGMAC_network_interface.h"
#include "synopGMAC_Dev.h"


/****************************************************/


struct net_device * g_netdev[ETH_NUM] = {NULL};

// To enable or disable LRO
// LRO slowdown TSO a little bit, because the TCP ACK is not handled ASAP
// So if your application needs the maximun tranmit performance, don't enable LRO
int xc3_use_lro = 0;

#define LRO_FLUSH_INTERVAL  30  // wait too long to aggrate TCP is not good idea
                                // wait 30ms to push to TCP stack

#define	NUM_SKB_LIST	(3096)

LIST_HEAD(skb_list);

struct skb_list_node {
	struct sk_buff *skb;
    struct list_head mylist;
};

static spinlock_t skb_list_lock;
static struct semaphore sem_alloc_skb;

/***************************************************/
int xc_allocate_skb_helper_thread(void *data)
{
	struct net_device *netdev;
	struct sk_buff *skb;
	struct skb_list_node *skb_entry;
	unsigned long flags;
	
	netdev = (struct net_device *)&g_netdev[0];

	spin_lock_init(&skb_list_lock);
	sema_init(&sem_alloc_skb, NUM_SKB_LIST);
	
	while(1)
	{
		down_interruptible(&sem_alloc_skb);

		skb_entry = kmalloc(sizeof(struct skb_list_node), GFP_KERNEL);
		if (skb_entry == NULL)
		{
			TR0("xc_allocate_skb_helper_thread, skb_entry allocattion failed\n");
		}

		skb = netdev_alloc_skb(netdev, 1500 + ETHERNET_PACKET_EXTRA + 64);  // two cache lines extra

		if(skb == NULL){
			TR0("low memory, xc_allocate_skb_helper_thread, SKB memory allocation failed\n");
			kfree(skb_entry);
			up(&sem_alloc_skb);
			continue;
		}

		skb_reserve(skb, 32 - ((unsigned int)skb->tail & 0x1f) + 2);  // recv buffer align to cache line

		skb_entry->skb = skb;
		
		INIT_LIST_HEAD(&(skb_entry->mylist));
		spin_lock_irqsave(&skb_list_lock, flags);
		list_add_tail(&skb_entry->mylist, &skb_list);
		spin_unlock_irqrestore(&skb_list_lock, flags);

	}
}

struct sk_buff *xc_get_skb_from_list(void)
{
	struct skb_list_node *skb_entry;
	struct sk_buff *skb;
	unsigned long flags;

	if (list_empty(&skb_list))
	{
		TR("skb list used up\n");
		return NULL;
	}

	spin_lock_irqsave(&skb_list_lock, flags);
	skb_entry = list_first_entry(&skb_list, struct skb_list_node, mylist);
	skb = skb_entry->skb;
	list_del(&skb_entry->mylist);
	spin_unlock_irqrestore(&skb_list_lock, flags);
	kfree(skb_entry);

	up(&sem_alloc_skb);

	return skb;
}


#if 0
//TODO lro_mgr is changed
int xc3_lro_helper_thread(void *data)  // helps with lro
{
#if 1
    synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice            *gmacdev;
    unsigned int               lockflags;
    spinlock_t lock_lro_skb;

    adapter = (synopGMACPciNetworkAdapter *)netdev_priv(g_netdev[0]);  // only support eth0 for now
    gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

    spin_lock_init (&lock_lro_skb);

	while(1)
    {
        msleep(LRO_FLUSH_INTERVAL);
    
    	TR("arc_gmac_thread lro_flush_all\n");
	    spin_lock_irqsave(&lock_lro_skb, lockflags);
        lro_flush_all(&gmacdev->lro_mgr);
		spin_unlock_irqrestore(&lock_lro_skb, lockflags);
           
    }
#endif
 
}

#endif

//
// helper thread to cleanup Tx dma queue 
// to avoid tso retransmit deadlock case when packet lost
// 
#ifdef SYNOPSYS_TSO_ENABLE

#define TSO_FREE_SKB_HELPER_SLEEP_INTERVAL  msecs_to_jiffies(100)   // 100ms, for 100Mbps, about 1M Bytes data in tx queue, for 1Gbps, tx queue will never full  
struct timer_list tso_helper_timer;
spinlock_t  lock_tso_skb;

void tso_helper_timer_func(unsigned long data)
{
    struct net_device          *netdev;
    struct net_device          **pnetdev = (struct net_device **)data ;
    synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice            *gmacdev;
  	int i;
	
	for(i=0;i<ETH_NUM;i++,++pnetdev)
	{
		netdev = *pnetdev;
    	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);  
	    gmacdev = (synopGMACdevice *)adapter->synopGMACdev;
	
	    if (netdev && (netdev->flags & IFF_UP) && netif_running(netdev))
    	{
    		xc3_release_tx_qptr(netdev, gmacdev);
			netif_wake_queue(netdev);
        }
    }
    
    tso_helper_timer.expires = jiffies + TSO_FREE_SKB_HELPER_SLEEP_INTERVAL;
    add_timer(&tso_helper_timer);        
}

void xc3_tso_helper_timer(void)  // helps with tso
{
    TR0("xc3_tso_helper_timer\n");

    spin_lock_init(&lock_tso_skb);
 
    init_timer((struct timer_list *)&tso_helper_timer);
    tso_helper_timer.function = tso_helper_timer_func;
    tso_helper_timer.data = (uint)g_netdev;
    tso_helper_timer.expires = jiffies + TSO_FREE_SKB_HELPER_SLEEP_INTERVAL;
    add_timer(&tso_helper_timer);
}
#endif

#ifdef CONFIG_PM
extern void synopGMAC_pm(int, int);
static int synopsis_suspend(struct platform_device *dev, pm_message_t state)
{
	printk("Synopsis: suspend\n");
	synopGMAC_pm(0, 0);
	//synopGMAC_pm(1, 0);   // eth1 is not enabled by default
	return 0;
}

static int synopsis_resume(struct platform_device *dev)
{
	printk("Synopsis: resume\n");
	synopGMAC_pm(0, 1);
	//synopGMAC_pm(1, 1);
	return 0;
}

static struct platform_driver synopsis_driver = {
	.driver		= {
		.name	= "synopsis",
		.owner	= THIS_MODULE,
	},
	.suspend	= synopsis_suspend,
	.resume		= synopsis_resume,
};

static struct platform_device *synopsis_platform_device;

static int __init synopsis_init(void)
{
	int error;

	printk("%s\n", __FUNCTION__);

	error = platform_driver_register(&synopsis_driver);
	if (error)
		return error;

	synopsis_platform_device = platform_device_alloc("synopsis", -1);
	if (!synopsis_platform_device) {
		error = -ENOMEM;
		goto err_driver_unregister;
	}

	error = platform_device_add(synopsis_platform_device);
	if (error)
		goto err_free_device;


	return 0;

 err_free_device:
	platform_device_put(synopsis_platform_device);
 err_driver_unregister:
	platform_driver_unregister(&synopsis_driver);
	return error;
}

static void __exit synopsis_exit(void)
{
	platform_device_unregister(synopsis_platform_device);
	platform_driver_unregister(&synopsis_driver);
	printk(KERN_INFO "synopsis: removed.\n");
}
#endif

#if 0
#include <linux/proc_fs.h>
static int SynopGMAC_proc_show(struct seq_file *m, void *v)
{
    synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice            *gmacdev;
	unsigned int x;

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(g_netdev[0]);  // only support eth0 for now
    gmacdev = (synopGMACdevice *)adapter->synopGMACdev;


    seq_printf(m, "DmaBusMode = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaBusMode));
    seq_printf(m, "DmaTxPollDemand = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaTxPollDemand));
    seq_printf(m, "DmaRxPollDemand = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaRxPollDemand));
    seq_printf(m, "DmaRxBaseAddr = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaRxBaseAddr));
    seq_printf(m, "DmaTxBaseAddr = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaTxBaseAddr));
    seq_printf(m, "DmaStatus = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaStatus));
    seq_printf(m, "DmaControl = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaControl));
    seq_printf(m, "DmaInterrupt = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaInterrupt));
    seq_printf(m, "DmaMissedFr = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaMissedFr));
    seq_printf(m, "DmaTxCurrDesc = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaTxCurrDesc));
    seq_printf(m, "DmaRxCurrDesc = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaRxCurrDesc));
    seq_printf(m, "DmaTxCurrAddr = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaTxCurrAddr));
    seq_printf(m, "DmaRxCurrAddr = 0x%08x\n", synopGMACReadReg((u32)gmacdev->DmaBase, DmaRxCurrAddr));

    seq_printf(m, "GmacConfig = 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacConfig));
    seq_printf(m, "GmacFrameFilter = 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacFrameFilter));
    seq_printf(m, "GmacHashHigh= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacHashHigh));
    seq_printf(m, "GmacHashLow= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacHashLow));
    seq_printf(m, "GmacGmiiAddr= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacGmiiAddr));
    seq_printf(m, "GmacGmiiData= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacGmiiData));
    seq_printf(m, "GmacFlowControl= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacFlowControl));
    seq_printf(m, "GmacVlan= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacVlan));
    seq_printf(m, "GmacVersion= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacVersion));
    seq_printf(m, "GmacWakeupAddr= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacWakeupAddr));
    seq_printf(m, "GmacPmtCtrlStatus= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacPmtCtrlStatus));
    seq_printf(m, "GmacInterruptStatus= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacInterruptStatus));
    seq_printf(m, "GmacInterruptMask= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacInterruptMask));
    seq_printf(m, "GmacAddr0High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr0High));
    seq_printf(m, "GmacAddr0Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr0Low));
    seq_printf(m, "GmacAddr1High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr1High));
    seq_printf(m, "GmacAddr1Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr1Low));
    seq_printf(m, "GmacAddr2High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr2High));
    seq_printf(m, "GmacAddr2Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr2Low));
    seq_printf(m, "GmacAddr3High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr3High));
    seq_printf(m, "GmacAddr3Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr3Low));
    seq_printf(m, "GmacAddr4High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr4High));
    seq_printf(m, "GmacAddr4Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr4Low));
    seq_printf(m, "GmacAddr5High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr5High));
    seq_printf(m, "GmacAddr5Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr5Low));
    seq_printf(m, "GmacAddr6High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr6High));
    seq_printf(m, "GmacAddr6Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr6Low));
    seq_printf(m, "GmacAddr7High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr7High));
    seq_printf(m, "GmacAddr7Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr7Low));
    seq_printf(m, "GmacAddr8High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr8High));
    seq_printf(m, "GmacAddr8Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr8Low));
    seq_printf(m, "GmacAddr9High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr9High));
    seq_printf(m, "GmacAddr9Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr9Low));
    seq_printf(m, "GmacAddr10High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr10High));
    seq_printf(m, "GmacAddr10Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr10Low));
    seq_printf(m, "GmacAddr11High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr11High));
    seq_printf(m, "GmacAddr11Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr11Low));
    seq_printf(m, "GmacAddr12High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr12High));
    seq_printf(m, "GmacAddr12Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr12Low));
    seq_printf(m, "GmacAddr13High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr13High));
    seq_printf(m, "GmacAddr13Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr13Low));
    seq_printf(m, "GmacAddr14High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr14High));
    seq_printf(m, "GmacAddr14Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr14Low));
    seq_printf(m, "GmacAddr15High= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr15High));
    seq_printf(m, "GmacAddr15Low= 0x%08x\n", synopGMACReadReg((u32)gmacdev->MacBase, GmacAddr15Low));

#if 0
	x=synopGMACReadReg((u32)gmacdev->MacBase, GmacFlowControl);
	x|=1;
    synopGMACWriteReg((u32)gmacdev->MacBase, GmacFlowControl, x);
	mdelay(1000);
#endif

    return 0;
}

static int SynopGMAC_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, SynopGMAC_proc_show, PDE(inode)->data);
}

static const struct file_operations SynopGMAC_proc_fops = {
    .owner      = THIS_MODULE,
    .open       = SynopGMAC_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
};

static void __devinit proc_eth_init(void)
{
    struct proc_dir_entry *pde;

    pde = proc_create("SynopGMAC", 0444, NULL, &SynopGMAC_proc_fops);
    if (!pde) {
        printk("Ethernet failed to create /proc entry\n");
        return;
    }
}
#endif

int __init SynopGMAC_Host_Interface_init(void)
{
    int retval;
	struct task_struct *thread1;

	TR0("**********************************************************\n");
	TR0("* Driver    :%s\n",synopGMAC_driver_string);
	TR0("* Version   :%s\n",synopGMAC_driver_version);
	TR0("* Copyright :%s\n",synopGMAC_copyright);
	TR0("**********************************************************\n");

    TR0("Initializing synopsys GMAC interfaces ..\n") ;
    /* Initialize the bus interface for the hostcontroller E.g PCI in our case */
        
	/*Now we have got pdev structure from pci interface. Lets populate it in our global data structure*/	

    turnon_eth_clk();
    
	plat_eth_init();

//	proc_eth_init();

	/* Initialize the Network dependent services */
    if((retval = synopGMAC_init_network_interface(0))){
		TR0("Could not initialize the Network interface.\n");
		return retval;
	}
	
#if CONFIG_VIXS_SYNOPSYS_NUM_PORTS >=2
    if((retval = synopGMAC_init_network_interface(1))){
		TR0("Could not initialize the Network interface.\n");
		return retval;
	}
#endif
    
#ifdef CONFIG_PM  
	synopsis_init();
#endif

#if 0
	#ifndef CONFIG_SYNOPSYS_NAPI
    if (xc3_use_lro)
    {
        TR0("LRO is enabled\n");
        kernel_thread (xc3_lro_helper_thread, NULL, CLONE_KERNEL);
    }
	#endif
#endif

#if defined(SYNOPSYS_TSO_ENABLE) && !defined(TR_INTR_ENABLE)
    xc3_tso_helper_timer();
#endif

	thread1 = kthread_create(xc_allocate_skb_helper_thread, NULL, "xc_skb_helper"); 
    if (thread1)
    {
        wake_up_process(thread1);
    }
		
	return 0 ;
}

void __exit SynopGMAC_Host_Interface_exit(void)
{
#ifdef CONFIG_PM  
	synopsis_exit();
#endif

//	remove_proc_entry("SynopGMAC", NULL);

	TR0("Exiting synopsys GMAC interfaces ..\n") ;

	/* De-Initialize the Network dependent services */
	synopGMAC_exit_network_interface();

	turnoff_eth_clk();
	
	TR0("Exiting synopGMAC_exit_network_interface\n");

}

module_init(SynopGMAC_Host_Interface_init);
module_exit(SynopGMAC_Host_Interface_exit);

MODULE_AUTHOR("Synopsys India");
MODULE_LICENSE("GPL/BSD");
MODULE_DESCRIPTION("SYNOPSYS GMAC NETWORK DRIVER WITH PCI INTERFACE");

