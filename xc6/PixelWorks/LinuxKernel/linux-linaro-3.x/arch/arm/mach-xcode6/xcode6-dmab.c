#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/spinlock.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <mach/hardware.h>
#include <mach/xcode6-common.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#define VIXS_DMA_MAX_TRANSFER_SIZE	(0x10000)  

#define	VIXS_DMA_INTMIPS			(1 << DMA_DIRECT_DMACMD_B_INTMIPS_SHIFT)
#define VIXS_DMA_INTHOST			(1 << DMA_DIRECT_DMACMD_B_INTHOST_SHIFT)
#define VIXS_DMA_SRC_FRAME			(1 << DMA_DIRECT_DMACMD_B_SRCSPACE_SHIFT)
#define VIXS_DMA_SRC_SYSTEM			(0 << DMA_DIRECT_DMACMD_B_SRCSPACE_SHIFT)
#define VIXS_DMA_DST_FRAME			(1 << DMA_DIRECT_DMACMD_B_DSTSPACE_SHIFT)
#define VIXS_DMA_DST_SYSTEM			(0 << DMA_DIRECT_DMACMD_B_DSTSPACE_SHIFT)
#define VIXS_DMA_SWAPCTRL_32_BIT        	(2<<DMA_DIRECT_DMACMD_B_SWAP_SHIFT)


#define VIXS_DMA_HW_MUTEX_REG       MIPS_INTERLOCK1
#define VIXS_DMA_HW_MUTEX_ID        6        
#define VIXS_DMA_HW_MUTEX_ID_SHIFT  (4*VIXS_DMA_HW_MUTEX_ID)        // each mutex ID takes 4 bits   
#define VIXS_DMA_HW_MUTEX_MASK      0x0F000000

#define V_HOST_MUTEX_START                    0x8
#define V_XC_INDIRECT_DMA0_MUTEX                    MIPS_INTERLOCK0
#define V_XC_INDIRECT_DMA0_MUTEX_ID_SHIFT           MIPS_INTERLOCK0_ID6_SHIFT
#define V_XC_INDIRECT_DMA0_INTERLOCK_DRV_ID   (V_HOST_MUTEX_START+5)
#define DMA_INTERLOCK_FUNC_ID	            (0x4)

#define V_XC_IIA_MUTEX                              MIPS_INTERLOCK7
#define V_XC_IIA_MUTEX_ID_SHIFT           MIPS_INTERLOCK1_ID5_SHIFT
#define V_XC_IIA_MUTEX_MASK               MIPS_INTERLOCK1_ID5_MASK
#define V_XC_IIA_INTERLOCK_DRV_ID   (V_HOST_MUTEX_START+4)
#define IIA_INTERLOCK_FUNC_ID	            (0x4)


#define MAX_DMA_PAGES 132

#define     ONEPAGE             1
#define     MORETHANONEPAGE     2

#define     MAXSCATTERENTRIES   132


#define     DMAQUEUEEMPTY                       0
#define     DMAQUEUEFULL                        1
#define     DMADATABUFFREE                      0
#define     DMADATABUFUSED                      1

#define     DMAMINMSTIMEOUT                     10
#define     SIZEOFMUTEXSTUCT                        sizeof(spinlock_t)


#define vixs_reg_writel(val, addr)  writel(val, (volatile unsigned int *)(XC_SOC_PROC_MMREG_BASE + addr))
#define vixs_reg_readl(addr)          readl((volatile unsigned int *)(XC_SOC_PROC_MMREG_BASE + addr))

//static struct semaphore   g_dmab_sema;
//static wait_queue_head_t  g_dmab_queue;   
//static unsigned int       g_dmab_flag;
static spinlock_t         g_lock;	

int xc_dma_host_to_host(unsigned char* _dest, unsigned char* _src, unsigned int _len){
    unsigned long src;
    unsigned long dest;
    unsigned int len, transferlen;
    unsigned int command = 0;
    unsigned int temp;
    unsigned long flags;
    unsigned long timeout=0;

    if(_len == 0) {
        return -1;
    }
    spin_lock_irqsave(&g_lock,flags);
    temp = (vixs_reg_readl(VIXS_DMA_HW_MUTEX_REG) & VIXS_DMA_HW_MUTEX_MASK)>>VIXS_DMA_HW_MUTEX_ID_SHIFT;

    if (temp == DMA_INTERLOCK_FUNC_ID){
        printk("fatal error, hw lock taken by myself already??, hw lock=%x\n", temp);
	BUG();
    }
    
    while (temp != DMA_INTERLOCK_FUNC_ID){
        vixs_reg_writel(DMA_INTERLOCK_FUNC_ID<<VIXS_DMA_HW_MUTEX_ID_SHIFT,  VIXS_DMA_HW_MUTEX_REG);
        temp = (vixs_reg_readl(VIXS_DMA_HW_MUTEX_REG)& VIXS_DMA_HW_MUTEX_MASK) >> VIXS_DMA_HW_MUTEX_ID_SHIFT;
    }
    

    len = _len;
    src = (unsigned long) _src;
    dest = (unsigned long) _dest;
    //printk("_src:%x src:%x _dest:%x dest:%x\n",_src,src,_dest,dest);
    do
    {
        
        if(len > VIXS_DMA_MAX_TRANSFER_SIZE)
        {
            transferlen = VIXS_DMA_MAX_TRANSFER_SIZE;
        }
        else
        {
            transferlen = len;
        }

        if (transferlen == VIXS_DMA_MAX_TRANSFER_SIZE)
            command = 0;        // size 0 means 64KB
        else
            command = transferlen;
        
        command |= VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_FRAME;
//      command |= VIXS_DMA_INTMIPS;

        if (vixs_reg_readl(DMA_STATUS) & DMA_STATUS_ERR_DIRECT_B_MASK) 
        {
            printk("dma error??? reg(XC_DMA_STATUS)=0x%08x\n", vixs_reg_readl(DMA_STATUS));
            printk("reg dest=%x\n", vixs_reg_readl(DMA_DIRECT_LDST_B));
            printk("reg src=%x\n",  vixs_reg_readl(DMA_DIRECT_LSRC_B));
            printk("reg cmd=%x\n",  vixs_reg_readl(DMA_DIRECT_DMACMD_B));
            BUG();
        }
	  

        vixs_reg_writel(dest, DMA_DIRECT_LDST_B);
        vixs_reg_writel(src, DMA_DIRECT_LSRC_B);
        vixs_reg_writel(0, DMA_DIRECT_UDST_B);
        vixs_reg_writel(0, DMA_DIRECT_USRC_B);
        timeout=jiffies + 3*HZ;//3 

        vixs_reg_writel(command, DMA_DIRECT_DMACMD_B);

     	#if 0
        if ((vixs_reg_readl(DMA_STATUS) & DMA_STATUS_DIRECT_B_CURRENT_MASK) == 0)
        {
            printk("BAD !!! BAD!!!\n");
            BUG();
        }
        #endif
	    while (vixs_reg_readl(DMA_STATUS) & DMA_STATUS_DIRECT_B_CURRENT_MASK)
        {
	        if(time_after(jiffies,timeout))
            {
                printk("polling XC_DMA_STATUS timeout, reg(XC_DMA_STATUS)=0x%08x\n", vixs_reg_readl(DMA_STATUS));
		        BUG();
                break;
            }
        }

        if (vixs_reg_readl(DMA_STATUS) & DMA_STATUS_ERR_DIRECT_B_MASK){
            printk("dma error??? reg(XC_DMA_STATUS)=0x%08x\n", vixs_reg_readl(DMA_STATUS));
            printk("reg dest=%x %x\n", vixs_reg_readl(DMA_DIRECT_LDST_B),vixs_reg_readl(DMA_DIRECT_UDST_B));
            printk("reg src=%x %x \n",  vixs_reg_readl(DMA_DIRECT_LSRC_B),vixs_reg_readl(DMA_DIRECT_USRC_B));
            printk("reg cmd=%x\n",  vixs_reg_readl(DMA_DIRECT_DMACMD_B));
            printk("hw lock=%x\n", vixs_reg_readl (VIXS_DMA_HW_MUTEX_REG));
        }
	
        dest += transferlen;
        src += transferlen;
        len -= transferlen;
    }while(len);

    

    temp = (vixs_reg_readl(VIXS_DMA_HW_MUTEX_REG) & VIXS_DMA_HW_MUTEX_MASK) >> VIXS_DMA_HW_MUTEX_ID_SHIFT;
    if (temp != DMA_INTERLOCK_FUNC_ID){
        printk("fatal error, DMA_HW_MUTEX temp=%x next read=%x, should be %x\n", temp,vixs_reg_readl(VIXS_DMA_HW_MUTEX_REG)>>VIXS_DMA_HW_MUTEX_ID_SHIFT, DMA_INTERLOCK_FUNC_ID);
	    BUG();
    }
    
    vixs_reg_writel(DMA_INTERLOCK_FUNC_ID<<VIXS_DMA_HW_MUTEX_ID_SHIFT,  VIXS_DMA_HW_MUTEX_REG);
    spin_unlock_irqrestore(&g_lock, flags);
    return 0;
}

EXPORT_SYMBOL(xc_dma_host_to_host);

void __dma_test(void){
    unsigned char* src;
    unsigned char* dest;
    int j;

#define DMA_SIZE 1024*256
    src = kmalloc(DMA_SIZE,GFP_KERNEL);
    dest = kmalloc(DMA_SIZE,GFP_KERNEL);
    memset(src,0xee,DMA_SIZE);
    memset(dest,0x0,DMA_SIZE);
    flush_dcache_range((unsigned int)src,(unsigned int)src+DMA_SIZE);
    flush_dcache_range((unsigned int)dest,(unsigned int)dest+DMA_SIZE);
    printk("src:%p dest:%p DMA_SIZE:%x\n",src,dest,DMA_SIZE);
    xc_dma_host_to_host((unsigned char*)virt_to_phys(dest),(unsigned char*)virt_to_phys(src), DMA_SIZE);
    inv_dcache_range((unsigned int)dest,(unsigned int)dest+DMA_SIZE);
    for(j=0;j<DMA_SIZE;j++){
           if(src[j] != dest[j]){
              printk("dma fail:%d src:%x dest:%x\n",j, src[j],dest[j]);
             break;
           }
    }
    if(j==DMA_SIZE){
       printk("dma pass! len:%d\n",DMA_SIZE);
    }

    kfree(src);
    kfree(dest);
}

static int __init xc_dma_init(void)
{
	unsigned int temp=0;

	
    temp = vixs_reg_readl ( ACC_RESET_REG0);
    temp |= ACC_RESET_REG0_DMA_RESET_MASK;
    vixs_reg_writel (temp, ACC_RESET_REG0);

    //clear dma reset
    temp &= ~ACC_RESET_REG0_DMA_RESET_MASK;
    vixs_reg_writel ( temp,ACC_RESET_REG0);

    spin_lock_init(&g_lock);
//    __dma_test();
	return 0;
}
early_initcall(xc_dma_init);
