#ifdef CONFIG_XC5_VIRTUAL_USB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/usb/hcd.h>
#include "vusb-hcd.h"


extern volatile unsigned int xc5_pci_dev_initialized;

u32 VUSB_HCD_REG_BASE = 0;
EXPORT_SYMBOL(VUSB_HCD_REG_BASE);

u32 VUSB_HCD_REF_CNT = 0;
EXPORT_SYMBOL(VUSB_HCD_REF_CNT);

u32 VUSB_HCD_ACTIVE_CNT = 0;
EXPORT_SYMBOL(VUSB_HCD_ACTIVE_CNT);

spinlock_t vusb_usb_indirect_reg_lock;
EXPORT_SYMBOL(vusb_usb_indirect_reg_lock);

/*
 * The DMA throughput is 200MBps, the 64KB transfer take around 320us to complete.
 * When system busy, the DMA eingine can be occupied by firmware around 40ms, 
 * set the wait time as 60ms + Elem * 350us
 */
#define DMA_POLL_INTERVAL	5
#define MAX_DMA_TIMEOUT (700)
#define DMA_WAIT_TIME 		(12000)

//#define DMA_PROFILE
#ifdef DMA_PROFILE
static volatile u32 t1, t2, ms;
#endif

#ifdef CONFIG_VIRTUAL_XC5_SATA
extern spinlock_t g_xcode5_dma0_lock;
extern spinlock_t g_xcode5_dmab_lock;
#else
spinlock_t g_xcode5_dma0_lock;
spinlock_t g_xcode5_dmab_lock;
EXPORT_SYMBOL(g_xcode5_dma0_lock);
EXPORT_SYMBOL(g_xcode5_dmab_lock);
#endif

XC_USBH_priv_struct g_XC_USBH_info;
EXPORT_SYMBOL(g_XC_USBH_info);

volatile u32 g_XC_USBH_Global_Struct_Initialized = 0;
EXPORT_SYMBOL(g_XC_USBH_Global_Struct_Initialized);

#define VUSB_RETRY_LIMIT (1000)

void vusb_dump_regs(void){
	/*
	   volatile u32 temp=0;
	   temp = VMMR_READ(XC_USBD_PHY_CLK_CTRL);
	DBK("XC5 XC_USBD_PHY_CLK_CTRL:%x\n",temp);
	   temp = VMMR_READ(XC_USBD_PHY_CTRL);
	DBK("XC5 XC_USBD_PHY_CTRL:%x\n",temp);
	   temp = VMMR_READ(XC_USBH_PHY_CTRL_REG);
	DBK("XC5 XC_USBH_PHY_CTRL_REG:%x\n",temp);

	temp = readl ((void*)XC_SOC_PROC_MMREG_BASE+  USBD_PHY_CLK_CTRL);
	DBK("XC6 XC_USBD_PHY_CLK_CTRL:%x\n",temp);
        temp = readl (void*)XC_SOC_PROC_MMREG_BASE + USBD_PHY_CTRL);
	DBK("XC6 XC_USBD_PHY_CTRL:%x\n",temp);
        temp = readl ((void*)XC_SOC_PROC_MMREG_BASE + USBH_PHY_CTRL_REG);
	DBK("XC6 XC_USBH_PHY_CTRL_REG:%x\n",temp);*/

}
u32 vusb_writel(u32 VUSB_reg_base, u32 ehci_reg_addr, u32 value, e_vusb_hc_reg_type type)
{
	u32 regVal = 0, retry = 0;
    unsigned long flags;
    //DBK("[%s:%d] \n",__func__,__LINE__);
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);

	while(((regVal = readl((void*)(VUSB_reg_base + VUSB_IAS_REG)))&0x1)== 1)
	{
		if(++retry >= VUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}



    writel(value, (void*)(VUSB_reg_base + VUSB_WDATA_REG));
    switch(type)
    {
        case VUSB_EHCI_REG:
            writel(ehci_reg_addr|0x80000000, (void*)(VUSB_reg_base + VUSB_EOC_REG));
            break;
        case VUSB_OHCI_REG:
            writel(ehci_reg_addr|0x80000000, (void*)(VUSB_reg_base + VUSB_OOC_REG));
            break;
        default:
            spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
            return 0;
            break;
    }

	retry = 0;
	while(((regVal = readl((void*)(VUSB_reg_base + VUSB_IAS_REG)))&0x1)== 1)
	{
		if(++retry >= VUSB_RETRY_LIMIT) {
			BUG();
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
			return -EBUSY;
		}
	}

	if(unlikely(regVal&0x2))
		BUG();

	spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);	
    return 0;
};
EXPORT_SYMBOL(vusb_writel);


u32 vusb_readl(u32 VUSB_reg_base, u32 ehci_reg_addr, e_vusb_hc_reg_type type)
{
	u32 regVal = 0, retry = 0;
    unsigned long  flags;
 // DBK("[%s:%d] \n",__func__,__LINE__);
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);

    while(((regVal = readl((void*)(VUSB_reg_base + VUSB_IAS_REG)))&0x1)== 1)
    {
		if(++retry >= VUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
			return 0;
		}
	}

 // DBK("ehci_reg_addr:%x VUSB_reg_base:%x type:%d\n",ehci_reg_addr,VUSB_reg_base,type);

    switch(type)
    {
    case VUSB_EHCI_REG:
    	writel(ehci_reg_addr, (void*)(VUSB_reg_base + VUSB_EOC_REG));
    	break;
    case VUSB_OHCI_REG:
    	writel(ehci_reg_addr, (void*)(VUSB_reg_base + VUSB_OOC_REG));
        break;
    default:
        spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    	 return 0;
    	 break;
    }

    while(((regVal = readl((void*)(VUSB_reg_base + VUSB_IAS_REG)))&0x1)== 1)
    {
		if(++retry >= VUSB_RETRY_LIMIT) {
			BUG();
			spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
			return 0;
		}
	}

	if(regVal&0x2) {
		BUG();
		spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
		return 0;
    };

    regVal = readl((void*)(VUSB_reg_base + VUSB_RDATA_REG));
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    return regVal;
};
EXPORT_SYMBOL(vusb_readl);



u32 vusb_data_buf_allocate(void)
{
    u32 ret;
    unsigned long  flags;
    
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    if(g_XC_USBH_info.ms_data_buf_pool.mps_next == NULL)
    {
        DBK("Failed to allocate data buffer\n");
        spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
	BUG();
    }


    ret = g_XC_USBH_info.ms_data_buf_pool.mps_next->m_paddr;
    //memset((void*)(g_XC_USBH_info.mp_mmfb + ret), 0, USBH_DATA_BUF_SEG_SIZE); 
    g_XC_USBH_info.ms_data_buf_pool.mps_next = g_XC_USBH_info.ms_data_buf_pool.mps_next->mps_next;

    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    DBK(" ret:%x\n",ret);
    return ret;
}
EXPORT_SYMBOL(vusb_data_buf_allocate);


void vusb_data_buf_free(u32 paddr)
{
    u32 index;
   unsigned long  flags;
   if(!paddr)
       return;

    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);

    index = (paddr - g_XC_USBH_info.ms_data_buf[0].m_paddr)/USBH_DATA_BUF_SEG_SIZE;

    g_XC_USBH_info.ms_data_buf[index].mps_next = g_XC_USBH_info.ms_data_buf_pool.mps_next;
    g_XC_USBH_info.ms_data_buf_pool.mps_next = &g_XC_USBH_info.ms_data_buf[index];
        
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
}

EXPORT_SYMBOL(vusb_data_buf_free);


u32 vusb_ed_hw_seg_allocate(dma_addr_t* handle)
{
    u32 ret;
    unsigned long flags;
    
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    if(g_XC_USBH_info.ms_ed_pool.mps_next == NULL)
    {
        DBK("Failed to allocate ed\n");
        spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
	BUG();
    }


    *handle = (dma_addr_t)g_XC_USBH_info.ms_ed_pool.mps_next->m_paddr;

    ret = (u32) g_XC_USBH_info.ms_ed_pool.mps_next->m_vaddr;
    g_XC_USBH_info.ms_ed_pool.mps_next = g_XC_USBH_info.ms_ed_pool.mps_next->mps_next;

    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    
    return ret;
}
EXPORT_SYMBOL(vusb_ed_hw_seg_allocate);



void vusb_ed_hw_seg_free(u32 ed_hw_seg_vaddr)
{
    u32 index;
    unsigned long flags;
 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    index = (ed_hw_seg_vaddr - g_XC_USBH_info.ms_ed_hw_seg[0].m_vaddr)/OHCI_ED_TD_SEG_DEFAULT_LEN;

    g_XC_USBH_info.ms_ed_hw_seg[index].mps_next = g_XC_USBH_info.ms_ed_pool.mps_next;
    g_XC_USBH_info.ms_ed_pool.mps_next = &g_XC_USBH_info.ms_ed_hw_seg[index];
        
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
}

EXPORT_SYMBOL(vusb_ed_hw_seg_free);

u32 vusb_td_hw_seg_allocate(dma_addr_t* handle)
{
    u32 ret;
   unsigned long flags;
 
    
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    if(g_XC_USBH_info.ms_td_pool.mps_next == NULL)
    {
        printk("%s:%d Failed to allocate td\n",__func__,__LINE__);
        spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
        return 0;
    }


    *handle = (dma_addr_t)g_XC_USBH_info.ms_td_pool.mps_next->m_paddr;

    ret = (u32) g_XC_USBH_info.ms_td_pool.mps_next->m_vaddr;
    g_XC_USBH_info.ms_td_pool.mps_next = g_XC_USBH_info.ms_td_pool.mps_next->mps_next;
    
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    return ret;
}
EXPORT_SYMBOL(vusb_td_hw_seg_allocate);

void vusb_td_hw_seg_free(u32 td_hw_seg_vaddr)
{
    u32 index;
    unsigned long flags; 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    index = (td_hw_seg_vaddr - g_XC_USBH_info.ms_td_hw_seg[0].m_vaddr)/OHCI_ED_TD_SEG_DEFAULT_LEN;

    g_XC_USBH_info.ms_td_hw_seg[index].mps_next = g_XC_USBH_info.ms_td_pool.mps_next;
    g_XC_USBH_info.ms_td_pool.mps_next = &g_XC_USBH_info.ms_td_hw_seg[index];
        
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
}
EXPORT_SYMBOL(vusb_td_hw_seg_free);


u32 vusb_qtd_hw_seg_allocate(dma_addr_t* handle)
{
    u32 ret;
    unsigned long flags; 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    
    //DBK("g_XC_USBH_info.qtd_pool.mps_next:%p\n",g_XC_USBH_info.qtd_pool.mps_next);
    if(g_XC_USBH_info.ms_qtd_pool.mps_next == NULL)
    {
        spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
        DBK("Failed to allocate qtd\n");
        return 0;//BUG();
    }


    *handle = (dma_addr_t)g_XC_USBH_info.ms_qtd_pool.mps_next->m_paddr;

    ret = (u32) g_XC_USBH_info.ms_qtd_pool.mps_next->m_vaddr;
    DBK("*handle:%x ret:%x\n",*handle,ret);
    g_XC_USBH_info.ms_qtd_pool.mps_next = g_XC_USBH_info.ms_qtd_pool.mps_next->mps_next;

    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    
    return ret;
}

EXPORT_SYMBOL(vusb_qtd_hw_seg_allocate);


void vusb_qtd_hw_seg_free(u32 ms_qtd_hw_seg_vaddr)
{
    u32 index;
    unsigned long flags; 
 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    index = (ms_qtd_hw_seg_vaddr - g_XC_USBH_info.ms_qtd_hw_seg[0].m_vaddr)/EHCI_QH_QTD_SEG_DEFAULT_LEN;

    g_XC_USBH_info.ms_qtd_hw_seg[index].mps_next = g_XC_USBH_info.ms_qtd_pool.mps_next;
    g_XC_USBH_info.ms_qtd_pool.mps_next = &g_XC_USBH_info.ms_qtd_hw_seg[index];
        
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
}

EXPORT_SYMBOL(vusb_qtd_hw_seg_free);

u32 vusb_qh_hw_seg_allocate(dma_addr_t* handle)
{
    u32 ret;
    unsigned long flags; 
 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    
    if(g_XC_USBH_info.ms_qh_pool.mps_next == NULL)
    {
        DBK("Failed to allocate qh\n");
	BUG();
    }


    *handle = (dma_addr_t)g_XC_USBH_info.ms_qh_pool.mps_next->m_paddr;

    ret = (u32) g_XC_USBH_info.ms_qh_pool.mps_next->m_vaddr;
    g_XC_USBH_info.ms_qh_pool.mps_next = g_XC_USBH_info.ms_qh_pool.mps_next->mps_next;
    
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
    return ret;
}

EXPORT_SYMBOL(vusb_qh_hw_seg_allocate);

void vusb_qh_hw_seg_free(u32 qh_hw_seg_vaddr)
{
    u32 index;
   unsigned long flags;
 
    spin_lock_irqsave(&vusb_usb_indirect_reg_lock, flags);
    index = (qh_hw_seg_vaddr - g_XC_USBH_info.ms_qh_hw_seg[0].m_vaddr)/EHCI_QH_QTD_SEG_DEFAULT_LEN;

    g_XC_USBH_info.ms_qh_hw_seg[index].mps_next = g_XC_USBH_info.ms_qh_pool.mps_next;
    g_XC_USBH_info.ms_qh_pool.mps_next = &g_XC_USBH_info.ms_qh_hw_seg[index];
        
    spin_unlock_irqrestore(&vusb_usb_indirect_reg_lock, flags);
}

EXPORT_SYMBOL(vusb_qh_hw_seg_free);
u32 xc_vusb_host_to_fb(u32 src, u32 dest, u32 len){
	unsigned char* ptr=(unsigned char*)phys_to_virt(src);
	dump_packet(ptr,len);
        ptr= (unsigned char* )g_XC_USBH_info.mp_mmfb+dest;
	dump_packet(ptr,len);
	DBK("src:%x g_XC_USBH_info.mp_mmfb+dest:%p len:%x\n",src, g_XC_USBH_info.mp_mmfb+dest,len);
	flush_dcache_range((u32)phys_to_virt(src),(u32)phys_to_virt(src)+len);
	memcpy((void*)(g_XC_USBH_info.mp_mmfb+dest),(void*)phys_to_virt(src),len);
	ptr= (unsigned char*)(g_XC_USBH_info.mp_mmfb+dest);
	dump_packet(ptr,len);
	return len;
}

u32 xc_vusb_fb_to_host(u32 src, u32 dest, u32 len){
	unsigned char* ptr=(unsigned char*)((u32)g_XC_USBH_info.mp_mmfb + src);
	dump_packet(ptr,len);
        ptr= phys_to_virt(dest);
	dump_packet(ptr,len);
	DBK("src:%x dest:%p len:%x\n",src,phys_to_virt(dest),len);

//	inv_dcache_range(phys_to_virt(dest),phys_to_virt(dest)+len);
	memcpy((void*)phys_to_virt(dest),(void*)g_XC_USBH_info.mp_mmfb + src,len);
	flush_dcache_range((u32)phys_to_virt(dest),(u32)phys_to_virt(dest)+len);
	ptr= phys_to_virt(dest);
	dump_packet(ptr,len);

	return len;
}
EXPORT_SYMBOL(xc_vusb_host_to_fb);

#ifdef CONFIG_XC5_VUSB_SGDMA
u32 Vixs_Build_64Bit_Info_To_IO(u32 * src, u32 * dest, u32 size)
{
#if defined(BIG_ENDIAN)
	if (size == sizeof(u32))
    {
		// writel(0, (u32 *)dest);
		// writel(*src, (u32 *)dest + 1);
		writel(*src, (u32 *)dest);
		writel(0, (u32 *)dest+1);
    }
    else
    {
        writel(*src, dest + 1);
        writel(*(src+1), (dest));
    }
#else

    writel(*src, dest);

    if (size == sizeof(unsigned long long))
    {
        writel(*(src+1), (dest+1));
    }
    else
    {
        writel(0, (dest+1));
    }
#endif

    return 1;
}

u32 XCIndirectDmaHostToFB (u32 addrSrcHost, u32 addrDestXC, u32 len)
{
	u32 count = 0, curlen;
	volatile u32 temp;
	u32 command = 0;
    PXC_DMADESCRIPTORS currentDescriptor = NULL;
    unsigned long  flags;

     
   DBK("src:%x dest:%x len:%d\n", addrSrcHost,addrDestXC,len);

    if((len/0x10000) > g_XC_USBH_info.desc_num)
    {
        printk("XCIndirectDmaHostToFB: len %x exceeds the max DMA size %x\n", len, g_XC_USBH_info.desc_num*0x10000);
        return 0;
    }

	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
        
    while(len > 0)
    {

        currentDescriptor = g_XC_USBH_info.pdescriptor + count;

        if(len > 0x10000)
        {
            curlen = 0x10000;
        }
        else
        {
            curlen = len;
        }

        command = len;

        command |= (VIXS_DMA_NOT_EOL|VIXS_DMA_DST_FRAME|VIXS_DMA_SRC_SYSTEM);

		Vixs_Build_64Bit_Info_To_IO(&addrDestXC, (u32 *)&currentDescriptor->m_dst_addr, sizeof(u32));

		Vixs_Build_64Bit_Info_To_IO(&addrSrcHost, (u32 *)&currentDescriptor->m_src_addr, sizeof(u32));

        writel(command, &currentDescriptor->m_command);
        writel(0, &currentDescriptor->m_control_word);
				
        count++;
        len -= curlen;
    }

    //point to the last descriptor
    currentDescriptor = g_XC_USBH_info.pdescriptor + (count -1);
    command = readl(&currentDescriptor->m_command);
    command |= VIXS_DMA_EOL;
    writel(command, &currentDescriptor->m_command);
    readl(&currentDescriptor->m_command);
    
	VMMR_WRITE(VIXS_DMA_DQ_PTR0, g_XC_USBH_info.descriptor_offset);

	while (VMMR_READ(VIXS_DMA_STATUS) & VIXS_DMA_INDIRECT0_MASK);

	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);        

	temp = VMMFB_READ(0x0);
    //DBK("Exit XCIndirectDmaHostToFB\n");
    return count;
}


u32 XCIndirectDmaFBToHost (u32 addrSrcXC, u32 addrDestHost, u32 len)
{
	u32 count = 0, curlen;
	u32 command = 0;	
    PXC_DMADESCRIPTORS currentDescriptor = NULL;
    unsigned long  flags;
   DBK("src:%x dest:%x len:%d\n", addrSrcXC,addrDestHost,len);
    if((len/0x10000) > g_XC_USBH_info.desc_num)
    {
        DBK("XCIndirectDmaHostToFB: len %x exceeds the max DMA size %x\n", len, g_XC_USBH_info.desc_num*0x10000);
        return 0;
    }
	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
        
    //DBK("Enter XCIndirectDmaFBToHost\n");
    while(len > 0)
    {

        currentDescriptor = g_XC_USBH_info.pdescriptor + count;

        if(len > 0x10000)
        {
            curlen = 0x10000;
        }
        else
        {
            curlen = len;
        }

        command = len;
        command |= (VIXS_DMA_NOT_EOL|VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME);

		Vixs_Build_64Bit_Info_To_IO(&addrDestHost, (u32 *)&currentDescriptor->m_dst_addr, sizeof(u32));

		Vixs_Build_64Bit_Info_To_IO(&addrSrcXC, (u32 *)&currentDescriptor->m_src_addr, sizeof(u32));
        writel(command, &currentDescriptor->m_command);
        writel(0, &currentDescriptor->m_control_word);
        count++;	
        len -= curlen;
        
    }

    //point to the last descriptor
    currentDescriptor = g_XC_USBH_info.pdescriptor + (count -1);
    command = readl(&currentDescriptor->m_command);    
    command |= VIXS_DMA_EOL;
    writel(command, &currentDescriptor->m_command);
    readl(&currentDescriptor->m_command);
	VMMR_WRITE(VIXS_DMA_DQ_PTR0, g_XC_USBH_info.descriptor_offset);

	while (VMMR_READ(VIXS_DMA_STATUS) & VIXS_DMA_INDIRECT0_MASK);

	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);        

    //DBK("Exit XCIndirectDmaFBToHost\n");
    return count;
}
#else //CONFIG_XC5_VUSB_SGDMA
u32 XCDmaHostToFB (u32 addrSrcHost, u32 addrDestXC, u32 len)
{
	unsigned long flags;
	u32 i = 0, command = 0, mapp_addr = 0;	
	volatile u32 temp;

	if ((len>0x10000)|| (len==0)) {
		printk("[%s] ERROR: Invalid DMA request size: %x\n",__func__, len);
		return 0;
	}

	//printk("[{%s:%d} H=>F] src:%x => dest:%x  len:%d\n",__func__, __LINE__,addrSrcHost ,addrDestXC,len);	 
	mapp_addr=pci_map_single( g_XC_USBH_info.mps_pcidev,
			phys_to_virt(addrSrcHost),
			len, PCI_DMA_TODEVICE);
	//printk("[%s:%d] mps_pcidev:%p  mapp_addr:%x\n",__func__, __LINE__, g_XC_USBH_info.mps_pcidev,mapp_addr);

	temp = VMMR_READ(VIXS_DMA_STATUS);
	if (temp & VIXS_DMA_ERROR_MASK) {		
		printk("[%s] ERROR: Last operation DMA engine error, status: 0x%x!!\n",__func__,  temp);
		/* only clear self error mask */
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK)
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_DIRECT_B_MASK);
		return 0;
	}	

	/* polling for direct DMA idle */
#ifdef DMA_PROFILE
	t1 = jiffies;
#endif
	/* Here hold the lock, set up the DMA */
	spin_lock_irqsave(&g_xcode5_dmab_lock, flags);
	i = 0;
	while (1) {
		temp = VMMR_READ(VIXS_DMA_STATUS);
		temp &= XC_DMA_STATUS_DIRECT_B_MASK;
		if (temp == 0)
			break;		
		
		/* maximum poll for 60ms */
		if (unlikely(++i > DMA_WAIT_TIME)) {
			spin_unlock_irqrestore(&g_xcode5_dmab_lock, flags);
			printk("[%s ERROR: DMA slot B not idle, status 0x%x!\n", 
				__func__, VMMR_READ(VIXS_DMA_STATUS));			
			return 0;
		}
		udelay(DMA_POLL_INTERVAL);
	}
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms = jiffies_to_msecs(t2 - t1);
	if (ms)
		printk("[%s] INFO: DMA idle wait time %d,\n", __func__, ms);
#endif

	command = len;
	command |= (VIXS_DMA_DST_FRAME | VIXS_DMA_SRC_SYSTEM);
	VMMR_WRITE(VIXS_DMA_DIRECT_LDST_B, addrDestXC);
	VMMR_WRITE(VIXS_DMA_DIRECT_LSRC_B, addrSrcHost);
	VMMR_WRITE(VIXS_DMA_DIRECT_UDST_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_USRC_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_DMACMD_B, command);

	i=0;		
#ifdef DMA_PROFILE
	t1 = jiffies;
#endif
	do {
		udelay(DMA_POLL_INTERVAL);
		temp = VMMR_READ(VIXS_DMA_STATUS);		
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK) {
			printk("[%s] ERROR: DMA slot B error, status: 0x%x", __func__, temp);
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_DIRECT_B_MASK);
			len = 0;
			break;
		}			

		if(unlikely(++i>(MAX_DMA_TIMEOUT + DMA_WAIT_TIME))) {
			printk("[%s] ERROR: DMA slot B polling timeout, cmd: 0x%x src: 0x%x dst: 0x%x status: 0x%x\n", 
					__func__, command, addrSrcHost, addrDestXC, temp);
			len = 0;
			break;
		}
	} while(temp & VIXS_DMA_DIRECT_B_MASK);
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms=jiffies_to_msecs(t2-t1);
	if(ms)
		printk("[%s] INFO: DMA time slice %d ms.\n", __func__, ms);
#endif

	spin_unlock_irqrestore(&g_xcode5_dmab_lock, flags);
	pci_unmap_single(g_XC_USBH_info.mps_pcidev,mapp_addr,len,PCI_DMA_TODEVICE);	
	return len;
}

EXPORT_SYMBOL(XCDmaHostToFB);

u32 XCDmaFBToHost (u32 addrSrcXC, u32 addrDestHost, u32 len)
{
	unsigned long flags;
	u32 i = 0, command = 0, mapp_addr =0;	
	volatile u32 temp;

	if (unlikely((len>0x10000)||(len==0))) 	{
		printk("[%s] ERROR: Invalid DMA request size: %x!\n",__func__, len);
		return 0;
	}

	//printk("[{%s:%d} F=>H] src:%x => dest:%x  len:%d\n",__func__, __LINE__, addrSrcXC ,addrDestHost,len);
	mapp_addr=pci_map_single( g_XC_USBH_info.mps_pcidev,
			phys_to_virt(addrDestHost),
			len, PCI_DMA_FROMDEVICE);
	//printk("[%s:%d] mps_pcidev:%p  mapp_addr:%x\n", __func__, __LINE__, g_XC_USBH_info.mps_pcidev, mapp_addr);

	temp = VMMR_READ(VIXS_DMA_STATUS);
	if (temp & VIXS_DMA_ERROR_MASK) {		
		printk("[%s] ERROR: Last operation DMA engine error, status: 0x%x!!\n",__func__,  temp);
		/* only clear self error mask */
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK)
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_DIRECT_B_MASK);
		return 0;
	}

	/* polling for direct DMA idle */
#ifdef DMA_PROFILE
	t1 = jiffies;
#endif
	/* Here hold the lock, set up the DMA */
	spin_lock_irqsave(&g_xcode5_dmab_lock, flags);
	i = 0;
	while (1) {		
		temp = VMMR_READ(VIXS_DMA_STATUS);
		temp &= XC_DMA_STATUS_DIRECT_B_MASK;
		if (temp == 0)
			break;
		
		/* maximum poll for 60ms */
		if (unlikely(++i > DMA_WAIT_TIME)) {
			spin_unlock_irqrestore(&g_xcode5_dmab_lock, flags);
			printk("[%s ERROR: DMA slot B not idle, status 0x%x!\n", 
				__func__, VMMR_READ(VIXS_DMA_STATUS));			
			return 0;
		}
		udelay(DMA_POLL_INTERVAL);
	}
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms = jiffies_to_msecs(t2 - t1);
	if (ms)
		printk("[%s] INFO: DMA idle wait time %d,\n", __func__, ms);
#endif

	command = len;
	command |= VIXS_DMA_DST_SYSTEM|VIXS_DMA_SRC_FRAME;

	VMMR_WRITE(VIXS_DMA_DIRECT_LDST_B, addrDestHost);
	VMMR_WRITE(VIXS_DMA_DIRECT_LSRC_B, addrSrcXC);
	VMMR_WRITE(VIXS_DMA_DIRECT_UDST_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_USRC_B, 0);
	VMMR_WRITE(VIXS_DMA_DIRECT_DMACMD_B, command);

	/* polling for DMA complete */
	i=0;		
#ifdef DMA_PROFILE	
	t1 = jiffies;
#endif
	do {
		/* DMA max speed 200MBps, around 5 us per 1KB block */
		udelay(DMA_POLL_INTERVAL);
		temp = VMMR_READ(VIXS_DMA_STATUS);		
		if (temp & XC_DMA_STATUS_ERR_DIRECT_B_MASK) {
			printk("[%s] ERROR: Polling DMA slot B error, status: 0x%x",__func__, temp);
			VMMR_WRITE(VIXS_DMA_STATUS, XC_DMA_STATUS_ERR_DIRECT_B_MASK);
			len = 0;
			break;
		}			

		/* MAX DMA tx time is 350us, use 3500 us for timeout limit */
		if(unlikely(++i>(MAX_DMA_TIMEOUT + DMA_WAIT_TIME))) {
			printk("[%s] ERROR: DMA slot B polling timeout, cmd: 0x%x src: 0x%x dst: 0x%x status: 0x%x\n", 
					__func__, command, addrSrcXC, addrDestHost, temp);
			len = 0;
			break;
		}
	} while(temp & VIXS_DMA_DIRECT_B_MASK);
#ifdef DMA_PROFILE
	t2 = jiffies;
	ms=jiffies_to_msecs(t2 - t1);
	if(ms)
		printk("[%s] INFO: DMA time slice %d ms.\n", __func__, ms);
#endif

	spin_unlock_irqrestore(&g_xcode5_dmab_lock, flags);
	pci_unmap_single(g_XC_USBH_info.mps_pcidev,mapp_addr,len,PCI_DMA_FROMDEVICE);
	return len;
}
EXPORT_SYMBOL(XCDmaFBToHost);
#endif //CONFIG_XC5_VUSB_SGDMA

DECLARE_WAIT_QUEUE_HEAD(vusb_ohci_wq);
static int vusb_ohci_wq_flag = 0;

void vusb_ohci_init_wait(void)
{
	wait_event(vusb_ohci_wq, (vusb_ohci_wq_flag != 0));
}
EXPORT_SYMBOL(vusb_ohci_init_wait);

void vusb_ohci_init_wakeup(void)
{
	vusb_ohci_wq_flag=1;
	wake_up(&vusb_ohci_wq);
}
EXPORT_SYMBOL(vusb_ohci_init_wakeup);

extern u32 meminit(u32 mmr) ;

int XC_USBH_Global_Struct_Init()
{
    struct pci_dev *dev = NULL;
    u32 physMemAddr;
    u32 physMemSize;
	u32 count;
	volatile u32 temp;
	unsigned long flags;


	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
    if(g_XC_USBH_Global_Struct_Initialized)
    {
		spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);
        DBK(" XC USBH Global Struct has been initialized already\n");
        return 0;
    }
	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);

	// wait for pcie sata initialized
	while (xc5_pci_dev_initialized < 3) {		
		msleep(100);
	}

    memset(&g_XC_USBH_info, 0, sizeof(g_XC_USBH_info));

    //find XCode PCI device and get FB mem base and REG base
	dev = pci_get_device(XCODE5_PCI_VENDOR_ID, XCODE5_PCI_DEVICE_ID, NULL);
        
    if (NULL == dev)
    {
        DBK("FAILED to find Vendor Id: %Xh and device ID %Xh\n",
				XCODE5_PCI_VENDOR_ID, XCODE5_PCI_DEVICE_ID);
        return -ENODEV;
    }

    g_XC_USBH_info.mps_pcidev = dev;
    
    pci_enable_device(dev);


    //step through and get all the BARs
    for (count = 0; count < TotalBAR; count ++)
    {
        temp = pci_resource_flags(dev, count);

        if (temp & IORESOURCE_IO)
        {
            //ignore
        }
        else if(temp & IORESOURCE_MEM)
        {
            physMemAddr = pci_resource_start (dev, (u32) count);
            temp = pci_resource_end (dev, (u32) count);
            physMemSize = temp - physMemAddr;

            if (physMemSize > 0x10000) //64K
            {
                //MMFB
                g_XC_USBH_info.mp_physfb = (u32*)physMemAddr;
                g_XC_USBH_info.m_fbsize = physMemSize;
                g_XC_USBH_info.mp_mmfb = ioremap_nocache(physMemAddr, physMemSize);

                DBK("BAR%x phys addr 0x%08x lin. addr 0x%08x size 0x%08x\n",
                                                    (u32)count,
                                                    (u32)g_XC_USBH_info.mp_physfb,
                                                    (u32)g_XC_USBH_info.mp_mmfb,
                                                    (u32)g_XC_USBH_info.m_fbsize);
            }
            else
            {
                //MMR
                if(g_XC_USBH_info.mp_physmmr)
                {
                    DBK("MMR is valid already, ignore this bar\n");
                }
                else
                {
                    //MMR
                    g_XC_USBH_info.mp_physmmr = (u32*)physMemAddr;
                    g_XC_USBH_info.m_mmrsize = physMemSize;
                    g_XC_USBH_info.mp_mmr = ioremap_nocache(physMemAddr, physMemSize);
    				
                    DBK("BAR%x phys addr 0x%08x lin. addr 0x%08x size 0x%08x\n",
                                                        (u32)count,
                                                        (u32)g_XC_USBH_info.mp_physmmr,
                                                        (u32)g_XC_USBH_info.mp_mmr,
                                                        (u32)g_XC_USBH_info.m_mmrsize);
                }
            }
        }
    }
   
    spin_lock_init(&vusb_usb_indirect_reg_lock);
    g_XC_USBH_info.m_pci_int_line = dev->irq;

	// Found PCIe device and resource allocated, check the device id for xcode5
	temp = VMMR_READ(XC_RBM_PCI_SUB_CFG);
	if (temp != XCODE5_DEVICE_ID) {
		printk("Can not find valid XCODE5 device, devid=0x%x\n", temp);
		return -1;
    }

#if 0
#ifndef CONFIG_VIRTUAL_XC5_SATA
	// put xcode5 hard blocks into reset and all ios to input mode, wait for xcode5 driver to initialized the hardware
	if ((VMMR_READ(XC_CG_RESET_REG) & XC_CG_RESET_REG_MC_RESET_MASK) == 0) {
		spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
		VMMR_WRITE(XC_CG_RESET_REG, 0xFFFFF9FF);
		VMMR_WRITE(XC_CG_RESET_REG1, 0xFFFFFFFF);
		VMMR_WRITE(XC_RBM_SWAP, 0);

		/* unlock interlock */
		for (i=0; i<8; i++) {
			temp = VMMR_READ(XC_MIPS_INTERLOCK0 + (i<<2));
			if(temp)
				VMMR_WRITE(XC_MIPS_INTERLOCK0 + (i<<2), temp);		
   }

		/*restore GPIO */
		VMMR_WRITE(XC_GPIO_DEDICATED_OUTEN, 0); 
		VMMR_WRITE(XC_GPIO_A_CTRL, XC_GPIO_A_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_A_OE, 0);
		VMMR_WRITE(XC_GPIO_B_CTRL, XC_GPIO_B_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_B_OE, 0);
		VMMR_WRITE(XC_GPIO_C_CTRL, XC_GPIO_C_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_C_OE, 0);
		VMMR_WRITE(XC_GPIO_D_CTRL, XC_GPIO_D_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_D_OE, 0);
		VMMR_WRITE(XC_GPIO_E_CTRL, XC_GPIO_E_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_E_OE, 0);
		VMMR_WRITE(XC_GPIO_F_CTRL, XC_GPIO_F_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_F_OE, 0);
		VMMR_WRITE(XC_GPIO_G_CTRL, XC_GPIO_G_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_G_OE, 0);
		VMMR_WRITE(XC_GPIO_H_CTRL, XC_GPIO_F_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_H_OE, 0);
		VMMR_WRITE(XC_GPIO_I_CTRL, XC_GPIO_I_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_I_OE, 0);
		VMMR_WRITE(XC_GPIO_J_CTRL, XC_GPIO_J_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_J_OE, 0);	
		// GPIO_J is the PCI pins	
		//VMMR_WRITE(XC_GPIO_K_CTRL, XC_GPIO_K_CTRL_DEFAULT_VALUE);
		//VMMR_WRITE(XC_GPIO_K_OE, 0);
		VMMR_WRITE(XC_GPIO_L_CTRL, XC_GPIO_L_CTRL_DEFAULT_VALUE);
		VMMR_WRITE(XC_GPIO_L_OE, 0);
		spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);	

		/* set default board id for xcode5 */
		VMMR_WRITE(XC_CG_DUMMY_REG, 0x0043);

		VMMFB_WRITE(0xb0, 0);
		do {		
			temp = VMMFB_READ(0xb0);		
        msleep(100);
		} while ((temp == 0));
	}
#endif
#endif

    DBK_LOC;
	/* Turn on the USB port power */
	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);	
	/* Turn on the USB port power */
	temp = VMMR_READ(XC_GPIO_DEDICATED_OUT);	
	temp |= XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT5_MASK;
	VMMR_WRITE(XC_GPIO_DEDICATED_OUT, temp);	

	temp = VMMR_READ(XC_GPIO_DEDICATED_OUTEN);
	temp |= XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT5_MASK;
	temp &= ~XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT4_MASK;
	VMMR_WRITE(XC_GPIO_DEDICATED_OUTEN, temp);	
	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);	

	for (count=0; count<10; count++) {
		temp = VMMR_READ(XC_GPIO_DEDICATED_IN);
		if (likely(temp & XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT5_MASK))
			break;

		if (count == 9) {
			printk("XCode5 USB port power not on, gpio stat 0x%x", temp);
			return -1;
		}
		msleep(100);
    }


	for (count=0; count<10; count++) {
		temp = VMMR_READ(XC_GPIO_DEDICATED_IN);
		if (likely(temp & XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT4_MASK)) {
			break;
		} else {
			printk("XCode5 USB port power over current/temperature, disable the port power, state: 0x%x\n!", temp);
			/* Turn off the power */
			spin_lock_irqsave(&g_xcode5_dma0_lock, flags);	
			temp = VMMR_READ(XC_GPIO_DEDICATED_OUT);
			temp &= ~XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT5_MASK;
			VMMR_WRITE(XC_GPIO_DEDICATED_OUT, temp);
			spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);	
			
			/* disable the port, if over the limit */
			if (count == 9) {
				printk("XCode5 USB host port power is off.\n");
				return -1;
			}
			msleep(500);
			
			/* Turn on the power */
			spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
			temp = VMMR_READ(XC_GPIO_DEDICATED_OUT);
			temp |= XC_GPIO_DEDICATED_OUT_GPIO_DEDICATED_OUT5_MASK;
			VMMR_WRITE(XC_GPIO_DEDICATED_OUT, temp);
			spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);	
    }
	}
	
	printk("XC5 USB host port power is on.\n");	

    //Inlitialize memory based structures
    g_XC_USBH_info.m_ehci_periodic_mem_paddr = (u32)EHCI_PERIODIC_MEM_OFFSET;
    g_XC_USBH_info.m_ehci_periodic_mem_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + EHCI_PERIODIC_MEM_OFFSET);

    for(count = 0; count < EHCI_QH_QUEUE_ITEM_NUM; count++)
    {
        g_XC_USBH_info.ms_qh_hw_seg[count].m_paddr = EHCI_QH_HW_SEG_MEM_OFFSET + EHCI_QH_QTD_SEG_DEFAULT_LEN*count;
        g_XC_USBH_info.ms_qh_hw_seg[count].m_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + EHCI_QH_HW_SEG_MEM_OFFSET + EHCI_QH_QTD_SEG_DEFAULT_LEN*count);
        g_XC_USBH_info.ms_qh_hw_seg[count].m_status = 0;
        g_XC_USBH_info.ms_qh_hw_seg[count].mps_next = &g_XC_USBH_info.ms_qh_hw_seg[count+1];
	DBK("g_XC_USBH_info.qh_hw_seg[%d].m_paddr:%p\n",count, (void*)g_XC_USBH_info.ms_qh_hw_seg[count].m_paddr);
	DBK("&qh_hw_seg [%d]:%p\n",count, (void*)&g_XC_USBH_info.ms_qh_hw_seg[count]);
    }
    g_XC_USBH_info.ms_qh_hw_seg[EHCI_QH_QUEUE_ITEM_NUM-1].mps_next = NULL;
    g_XC_USBH_info.ms_qh_pool.mps_next = &g_XC_USBH_info.ms_qh_hw_seg[0];
DBK("&qh_hw_seg [0]:%p g_XC_USBH_info.ms_qh_pool.mps_next:%p\n", &g_XC_USBH_info.ms_qh_hw_seg[0],g_XC_USBH_info.ms_qh_pool.mps_next);

    for(count = 0; count < EHCI_QTD_QUEUE_ITEM_NUM; count++)
    {
        g_XC_USBH_info.ms_qtd_hw_seg[count].m_paddr = EHCI_QTD_HW_SEG_MEM_OFFSET + EHCI_QH_QTD_SEG_DEFAULT_LEN*count;
        g_XC_USBH_info.ms_qtd_hw_seg[count].m_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + EHCI_QTD_HW_SEG_MEM_OFFSET + EHCI_QH_QTD_SEG_DEFAULT_LEN*count);
        g_XC_USBH_info.ms_qtd_hw_seg[count].m_status = 0;
        g_XC_USBH_info.ms_qtd_hw_seg[count].mps_next = &g_XC_USBH_info.ms_qtd_hw_seg[count+1];
        DBK("g_XC_USBH_info.ms_qtd_hw_seg[%d].m_paddr :%x\n",count ,g_XC_USBH_info.ms_qtd_hw_seg[count].m_paddr );
    }
    g_XC_USBH_info.ms_qtd_hw_seg[EHCI_QTD_QUEUE_ITEM_NUM-1].mps_next = NULL;
    g_XC_USBH_info.ms_qtd_pool.mps_next = &g_XC_USBH_info.ms_qtd_hw_seg[0];        
  
    //Inlitialize memory based structures
    g_XC_USBH_info.m_ohci_hcca_mem_paddr = /*0xC0000000 + */OHCI_HCCA_MEM_OFFSET;
    g_XC_USBH_info.m_ohci_hcca_mem_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + OHCI_HCCA_MEM_OFFSET);

    for(count = 0; count < OHCI_ED_QUEUE_ITEM_NUM; count++)
    {
        g_XC_USBH_info.ms_ed_hw_seg[count].m_paddr = /*0xC0000000 + */OHCI_ED_HW_SEG_MEM_OFFSET + OHCI_ED_TD_SEG_DEFAULT_LEN*count;
        g_XC_USBH_info.ms_ed_hw_seg[count].m_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + OHCI_ED_HW_SEG_MEM_OFFSET + OHCI_ED_TD_SEG_DEFAULT_LEN*count);
        g_XC_USBH_info.ms_ed_hw_seg[count].m_status = 0;
        g_XC_USBH_info.ms_ed_hw_seg[count].mps_next = &g_XC_USBH_info.ms_ed_hw_seg[count+1];
        DBK("g_XC_USBH_info.ms_ed_hw_seg[%d].m_paddr :%x\n",count ,g_XC_USBH_info.ms_ed_hw_seg[count].m_paddr );
    }
    g_XC_USBH_info.ms_ed_hw_seg[OHCI_ED_QUEUE_ITEM_NUM-1].mps_next = NULL;
    g_XC_USBH_info.ms_ed_pool.mps_next = &g_XC_USBH_info.ms_ed_hw_seg[0];

    for(count = 0; count < OHCI_TD_QUEUE_ITEM_NUM; count++)
    {
        g_XC_USBH_info.ms_td_hw_seg[count].m_paddr = /*0xC0000000 + */OHCI_TD_HW_SEG_MEM_OFFSET + OHCI_ED_TD_SEG_DEFAULT_LEN*count;
        g_XC_USBH_info.ms_td_hw_seg[count].m_vaddr = (u32)(g_XC_USBH_info.mp_mmfb + OHCI_TD_HW_SEG_MEM_OFFSET + OHCI_ED_TD_SEG_DEFAULT_LEN*count);
        g_XC_USBH_info.ms_td_hw_seg[count].m_status = 0;
        g_XC_USBH_info.ms_td_hw_seg[count].mps_next = &g_XC_USBH_info.ms_td_hw_seg[count+1];
        DBK("g_XC_USBH_info.ms_td_hw_seg[%d].m_paddr :%x\n",count ,g_XC_USBH_info.ms_td_hw_seg[count].m_paddr );
    }
    g_XC_USBH_info.ms_td_hw_seg[OHCI_TD_QUEUE_ITEM_NUM-1].mps_next = NULL;
    g_XC_USBH_info.ms_td_pool.mps_next = &g_XC_USBH_info.ms_td_hw_seg[0];        


    for(count = 0; count < USBH_DATA_BUF_SEG_NUM; count++)
    {
        g_XC_USBH_info.ms_data_buf[count].m_paddr = USBH_DATA_BUF_OFFSET + USBH_DATA_BUF_SEG_SIZE * count; /* last 4MB on CH0 */
        g_XC_USBH_info.ms_data_buf[count].m_status = 0;
        g_XC_USBH_info.ms_data_buf[count].mps_next = &g_XC_USBH_info.ms_data_buf[count+1];
        DBK("g_XC_USBH_info.m_paddr :%x\n",g_XC_USBH_info.ms_data_buf[count].m_paddr );
    }
    g_XC_USBH_info.ms_data_buf[USBH_DATA_BUF_SEG_NUM-1].mps_next = NULL;
    g_XC_USBH_info.ms_data_buf_pool.mps_next = &g_XC_USBH_info.ms_data_buf[0];        
    DBK("g_XC_USBH_info.ms_data_buf_pool.mps_next :%p\n",g_XC_USBH_info.ms_data_buf_pool.mps_next );

#ifdef CONFIG_XC5_VUSB_SGDMA
    g_XC_USBH_info.descriptor_offset = XC_USBH_INDIRECT_DMA_DESC_OFFSET;
    g_XC_USBH_info.desc_num = XC_USBH_INDIRECT_DMA_DESC_SIZE/sizeof(s_dma_desc);
    g_XC_USBH_info.pdescriptor = (struct _XC_DMADESCRIPTORS *)(g_XC_USBH_info.mp_mmfb + XC_USBH_INDIRECT_DMA_DESC_OFFSET);
#endif

//    g_XC_USBH_info.m_dma_mask = (u64)0x80000000; //2G, shoudl be enough
    g_XC_USBH_info.m_dma_mask = DMA_BIT_MASK(32);

	spin_lock_irqsave(&g_xcode5_dma0_lock, flags);
    g_XC_USBH_Global_Struct_Initialized = 1;
	spin_unlock_irqrestore(&g_xcode5_dma0_lock, flags);
	
#if 0
    printk("[%s:%d] EHCI_PERIODIC_MEM_OFFSET:%x\n",__func__,__LINE__,EHCI_PERIODIC_MEM_OFFSET);
    printk("g_XC_USBH_info.ms_qh_hw_seg[0].m_paddr:%x\n", g_XC_USBH_info.ms_qh_hw_seg[0].m_paddr);
     printk("g_XC_USBH_info.ms_qtd_hw_seg[0].m_paddr:%x\n",g_XC_USBH_info.ms_qtd_hw_seg[0].m_paddr );
     printk("g_XC_USBH_info.ms_ed_hw_seg[0].m_paddr:%x\n",g_XC_USBH_info.ms_ed_hw_seg[0].m_paddr);
     printk("g_XC_USBH_info.ms_td_hw_seg[count].m_paddr :%x :%x\n",g_XC_USBH_info.ms_td_hw_seg[0].m_paddr,  g_XC_USBH_info.ms_td_hw_seg[OHCI_TD_QUEUE_ITEM_NUM-1].m_paddr);
     printk("g_XC_USBH_info.data_buf[0].m_paddr :%x\n",g_XC_USBH_info.ms_data_buf[0].m_paddr);
     printk("size :%x \n", g_XC_USBH_info.ms_td_hw_seg[OHCI_TD_QUEUE_ITEM_NUM-1].m_paddr - EHCI_PERIODIC_MEM_OFFSET);
    printk("g_XC_USBH_info.data_buf[USBH_DATA_BUF_SEG_NUM-1].m_paddr :%x\n",g_XC_USBH_info.ms_data_buf[USBH_DATA_BUF_SEG_NUM-1].m_paddr );
#endif
    return 0;
}


EXPORT_SYMBOL(XC_USBH_Global_Struct_Init);

#endif

