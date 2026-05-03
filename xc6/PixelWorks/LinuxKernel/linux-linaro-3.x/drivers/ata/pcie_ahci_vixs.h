#ifndef __PCIE_AHCI_VIXS_H__
#define __PCIE_AHCI_VIXS_H__

#include <linux/mempool.h>
#include <linux/proc_fs.h>
#include <plat/xcode5_reg.h>

//#define XC5_VSATA_DEBUG
#ifdef  XC5_VSATA_DEBUG
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/time.h>
static inline void printk_time(void);
#define DBK(fmt, args...) printk("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBK_  printk
#define DBK_LOC printk("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBK_LOC_T printk_time();printk("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBKT(fmt, args...) printk_time();printk("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
static inline void printk_time(void)
{
        struct timespec tv;
        getnstimeofday(&tv);
}

static inline void dump_packet(unsigned char* buf, int size)
{
    int i,j;
    int row = size/16;
    int col = size%16;
    unsigned char* cur=buf;
    if(size < 16){
        for(i=0;i<size;i++){
                printk("%x ",*cur);
                cur++;
        }

        printk("\n");
        }
    else{
    for (i=0;i<row;i++)
    {
        j = (i==row-1)?col:16;
        printk("%2x|",i<<4);
        for(j=0;j<16;j++)
        {
             printk("%x ",*cur);
             cur++;
        }
        printk("\n");
    }}
}

#else
#include <stdio.h>
#include <sys/time.h>
static inline void printf_time(void);
#define DBF(fmt, args...) printf("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBF_LOC printf("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBFT(fmt, args...) printf_time();printf("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBF_LOC_T       printf_time();printf("[%s:%d]\n",__FUNCTION__,__LINE__)


static inline void printf_time(void)
{
        struct timeval start;
        gettimeofday(&start, NULL);
        printf("%03ds %03dms %03dus",(int) start.tv_sec % 1000,(int) start.tv_usec/1000,(int)start.tv_usec);
}
#endif //__KERNEL__
#define DBK(fmt, args...) printk(KERN_DEFAULT "[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBK_(fmt, args...) printk(KERN_DEFAULT fmt,## args)
#define DBK_LOC printk(KERN_DEFAULT "[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBK_LOC_T printk_time();printk(KERN_DEFAULT "[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBKT(fmt, args...) printk_time();printk(KERN_DEFAULT "[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#else
#define DBK(fmt, args...) 
#define DBK_(fmt, args...)
#define DBK_LOC 
#define DBK_LOC_T 
#define DBKT(fmt, args...) 
static inline void dump_packet(unsigned char* buf, int size)
{
}
#endif //XC5_VSATA_DEBUG

static inline void dump_packet_err(unsigned char* buf, int size)
{
    int i,j;
    int row = size/16;
    int col = size%16;
    unsigned char* cur=buf;
    if(size < 16){
        for(i=0;i<size;i++){
                printk("%x ",*cur);
                cur++;
        }

        printk("\n");
        }
    else{
    for (i=0;i<row;i++)
    {
        j = (i==row-1)?col:16;
        printk("%2x|",i<<4);
        for(j=0;j<16;j++)
        {
             printk("%x ",*cur);
             cur++;
        }
        printk("\n");
    }}
}





//#define VIXS_PCI_AHCI_DRIVER

#define		BAR0				0				//IO Mapped Registers
#define		BAR1				1+BAR0			//Memory Mapped Registers
#define		BAR2				1+BAR1			//Frame Buffer
#define		BAR3				1+BAR2			//Frame Buffer
#define		TotalBAR			10//+BAR3


#define XCODE5_PCI_VENDOR_ID	0x1745
#define XCODE5_PCI_DEVICE_ID	0x5000
#define XCODE5_DEVICE_ID		0x50001745
			

#define	VIXS_CG_RESET_REG 0x0000
    #define VIXS_CG_RESET_REG_DMA_RESET_MASK                          0x00000002

#define	VIXS_DMA_DQ_PTR0 0x0C00

#define VIXS_DMA_DIRECT_LDST_B 0x0CA8
#define VIXS_DMA_DIRECT_LSRC_B 0x0CA0
#define VIXS_DMA_DIRECT_UDST_B 0x0CAC
#define VIXS_DMA_DIRECT_USRC_B 0x0CA4
#define  VIXS_DMA_DIRECT_DMACMD_B 0x0CB0
#define  VIXS_DMA_DIRECT_DMACW_B 0x0CB4


#define VIXS_DMA_STATUS     0x0C34
    #define VIXS_DMA_DIRECT_A_MASK  0x4
    #define VIXS_DMA_DIRECT_B_MASK  0x8
    #define VIXS_DMA_INDIRECT0_MASK 0x00000001
     #define VIXS_DMA_INDIRECT1_MASK 0x00000002 

#define VIXS_DMA_ERROR_MASK 		(XC_DMA_STATUS_ERR_INDIRECT0_MASK |\
									 XC_DMA_STATUS_ERR_INDIRECT1_MASK |\
									 XC_DMA_STATUS_ERR_DIRECT_A_MASK  |\
									 XC_DMA_STATUS_ERR_DIRECT_B_MASK)
									 
#define VIXS_DMA_CURRENT_MASK		(XC_DMA_STATUS_INDIRECT0_CURRENT_MASK |\
									 XC_DMA_STATUS_INDIRECT1_CURRENT_MASK |\
									 XC_DMA_STATUS_DIRECT_A_CURRENT_MASK  |\
									 XC_DMA_STATUS_DIRECT_B_CURRENT_MASK)

#define	VIXS_DMA_HOST_INT_STATUS 0x0C50
    #define VIXS_DMA_HOST_INT_STATUS_XFER_DONE0_MASK                  0x00000001
    #define VIXS_DMA_HOST_INT_STATUS_XFER_DONEA_MASK                  0x00000004
    #define VIXS_DMA_HOST_INT_STATUS_XFER_DONEB_MASK                  0x00000008

#define	VIXS_DMA_HOST_INT_MASK 0x0C54
    #define VIXS_DMA_HOST_INT_MASK_XFER_DONE0_MASK             0x00000001
    #define VIXS_DMA_HOST_INT_MASK_XFER_DONEA_MASK             0x00000004
    #define VIXS_DMA_HOST_INT_MASK_XFER_DONEB_MASK             0x00000008

#define	VIXS_DMA_MIPS_INT_STATUS 0x0C58
    #define VIXS_DMA_MIPS_INT_STATUS_XFER_DONE0_MASK                  0x00000001
    #define VIXS_DMA_MIPS_INT_STATUS_XFER_DONEA_MASK                  0x00000004
    #define VIXS_DMA_MIPS_INT_STATUS_XFER_DONEB_MASK                  0x00000008

#define	VIXS_DMA_MIPS_INT_MASK 0x0C5C
    #define VIXS_DMA_MIPS_INT_MASK_XFER_DONE0_MASK             0x00000001
    #define VIXS_DMA_MIPS_INT_MASK_XFER_DONEA_MASK             0x00000004
    #define VIXS_DMA_MIPS_INT_MASK_XFER_DONEB_MASK             0x00000008

#define	VIXS_MIPS4_INTERRUPT 0x04D8
#define	VIXS_MIPS4_INTERRUPT_MASK 0x04DC
    #define VIXS_MIPS4_INTERRUPT_DMA_INT_MASK                         0x00000100
#define	VIXS_HOST_INTERRUPT 0x0414
#define	VIXS_HOST_INTERRUPT_MASK 0x0418
    #define VIXS_HOST_INTERRUPT_DMA_INT_MASK                          0x00000100


#define VIXS_DMA_MAX_TRANSFER_SIZE	0x10000


#define VIXS_DMA_NOT_EOL            (0 << 31)
#define VIXS_DMA_EOL                (1 << 31)
#define	VIXS_DMA_INTMIPS			(1 << 29)
#define VIXS_DMA_INTHOST			(1 << 28)
#define VIXS_DMA_SRC_FRAME			(1 << 24)
#define VIXS_DMA_SRC_SYSTEM			(0 << 24)
#define VIXS_DMA_DST_FRAME			(1 << 25)
#define VIXS_DMA_DST_SYSTEM			(0 << 25)
#define VIXS_DMA_SWAPCTRL_32_BIT        (2<<26)

#define XC4_AHCI_MEM_OFFSET    VSATA_OFFSET   //need 4Kbytes
#define XC4_AHCI_MEM_OFFSET_1  0x4000       //need 4Kbytes



//#define XC4_AHCI_DATA_BUF_OFFSET            (0x10000000)       //4M for 64 * 64K data buffer
//#define XC4_AHCI_DATA_BUF_OFFSET            (0x1FC00000 + 64*0x10000)       //4M for 64 * 64K data buffer
#define XC4_AHCI_DATA_BUF_OFFSET            (0x1D400000 )       //4M for 64 * 64K data buffer
#define XC4_AHCI_DATA_BUF_SEG_NUM         (2048)
#define XC4_AHCI_DATA_BUF_SEG_SIZE         (PAGE_SIZE)    //(blkdev.h)  MAX_SEGMENT_SIZE	65536


#define XC4_AHCI_DATA2_BUF_OFFSET           (XC4_AHCI_DATA_BUF_OFFSET + XC4_AHCI_DATA_BUF_SEG_NUM*XC4_AHCI_DATA_BUF_SEG_SIZE)     //4M for 64 * 64K data buffer
#define XC4_AHCI_DATA2_BUF_SEG_NUM         (256)
#define XC4_AHCI_DATA2_BUF_SEG_SIZE         (4*PAGE_SIZE)    //(blkdev.h)  MAX_SEGMENT_SIZE	65536


//#define XC4_AHCI_DATA_BUF_SEG_SIZE         (0x10000)    //(blkdev.h)  MAX_SEGMENT_SIZE	65536

#define XC4_AHCI_INDIRECT_DMA_DESC_OFFSET   XC4_AHCI_MEM_OFFSET + VSATA_CMD_SIZE 
#define XC4_AHCI_INDIRECT_DMA_DESC_SIZE        32*128


typedef struct _XC4_AHCI_data_buf
{
    struct _XC4_AHCI_data_buf* next;

    u32 status;     //0: free, 1: occupied
    u32 data_buf_paddr;   //physical address
    
}XC4_AHCI_data_buf;



#define xc4_ahci_mem_writel(data, addr) do { writel(__le32_to_cpu(data), addr);readl(addr);}while(0);
#define xc4_ahci_mem_readl(addr) __raw_readl(addr)


extern void* VIXS_VSATA_REG_BASE;
extern u32 VIXS_VSATA_writel(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 value);
extern u32 VIXS_VSATA_readl(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_VSATA_writeb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 value);
extern u8 VIXS_VSATA_readb(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_VSATA_writew(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16 value);
extern u16 VIXS_VSATA_readw(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_VSATA_writesl(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32*pbuf, u32 count);
extern u32 VIXS_VSATA_readsl(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 *pbuf, u32 count);
extern u32 VIXS_VSATA_writesb(void * SATAC_reg_base, void __iomem * sata_reg_addr, u8* pbuf , u32 count);
extern u32 VIXS_VSATA_readsb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 *pbuf, u32 count);
extern u32 VIXS_VSATA_writesw(void * SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf , u32 count);
extern u32 VIXS_VSATA_readsw(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf, u32 count);




#define VIXS_VSATA_reg_readb(a,b)	    VIXS_VSATA_readb(VIXS_VSATA_REG_BASE, a)
#define VIXS_VSATA_reg_readw(a,b)	    VIXS_VSATA_readw(VIXS_VSATA_REG_BASE, a)
#define VIXS_VSATA_reg_readl(a,b)           VIXS_VSATA_readl(VIXS_VSATA_REG_BASE, a)
#define VIXS_VSATA_reg_writeb(a,b,c)	    VIXS_VSATA_writeb(VIXS_VSATA_REG_BASE, b,a)
#define VIXS_VSATA_reg_writew(a,b,c)	    VIXS_VSATA_writew(VIXS_VSATA_REG_BASE, b,a)
#define VIXS_VSATA_reg_writel(a,b,c)	    VIXS_VSATA_writel(VIXS_VSATA_REG_BASE, b,a)

extern u32 XC4_AHCI_data_buf_allocate(u32 len);
extern void XC4_AHCI_data_buf_free(u32 paddr,u32 len);


//Direct DMA functions

typedef unsigned int (*VixsDMAFBToHost_func_t) (u32 addrSrcXC4, u32 addrDestHost, u32 len);
typedef unsigned int (*VixsDMAHostToFB_func_t) (u32 addrSrcHost, u32 addrDestXC4, u32 len);
typedef u32 (* VixsDataBufAlloc_func_t)(u32 len);
typedef void (* VixsDataBufFree_func_t)(u32 paddr,u32 len);


//Indirect DMA functions
typedef unsigned int (*vixsIndirectDMA_func_t)(unsigned int cmd, struct scatterlist *srcSG, struct scatterlist *destSG, unsigned int nElem);


typedef struct _VIXSDMADESCRIPTORS
{
    unsigned long long				src_addr;
    unsigned long long				dst_addr;
    unsigned int		command;
    unsigned int		control_word;
    unsigned long long				reserved;
} __attribute__ ((packed))  VIXSDMADESCRIPTORS, *PVIXSDMADESCRIPTORS;

#define CMD_DMA_HOST_TO_FB	0xda01
#define CMD_DMA_FB_TO_HOST	0xda02


typedef struct s_wait_queue_wrap_
{
    wait_queue_head_t ms_dma_queue;           
    unsigned int m_dma_flag;                  
}   s_wait_queue_wrap, *ps_wait_queue_wrap;

typedef struct {
    struct pci_dev*      mps_ppcidev;
    u32                  m_pci_int_line;
	__iomem u8 			*mp_pmmr;
	__iomem u8 			*mp_pmmfb;
    u32 *                mp_physfb;
    u32                   m_fbsize;
    u32 *                mp_physmmr;
    u32                  m_mmrsize;

    
    u64         m_dma_mask;

    u32         m_ahci_mem_phy;           //used by AHCI controller 
    u32         m_ahci_mem_virt;

    XC4_AHCI_data_buf   ms_data_buf[XC4_AHCI_DATA_BUF_SEG_NUM];
    XC4_AHCI_data_buf ms_data_buf_pool; 
    u32     m_data_buf_free_num;
    
    XC4_AHCI_data_buf   ms_data2_buf[XC4_AHCI_DATA_BUF_SEG_NUM];
    XC4_AHCI_data_buf ms_data2_buf_pool; 
    u32     m_data2_buf_free_num;

    
    mempool_t * mps_sg_mempool;
    u32   m_max_entry_num_per_sg;
    u32   m_max_sg_num_allocated;
    u32   m_sg_num_allocated;

    u32 m_next_host_index;    //Since XCode may have multiple AHCI hosts, it is used to determine which host it is.
    VixsDataBufAlloc_func_t mps_data_buf_alloc_func;
    VixsDataBufFree_func_t mps_data_buf_free_func;
    
#ifdef CONFIG_XC5_VSATA_SGDMA    
    PVIXSDMADESCRIPTORS mps_descriptor;
    unsigned int	m_desc_num;
    unsigned int m_descriptor_offset; //descriptor physical address in FB
    vixsIndirectDMA_func_t m_indirect_dma_func;
#else
    VixsDMAFBToHost_func_t mps_dma_fb_to_host_func;
    VixsDMAHostToFB_func_t mps_dma_host_to_fb_func;
#endif
    unsigned long*  mp_flags;
    
//    struct proc_dir_entry * root_proc_dir;    
} XC4_AHCI_priv_struct;

extern XC4_AHCI_priv_struct g_XC4_AHCI_info;
int __init pcie_ahci_init(void);

extern void* VIXS_SATA_REG_BASE;
extern u32 VIXS_SATA_writel(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 value);
extern u32 VIXS_SATA_readl(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writeb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 value);
extern u8 VIXS_SATA_readb(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writew(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16 value);
extern u16 VIXS_SATA_readw(void* SATAC_reg_base, void __iomem * sata_reg_addr);
extern u32 VIXS_SATA_writesl(void * SATAC_reg_base, void __iomem * sata_reg_addr, u32*pbuf, u32 count);
extern u32 VIXS_SATA_readsl(void* SATAC_reg_base, void __iomem * sata_reg_addr, u32 *pbuf, u32 count);
extern u32 VIXS_SATA_writesb(void * SATAC_reg_base, void __iomem * sata_reg_addr, u8* pbuf , u32 count);
extern u32 VIXS_SATA_readsb(void* SATAC_reg_base, void __iomem * sata_reg_addr, u8 *pbuf, u32 count);
extern u32 VIXS_SATA_writesw(void * SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf , u32 count);
extern u32 VIXS_SATA_readsw(void* SATAC_reg_base, void __iomem * sata_reg_addr, u16* pbuf, u32 count);




#define VIXS_SATA_reg_readb(a,b)	    VIXS_SATA_readb(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_readw(a,b)	    VIXS_SATA_readw(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_readl(a,b)	           VIXS_SATA_readl(VIXS_SATA_REG_BASE, a)
#define VIXS_SATA_reg_writeb(a,b,c)	    VIXS_SATA_writeb(VIXS_SATA_REG_BASE, b,a)
#define VIXS_SATA_reg_writew(a,b,c)	    VIXS_SATA_writew(VIXS_SATA_REG_BASE, b,a)
#define VIXS_SATA_reg_writel(a,b,c)	    VIXS_SATA_writel(VIXS_SATA_REG_BASE, b,a)


#define VIXS_SATA_IRQ   XCODE6_IRQ_SATA

#define VIXS_SATA_HOST_SEL_MASK     0x80000000              // bit 31 to select if this is host 0 or host1

#endif


