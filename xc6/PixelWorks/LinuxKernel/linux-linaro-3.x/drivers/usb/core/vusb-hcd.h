
#ifndef _VUSB_HCD_H
#define _VUSB_HCD_H
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <plat/xcode5_reg.h>

//#define XC5_VUSB_DEBUG
//#define XC5_OHCI_ONLY
#ifdef  XC5_VUSB_DEBUG
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
#endif // __KERNEL__

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

#endif //XC5_VUSB_DEBUG


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

/* The XC5 register and memory access Marco*/
#define VMMR_READ(reg)           readl( (volatile u32*)((u32)g_XC_USBH_info.mp_mmr + reg) )
#define VMMR_WRITE(reg, data)    writel( (data), (volatile u32*)((u32)g_XC_USBH_info.mp_mmr + reg) )
#define VMMFB_READ(addr)         readl( (volatile u8*)((u32)g_XC_USBH_info.mp_mmfb + addr) )
#define VMMFB_WRITE(addr, data)	 writel( (data), (volatile u8*)((u32)g_XC_USBH_info.mp_mmfb + addr) )

//********************************************************
//VUSB register definition
//********************************************************
//#define VUSB_REG_BASE	0xd0002000      //Not used in VUSB driver
#define VUSB_REG_SIZE	0x40
   

#define VUSB_IAS_REG	0
#define VUSB_EOC_REG	4
#define VUSB_OOC_REG	0xc
#define VUSB_WDATA_REG	0x10
#define VUSB_RDATA_REG		0x14
#define VUSB_OTC_REG	0x18
#define VUSB_STATUS_REG	0x1c
#define VUSB_CONTROL_REG	0x20
#define VUSB_PHY_CONTROL_REG	0x30
#define VUSB_HOST_INT_MASK_REG	0x34
#define VUSB_MIPS_INT_MASK_REG	0x38



//reserved area: 0x1F0C0 - 0x3F0C0
#define EHCI_QH_QUEUE_ITEM_NUM  8  
#define EHCI_QTD_QUEUE_ITEM_NUM 64 
#define EHCI_QH_QTD_SEG_DEFAULT_LEN 0x100        //128 is enough for QH/QTD hw seg

#define OHCI_ED_QUEUE_ITEM_NUM  8 //64
#define OHCI_TD_QUEUE_ITEM_NUM  64//128
#define OHCI_ED_TD_SEG_DEFAULT_LEN 0x80        //64 is enough for ED/TD hw seg

#ifdef CONFIG_XC5_VUSB_SGDMA
#define XC_USBH_INDIRECT_DMA_DESC_OFFSET   (VSATA_OFFSET+ VSATA_TOTAL_SIZE) //0x02000 - 0x03fff (VETH) 0x22000 - 0x31fff (VCOMM)
#define XC_USBH_INDIRECT_DMA_DESC_SIZE  0x1000
#else
#define XC_USBH_INDIRECT_DMA_DESC_OFFSET    (VSATA_OFFSET+ VSATA_TOTAL_SIZE) //0x02000 - 0x03fff (VETH) 0x22000 - 0x31fff (VCOMM)
#define XC_USBH_INDIRECT_DMA_DESC_SIZE  0x0
#endif
#define EHCI_PERIODIC_MEM_OFFSET   (XC_USBH_INDIRECT_DMA_DESC_OFFSET +  XC_USBH_INDIRECT_DMA_DESC_SIZE)      //need 4Kbytes, assume 64KB should be enough 4K + 16K + 32K + 256B + 8K
//#define EHCI_PERIODIC_MEM_OFFSET   0x4000
#define EHCI_QH_HW_SEG_MEM_OFFSET   (EHCI_PERIODIC_MEM_OFFSET + 0x1000)   //need EHCI_QH_QUEUE_ITEM_NUM*EHCI_QH_QTD_SEG_DEFAULT_LEN
#define EHCI_QTD_HW_SEG_MEM_OFFSET  (EHCI_QH_HW_SEG_MEM_OFFSET + EHCI_QH_QUEUE_ITEM_NUM*EHCI_QH_QTD_SEG_DEFAULT_LEN)   ////need EHCI_QTD_QUEUE_ITEM_NUM*EHCI_QH_QTD_SEG_DEFAULT_LEN


#define OHCI_HCCA_MEM_OFFSET    (EHCI_QTD_HW_SEG_MEM_OFFSET + EHCI_QTD_QUEUE_ITEM_NUM*EHCI_QH_QTD_SEG_DEFAULT_LEN)
#define OHCI_ED_HW_SEG_MEM_OFFSET (OHCI_HCCA_MEM_OFFSET + 256)

#define OHCI_TD_HW_SEG_MEM_OFFSET (OHCI_ED_HW_SEG_MEM_OFFSET + OHCI_ED_QUEUE_ITEM_NUM*OHCI_ED_TD_SEG_DEFAULT_LEN)

#if 1
#define USBH_DATA_BUF_OFFSET   (0x1FC00000)       //4M for 64 * 64K data buffer
#define USBH_DATA_BUF_SEG_NUM         (64)
#else
#define USBH_DATA_BUF_OFFSET   (OHCI_TD_HW_SEG_MEM_OFFSET+0x0)       //4M for 64 * 64K data buffer
#define USBH_DATA_BUF_SEG_NUM         (32)
#endif

#define USBH_DATA_BUF_SEG_SIZE      (0x10000)    //(blkdev.h)  MAX_SEGMENT_SIZE	65536



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
#define VIXS_DMA_ERROR_MASK 		(XC_DMA_STATUS_ERR_INDIRECT0_MASK |\
									 XC_DMA_STATUS_ERR_INDIRECT1_MASK |\
									 XC_DMA_STATUS_ERR_DIRECT_A_MASK  |\
									 XC_DMA_STATUS_ERR_DIRECT_B_MASK)

#define VIXS_DMA_CURRENT_MASK		(XC_DMA_STATUS_INDIRECT0_CURRENT_MASK |\
									 XC_DMA_STATUS_INDIRECT1_CURRENT_MASK |\
									 XC_DMA_STATUS_DIRECT_A_CURRENT_MASK  |\
									 XC_DMA_STATUS_DIRECT_B_CURRENT_MASK)

#define VIXS_DMA_NOT_EOL            (0 << 31)
#define VIXS_DMA_EOL                (1 << 31)
#define	VIXS_DMA_INTMIPS			(1 << 29)
#define VIXS_DMA_INTHOST			(1 << 28)
#define VIXS_DMA_SRC_FRAME			(1 << 24)
#define VIXS_DMA_SRC_SYSTEM			(0 << 24)
#define VIXS_DMA_DST_FRAME			(1 << 25)
#define VIXS_DMA_DST_SYSTEM			(0 << 25)
#define VIXS_DMA_SWAPCTRL_32_BIT        (2<<26)

#define XCODE5_PCI_VENDOR_ID    0x1745
#define     XCODE5_PCI_DEVICE_ID    0x5000
#define XCODE5_DEVICE_ID		0x50001745
#define	TotalBAR			6

#define VUSB_CG_RESET_REG_DMA_RESET_MASK 0x00000002
#define VUSB_CG_RESET_REG_RSA_RESET_MASK 0x00000400
#define VUSB_CG_RESET_REG 0x0
#define VUSB_CG_CLK_STOP1 0x12C
#define VUSB_CG_CLK_STOP1_DMACLK_STOP_MASK 0x00000010
#define VUSB_CG_CLK_STOP3 0x13C
#define VUSB_CG_BLK_CLK_STOP3_DMA_DMACLK_STOP_MASK 0x00000001
#define VUSB_CG_CLK_STOP2 0x138
#define VUSB_CG_BLK_CLK_STOP2_DMA_MCLK_STOP_MASK 0x00000001

//Used by EHCI
typedef struct s_qtd_hw_seg{
    struct s_qtd_hw_seg* mps_next;
    u32                  m_status;  //0: free, 1: occupied
    u32                  m_paddr;   //physical address
    u32                  m_vaddr;   //virtual address    
}s_qtd_hw_seg;

typedef struct s_qh_hw_seg{
    struct s_qh_hw_seg* mps_next;
    u32                 m_status;  //0: free, 1: occupied
    u32                 m_paddr;   //physical address
    u32                 m_vaddr;   //virtual address    
}s_qh_hw_seg;

//usded by OHCI 
typedef struct s_ed_hw_seg{
    struct s_ed_hw_seg* mps_next;
    u32                 m_status;  //0: free, 1: occupied
    u32                 m_paddr;   //physical address
    u32                 m_vaddr;   //virtual address
}s_ed_hw_seg;

typedef struct s_td_hw_seg{
    struct s_td_hw_seg* mps_next;
    u32                 m_status;  //0: free, 1: occupied
    u32                 m_paddr;   //physical address
    u32                 m_vaddr;   //virtual address
}s_td_hw_seg;

typedef struct s_data_buf
{
    struct s_data_buf* mps_next;
    u32                m_status;     //0: free, 1: occupied
    u32                m_paddr;   //physcial address
    
}s_data_buf;


typedef struct s_dma_desc
{
    unsigned long long		m_src_addr;
    unsigned long long		m_dst_addr;
    unsigned int		m_command;
    unsigned int		m_control_word;
    unsigned long long		m_reserved;
}__attribute__ ((packed))   s_dma_desc;

typedef struct _XC_DMADESCRIPTORS
{
    unsigned long long                          m_src_addr;
    unsigned long long                          m_dst_addr;
    unsigned int                m_command;
    unsigned int                m_control_word;
    unsigned long long                          m_reserved;
}__attribute__ ((packed))   XC_DMADESCRIPTORS, *PXC_DMADESCRIPTORS;

typedef struct {
    struct pci_dev*      mps_pcidev;
    u32                  m_pci_int_line;
	__iomem u8 			*mp_mmr;
	__iomem u8 			*mp_mmfb;
    u32*                 mp_physfb;
    u32                  m_fbsize;
    u32*                 mp_physmmr;
    u32                  m_mmrsize;
    //4K
    u32                  m_ehci_periodic_mem_paddr;
    u32                  m_ehci_periodic_mem_vaddr;
    //256 bytes
    u32         m_ohci_hcca_mem_paddr;        
    u32         m_ohci_hcca_mem_vaddr;
    u64         m_dma_mask;
    s_qtd_hw_seg ms_qtd_hw_seg[EHCI_QTD_QUEUE_ITEM_NUM];
    s_qtd_hw_seg ms_qtd_pool;
    s_qh_hw_seg  ms_qh_hw_seg[EHCI_QH_QUEUE_ITEM_NUM];
    s_qh_hw_seg ms_qh_pool;
    s_ed_hw_seg ms_ed_hw_seg[OHCI_ED_QUEUE_ITEM_NUM];
    s_ed_hw_seg ms_ed_pool;
    s_td_hw_seg ms_td_hw_seg[OHCI_TD_QUEUE_ITEM_NUM];
    s_td_hw_seg ms_td_pool;
    s_data_buf   ms_data_buf[USBH_DATA_BUF_SEG_NUM];
    s_data_buf ms_data_buf_pool;    
#ifdef CONFIG_XC5_VUSB_SGDMA
    //Indirect DMA resources and info 
    PXC_DMADESCRIPTORS pdescriptor;
    unsigned int desc_num;
    unsigned int descriptor_offset; //descriptor physical address in FB
#endif        
    struct usb_hcd *mps_hcd;
    struct usb_hcd *mps_ehci_hcd;
    struct ehci_hcd *mps_ehci;
} XC_USBH_priv_struct;




typedef enum {
	VUSB_EHCI_REG = 0,
	VUSB_OHCI_REG = 1,
} e_vusb_hc_reg_type;

extern u32 VUSB_HCD_REG_BASE;
extern u32 VUSB_HCD_REF_CNT;
extern u32 VUSB_HCD_ACTIVE_CNT;
extern u32 vusb_writel(u32 VUSB_reg_base, u32 ehci_reg_addr, u32 value, e_vusb_hc_reg_type type);
extern u32 vusb_readl(u32 VUSB_reg_base, u32 ehci_reg_addr, e_vusb_hc_reg_type type);
extern spinlock_t sduh_usb_indirect_reg_lock;
extern XC_USBH_priv_struct g_XC_USBH_info;

extern u32 vusb_data_buf_allocate(void);
extern void vusb_data_buf_free(u32 paddr);

extern unsigned int XCDmaHostToFB (u32 addrSrcHost, u32 addrDestXC3, u32 len);
extern unsigned int xc_vusb_host_to_fb (u32 addrSrcHost, u32 addrDestXC3, u32 len);
extern unsigned int xc_vusb_fb_to_host(u32 src, u32 dest, u32 len);
extern unsigned int XCDmaFBToHost (u32 addrSrcXC3, u32 addrDestHost, u32 len);
#ifdef CONFIG_XC5_VUSB_SGDMA
extern unsigned int XCIndirectDmaHostToFB (u32 addrSrcHost, u32 addrDestXC3, u32 len);
extern unsigned int XCIndirectDmaFBToHost (u32 addrSrcXC3, u32 addrDestHost, u32 len);
#endif


extern u32 vusb_ed_hw_seg_allocate(dma_addr_t* handle);
extern void vusb_ed_hw_seg_free(u32 ed_hw_seg_vaddr);
extern u32 vusb_td_hw_seg_allocate(dma_addr_t* handle);
extern void vusb_td_hw_seg_free(u32 td_hw_seg_vaddr);
extern u32 vusb_qtd_hw_seg_allocate(dma_addr_t* handle);
extern void vusb_qtd_hw_seg_free(u32 qtd_hw_seg_vaddr);
extern u32 vusb_qh_hw_seg_allocate(dma_addr_t* handle);
extern void vusb_qh_hw_seg_free(u32 qh_hw_seg_vaddr);
extern void vusb_dump_regs(void);
extern int XC_USBH_Global_Struct_Init(void);
extern void XC_USBH_init_wait(void);
extern void XC_USBH_init_wakeup(void);

#endif

