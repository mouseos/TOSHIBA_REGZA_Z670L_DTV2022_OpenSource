/*
 *  pci.c: Setup Xcode SoC PCIH.
 *
 *  Copyright 2009 ViXS Systems Inc
 *	  Philip Yang <pyang@vixs.com>
 *
  *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
  * 
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <mach/pci.h>
#include <plat/xcodeRegDef.h>
#include <mach/irqs.h>

#ifdef CONFIG_PLAT_XCODE64xx
#define BASE_ADDR_X   0xe0000000
#define BASE_ADDR_X_1 0xf0000000
#define BASE_ADDR_X_FILTER_START 0xe00
#define BASE_ADDR_X_FILTER_END   0xfff
static struct resource pcih_mem_resource = {
    .name   = "Xcode PCI MEM",
    .start  = BASE_ADDR_X + 0x01000000,			//TBD
    .end    = BASE_ADDR_X + 0x0EFFFFFF, /* 512 MB */
    .flags  = IORESOURCE_MEM,
};

static struct resource pcih_io_resource = {
    .name	= "Xcode PCI I/O",
    .start	= BASE_ADDR_X,		//TBD
    .end	= BASE_ADDR_X + 0x00FFFFFF,  /* 64KB */
    .flags	= IORESOURCE_IO,
};
#else
#define BASE_ADDR_X   0xffc00000
#define BASE_ADDR_X_1 0xf0000000
#define BASE_ADDR_X_FILTER_START 0xffc
#define BASE_ADDR_X_FILTER_END   0xffe
static struct resource pcih_mem_resource = {
    .name	= "Xcode PCI MEM",
    .start  = BASE_ADDR_X+0x100000 ,			//TBD
    .end    = BASE_ADDR_X + 0x003FFFFF, /* 512 MB */
    .flags	= IORESOURCE_MEM,
};

static struct resource pcih_io_resource = {
    .name	= "Xcode PCI I/O",
    .start	= BASE_ADDR_X,		//TBD
    .end	= BASE_ADDR_X + 0x00FFFF,  /* 64KB */
    .flags	= IORESOURCE_IO,
};

#endif
static struct resource pcieh1_mem_resource = {
    .name	= "Xcode PCIE 1 MEM",
    .start  = BASE_ADDR_X_1 + 0x01000000,			//TBD
    .end    = BASE_ADDR_X_1 + 0x0EFFFFFF, /* 512 MB */
    .flags	= IORESOURCE_MEM,
};

static struct resource pcieh1_io_resource = {
    .name	= "Xcode PCIE 1 I/O",
    .start	= BASE_ADDR_X_1,		//TBD
    .end	= BASE_ADDR_X_1 + 0x00FFFFFF,  /* 64KB */
    .flags	= IORESOURCE_IO,
};

extern struct pci_ops xcode_pci_ops;

/*
 * The xcode5 PCIe device initization stages, 
 * this status flags is show in /proc/xc5_init_stat
 * 0: not initialized
 * 1: xcode5 memory initialized
 * 2: xcode5 driver and firmware initialized
 * 3: xcode5 SATA over PCIe initialized
 * 4: xcode5 USB EHCI over PCIe initialized
 * 5: xcode5 USB OHCI over PCIe initialized
 */
volatile unsigned int xc5_pci_dev_initialized = 0;
EXPORT_SYMBOL(xc5_pci_dev_initialized);

/*
 * The xc5_register_lock is the spinlock for critical xcode5 register lock
 * it protect the access of registers:
 * CG_RESET_REG
 * CG_CLK_STOP
 * DEDICATED GPIO
 */
spinlock_t xc5_register_lock;
EXPORT_SYMBOL(xc5_register_lock);

// Initialize PCIe Host Bridge 
int pollReg (unsigned int address, unsigned int expectedValue, unsigned int mask, int timeOut)
{
    int i;
    for (i=0; i<timeOut; i++) {
        if ((mmr_read(address) & mask) == expectedValue)
	    return 0;
            mdelay(1);
    }
    return -1;
}

unsigned int readHostBridgeCfgReg (unsigned int cfgRegOffset, int port)
{
    mmr_write(cfgRegOffset, PCIE_DBI_ADDR+port*0xF800);
    mmr_write(0x00000001, PCIE_DBI_CTRL+port*0xF800);  //read
    if (pollReg  ((PCIE_ASYNC_IF_STATUS+port*0xF800), 0x00000001, 0x00000001, 100) ) {
        DBG("[%s:%d]PCIEH%d\n: Reading from register address 0x%08X -1ED! timeout", __func__,__LINE__,(port == 0) ? 0:1, cfgRegOffset);
        return (-1);
    }
    return mmr_read (PCIE_DBI_DOUT+port*0xF800);
}

// write data to a PCI Host internal register
int writeHostBridgeCfgReg (unsigned int cfgRegOffset, unsigned int data, unsigned int mask, int port)
{
    unsigned int readBackData = 0;
    //printk("%s:%d data:%x offset:%x\n",__func__,__LINE__,data, cfgRegOffset);
    mmr_write(data, PCIE_ASYNC_IF_WDAT_LO+port*0xF800);
    mmr_write(cfgRegOffset, PCIE_DBI_ADDR+port*0xF800);
    mmr_write(0x000000F1, PCIE_DBI_CTRL+port*0xF800);  //write
    if (pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100) == -1) {
        printk("[%s:%d]PCIEH%d: Write to PCIe Host register -1ED!\n", __func__,__LINE__, (port == 0) ? 0:1);
        printk("[%s:%d]PCIEH%d: Timeout error while writing 0x%08X to register address 0x%08X\n", __func__,__LINE__,(port == 0) ? 0:1, data, cfgRegOffset);
        return -1;
    }

    if ((readBackData = (readHostBridgeCfgReg (cfgRegOffset, port) & mask)) != data) {
        printk("[%s:%d]PCIEH%d: Write to PCIe Host register -1ED!\n", __func__,__LINE__,(port == 0) ? 0:1);
        printk("[%s:%d]PCIEH%d: Wrote 0x%08X to register address 0x%08X and read back 0x%08X\n",__func__,__LINE__, (port == 0) ? 0:1, data, cfgRegOffset, readBackData);
        return -1;
    }
    return 0;
}



// General global constant variables
#define VIXS_VEND_ID		0x1745
#ifdef CONFIG_PLAT_XCODE64xx
#define VIXS_DEV_ID			0x6000 // for Stingray
#else
#define VIXS_DEV_ID			0x6800 // for capri 
#endif
#define PCI_DV			0x84
#define VENDOR_ID_MASK		0x0000FFFF
#define DEVICE_ID_MASK		0xFFFF0000
#define DEVICE_ID_SHIFT		16

int fpga_read(unsigned int save2_i2c_addr, unsigned int read_index, unsigned int expect_data){

	unsigned int save2_read_temp = mmr_read(RBM_PCI_SUB_CFG) >> 28;
	unsigned int save2_i2c_command_data;
	//Clear ppc reset
	if (save2_read_temp >= 6) {
	  mmr_write(mmr_read(ACC_RESET_REG0) &  ~PPC_RESET_MASK, ACC_RESET_REG0);
	} else {
      //CG_RESET_REG \ PPC_RESET = 0x0
	  //CG_RESET_REG \ PPC_RESET = 0x1
	  //CG_RESET_REG \ PPC_RESET = 0x0
	}

	mmr_write(0xFFFFFFFF, PPC_INT_STATUS);
	
	// Configure I2C0
	mmr_write(0xF0000068, I2C0_CONFIG);		 // 2ms, 7-bit, 100Kbps
    save2_i2c_command_data = 0x00001301 | (save2_i2c_addr << 16);
	  // write index to be read
	mmr_write(read_index, I2C0_DATA_LOAD);
	mmr_write(save2_i2c_command_data, I2C0_COMMAND);  // write, device address, 1 byte
	  
	mdelay(100);

	while ((mmr_read(PPC_INT_STATUS)  & 0x00300040) != 0x0){};
    // Check the I2C channel status for successful 9554 programming
	if(mmr_read(I2C0_COMMAND) !=  save2_i2c_command_data) { // Ensure that the error and timeout status bits are clear...
		DBG("REG I2C0_COMMAND:%x\n",mmr_read(I2C0_COMMAND));
		return -1;	
	  } 		   
	  if((mmr_read(PPC_INT_STATUS) & 0x00300040) !=  0x40) { // That the interrupt status bit has been set, and then...
		DBG("REG PPC_INT_STATUS:%x\n",mmr_read(PPC_INT_STATUS));
		return -1;
	  } else {
		mmr_write(mmr_read(PPC_INT_STATUS) | PPC_INT_STATUS_I2C0_XFERDONE_MASK, PPC_INT_STATUS);
	  }
	
	  // read data at index
	  save2_i2c_command_data = 0x00000301 | (save2_i2c_addr << 16);
	  mmr_write(save2_i2c_command_data, I2C0_COMMAND);	 // read, device address 0x19, 1 bytes
	
	mdelay(100);
	  while((mmr_read(PPC_INT_STATUS)  & 0x00300040) != 0x0){};

  // Check the I2C channel status for successful 9554 programming
  	if(mmr_read(I2C0_COMMAND) !=  save2_i2c_command_data) { // Ensure that the error and timeout status bits are clear...
		DBG("I2C0_COMMAND\n");
		return -1;
	  } 		   
	  if((mmr_read(PPC_INT_STATUS) & 0x00300040) !=  0x40) {	// That the interrupt status bit has been set, and then... IVAN IVAN CHECK THIS
		return -1;
	  } else {
  	  	mmr_write(mmr_read(PPC_INT_STATUS) | PPC_INT_STATUS_I2C0_XFERDONE_MASK, PPC_INT_STATUS);
	  }
	
	//  EXPECTED_DATA = I2C0_DATA_READ & 0xFF
	return 0;
}

static int xc5_pci_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", xc5_pci_dev_initialized);
    return 0;
}

static int xc5_pci_proc_open(struct inode *inode, struct file *file)
{	
    return single_open(file, xc5_pci_proc_show, NULL);
}

static const struct file_operations xc5_pci_proc_ops = {
	.open       = xc5_pci_proc_open,
	.read       = seq_read,
	.llseek     = seq_lseek,
	.release    = seq_release_private,
};

static __init int xc5_pci_proc_init(void)
{
	proc_create("xc5_pci_init_stat", 0, NULL, &xc5_pci_proc_ops);
	return 0;
}
module_init(xc5_pci_proc_init);

/*
 * pcie host link status:
 * -1: unknow
 *  0: link is not up
 *  1: link is up
 */
int xcode_pcie_host_link_status[2] = {-1,-1};

int InitPCIeHostBridge (int port)
{
    unsigned int tempData = 0;
        
    if(port == 1)
    {
        printk("%s:%d off:0x20 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x20, 0)); 
        printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	
    }

    // if link is not up, return directly.
    if (xcode_pcie_host_link_status[port] == 0)
        return -1;

    // always use port0 to detect device/host mode
    if ( (mmr_read(PCIE_HOST_FB_MASK)& PCIE_HOST_FB_MASK_HOST_MODE_MASK) == 0 ){
        DBG_ERR(" pcie :%d as device PCIE_HOST_FB_MASK:%x\n",port,mmr_read(PCIE_HOST_FB_MASK));
        return -1;
    }else{
	    DBG_ERR(" pcie :%d as host PCIE_HOST_FB_MASK:%x\n",port,mmr_read(PCIE_HOST_FB_MASK));
    }
	
#ifdef CONFIG_PLAT_XCODE68xx
    mmr_write(0xffee0001, PROC5_PCIE_CFG );
#endif

    if(port == 0){	
        mmr_write(0xffffffff, PCIE_HOST_INT_MASK+ port*0xF800);
        mmr_write(0xffff0001, PCIE_HOST_MSI_ADDR+ port*0xF800);
        mmr_write(0xff0f0001, PCIE_HOST_CONFIG+ port*0xF800);
        mmr_write (0xFFF00001, PCIE_HOST_FB_MASK+port*0xF800);		// unmask all addresses
    }
    
    if(port == 1)
    {
        printk("%s:%d off:0x20 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x20, 0)); 
        printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	
    }

#ifdef CONFIG_PLAT_XCODE64xx
	mmr_write((0x7F<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_SWING_LOW_SHIFT) |\
		 (0x7F<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_SWING_FULL_SHIFT) |\
		 (0x20)<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_DEEMPH_GEN2_6DB_SHIFT|\
		  (0x15)<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_DEEMPH_GEN1_SHIFT|\
                  (0x15), PCIE_PHY_GEN2_CTRL+port*0xF800);
#else
	mmr_write((0x7F<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_SWING_LOW_SHIFT) |\
		 (0x7F<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_SWING_FULL_SHIFT) |\
		 (0x23)<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_DEEMPH_GEN2_6DB_SHIFT|\
		  (0x18)<<PCIE_PHY_GEN2_CTRL_PCIE_PHY_TX_DEEMPH_GEN1_SHIFT|\
                  (0x18), PCIE_PHY_GEN2_CTRL+port*0xF800);
#endif

	DBG("PCIEH%d: no link! reset again!\n", port );

    if(port == 1)
    {
        printk("%s:%d off:0x20 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x20, 0)); 
        printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	
    }
    
#ifdef CONFIG_PLAT_XCODE64xx
    if(port == 0){
        //reset pcie device
        mmr_write(mmr_read(GPIO_R_CTRL)| (1<<GPIO_R_CTRL_GPIO_MODE_SEL_SHIFT), GPIO_R_CTRL);
        mmr_write(mmr_read(GPIO_R_OUT)|((1<<4)|(1<<3)), GPIO_R_OUT);
        mmr_write(mmr_read(GPIO_R_OE)|((1<<4)|(1<<3)), GPIO_R_OE);
        mmr_write(mmr_read(GPIO_R_OUT)|((1<<4)|(1<<3)), GPIO_R_OUT);
        mdelay(100);
        mmr_write(mmr_read(GPIO_R_OUT)& ~(1<<4), GPIO_R_OUT);
        mdelay(100);
        mmr_write(mmr_read(GPIO_R_OUT)| (1<<4), GPIO_R_OUT);
        mmr_write(mmr_read(GPIO_DEDICATED_OUTEN)| (GPIO_DEDICATED_OUTEN7_MASK), GPIO_DEDICATED_OUTEN);
        mmr_write(mmr_read(GPIO_DEDICATED_OUT)& ~(GPIO_DEDICATED_OUTEN7_MASK), GPIO_DEDICATED_OUT);
        mdelay(100);
        mmr_write(mmr_read(GPIO_DEDICATED_OUT)| (GPIO_DEDICATED_OUTEN7_MASK), GPIO_DEDICATED_OUT);
	mmr_write(0x60001745, RBM_PCI_SUB_CFG);
    }
#endif

    if(port == 0){
        mmr_write(0x04800000, RBM_PCI_CFG) ;
    }

    if ((port == 0) || ((xcode_pcie_host_link_status[0] == 0) && (xcode_pcie_host_link_status[1] == 1))){
        mmr_write(0x10, PCIE_PHY_ANALOG_CTRL);
        mmr_write(0, PCIE_PHY_ANALOG_CTRL);

        #ifdef CONFIG_PLAT_XCODE64xx
        mmr_write(0x10, PCIE_PHY_ANALOG_CTRL+0xF800);
        mmr_write(0, PCIE_PHY_ANALOG_CTRL+0xF800);
        #endif

        printk("[%s:%d]PCIEH%d: reset pcie device.\n", __func__,__LINE__, (port == 0) ? 0:1);

        #ifdef CONFIG_PLAT_XCODE64xx
        //reset pcie device
        mmr_write(mmr_read(GPIO_R_CTRL)| (1<<GPIO_R_CTRL_GPIO_MODE_SEL_SHIFT), GPIO_R_CTRL);
        mmr_write(mmr_read(GPIO_R_OUT)|((1<<4)|(1<<3)), GPIO_R_OUT);
        mmr_write(mmr_read(GPIO_R_OE)|((1<<4)|(1<<3)), GPIO_R_OE);    
        mdelay(100);
        mmr_write(mmr_read(GPIO_R_OUT)& ~(1<<4), GPIO_R_OUT);
        mdelay(100);
        mmr_write(mmr_read(GPIO_R_OUT)| (1<<4), GPIO_R_OUT);
        #endif

    }

    mmr_write (mmr_read(PCIE_DIAGNOSTIC+port*0xF800) & ~PCIE_DIAGNOSTIC_DIAGNOSTIC_LTSSM_EN_MASK, \
                        PCIE_DIAGNOSTIC+port*0xF800) ;	// allow device access


    if (pollReg  ((PCIE_LINK_STATUS+port*0xF800), 0x00000003, 0x00000003, 1) ) {
#ifdef CONFIG_PLAT_XCODE64xx
        mmr_write(0x60001745, RBM_PCI_SUB_CFG) ;
#else
        mmr_write(0x68001745, RBM_PCI_SUB_CFG) ;
#endif
        printk("[%s:%d]PCIEH%d: waiting for link\n", __func__,__LINE__, (port == 0) ? 0:1);
    	mmr_write(0x04800000, RBM_PCI_CFG) ;
    	if (pollReg  ((PCIE_LINK_STATUS+port*0xF800), 0x00000003, 0x00000003, 1000) ) {
            printk("[%s:%d]PCIEH:%d: link is not up !\n", __func__,__LINE__, (port == 0) ? 0:1);
            xcode_pcie_host_link_status[port] = 0;
            return -1;
        }
    }
    
    printk("[%s:%d]PCIEH%d: link is up!\n", __func__,__LINE__, (port == 0) ? 0:1);

    xcode_pcie_host_link_status[port] = 1;

    mmr_write (0x20110, PCIE_ASYNC_IF_WDAT_LO+port*0xF800);
    mmr_write (0x80c, PCIE_DBI_ADDR+port*0xF800) ;
    mmr_write (0x000000F1, PCIE_DBI_CTRL+port*0xF800);  //write
    if (pollReg  ((PCIE_ASYNC_IF_STATUS+port*0xF800), 0x00000001, 0x00000001, 100)){
	    printk("[%s:%d]PCIEH%d: link status!\n", __func__,__LINE__, (port == 0) ? 0:1);
	    return -1;
    }
   
    mmr_write (0x00100006, PCIE_ASYNC_IF_WDAT_LO + port*0xF800) ;       //0x147            
    mmr_write (4, PCIE_DBI_ADDR +port*0xF800);                                   
    mmr_write (0x00000001, PCIE_DBI_CTRL+port*0xF800);  //read                  
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);
    tempData = mmr_read  (PCIE_DBI_DOUT+port*0xF800);              
    mmr_write (0x000000F1, PCIE_DBI_CTRL+port*0xF800) ;  //write                 
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);
                                                                    
    //enable ecrc check                                             
    mmr_write(0x00000100, PCIE_ASYNC_IF_WDAT_LO+port*0xF800) ;                  
    mmr_write (0x118, PCIE_DBI_ADDR+port*0xF800) ;                               
    mmr_write (0x000000F1, PCIE_DBI_CTRL+port*0xF800) ;  //write                 
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);
    mmr_read (PCIE_DBI_CTRL+port*0xF800);  //read                  
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);
    tempData = mmr_read (PCIE_DBI_DOUT+port*0xF800);

    //set filter mask so msg can go through core
/*   
    mmr_write (0x20000280, PCIE_ASYNC_IF_WDAT_LO+port*0xF800) ; //set bit29, standard msg
    mmr_write (0x71c, PCIE_DBI_ADDR+port*0xF800) ;
    mmr_write (0x000000F1, PCIE_DBI_CTRL+port*0xF800) ;  //write
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 10000);
    mmr_read (PCIE_DBI_CTRL+port*0xF800);  //read
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 10000);
*/
    mmr_write (0x00000003, PCIE_ASYNC_IF_WDAT_LO+port*0xF800) ; //set bit1 and bit0, vendor define 1 and 0
    mmr_write (0x720, PCIE_DBI_ADDR+port*0xF800) ;
    mmr_write (0x000000F1, PCIE_DBI_CTRL+port*0xF800);  //write
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);
    mmr_write (0x00000001, PCIE_DBI_CTRL+port*0xF800) ;  //read
    pollReg  (PCIE_ASYNC_IF_STATUS+port*0xF800, 0x00000001, 0x00000001, 100);

    // verify PCIe Host Bridge Vendor ID and Device ID
    if ((tempData = readHostBridgeCfgReg (0, port)) < 0) {
        printk("[%s:%d]PCIEH%d: Reading PCIe Host Bridge's Vendor ID and Device ID -1ED! tempData:%x\n", __func__,__LINE__, tempData,(port == 0) ? 0:1);
        return -1;
    }
    
    if ((tempData & VENDOR_ID_MASK) != VIXS_VEND_ID) {
        printk("[%s:%d]PCIEH%d: Reading PCIe Host Bridge's Vendor ID -1ED!\n", __func__,__LINE__, (port == 0) ? 0:1);
        printk("[%s:%d]PCIEH%d: Read 0x%08X as Vendor ID. Expected Vendor ID is 0x%08X.\n",  __func__,__LINE__,(port == 0) ? 0:1, tempData & VENDOR_ID_MASK, VIXS_VEND_ID);
        return -1;
    }

    if (((tempData & DEVICE_ID_MASK) >> DEVICE_ID_SHIFT) != VIXS_DEV_ID) {
        printk("[%s:%d]PCIEH%d: Reading PCIe Host Bridge's Device ID -1ED!\n",  __func__,__LINE__,(port == 0) ? 0:1);
        printk("[%s:%d]PCIEH%d: Read 0x%08X as Device ID. Expected Device ID is 0x%08X.\n",  __func__,__LINE__,(port == 0) ? 0:1, ((tempData & DEVICE_ID_MASK) >> DEVICE_ID_SHIFT), VIXS_DEV_ID);
        return -1;
    }
	
    printk("[%s:%d]PCIEH%d: PCIe Host Bridge Vendor ID = 0x%08X, Device ID = 0x%08X\n",  __func__,__LINE__,(port == 0) ? 0:1, tempData & VENDOR_ID_MASK, ((tempData & DEVICE_ID_MASK) >> DEVICE_ID_SHIFT));
    // according to Ricky Iu a bigger value should be written to Memory Base register than Memory Limit register 
    if (writeHostBridgeCfgReg (0x20, 0x10, 0x1F, port) == -1) {
        DBG("\nPCIEH%d: PCIe Host Bridge initialization -1ED!", (port == 0) ? 0:1);
        DBG("\nPCIEH%d: Writing to PCIe Host memory base register -1ED", (port == 0) ? 0:1);
        return -1;
    }
    // according to Ricky Iu a bigger value should be written to PF Memory Base register than PF Memory Limit register 
    if (writeHostBridgeCfgReg (0x24, 0x10, 0x1F, port) == -1) {
        DBG("\nPCIEH%d: PCIe Host Bridge initialization -1ED!", (port == 0) ? 0:1);
        DBG("\nPCIEH%d: Writing to PCIe Host prefechable memory base register -1ED", (port == 0) ? 0:1);
        return -1;
    }

    //printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	
    // Write 6 to PCIe Host Command Register (setting Memory Space and Bus Master bits)
    if (writeHostBridgeCfgReg (4, 6, 0xF, port) == -1) {
        DBG("\nPCIEH%d: PCIe Host Bridge initialization -1ED!", (port == 0) ? 0:1);
        DBG("\nPCIEH%d: Setting Memory Space and Bus Master bits in PCIe Host Command register -1ED", (port == 0) ? 0:1);
        return -1;
    }

    //printk("%s:%d off:0x20 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x20, 0)); 
    //printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	

    return 0;
}

static int __init xcode_pcie_setup(int nr, struct pci_sys_data *sys)
{
    int ret=0;
    
    if(InitPCIeHostBridge(nr)<0)
        goto error;
    
    if(nr==0){
        ret = request_resource(&iomem_resource, &pcih_mem_resource);
        if (ret) {
            DBG_ERR(KERN_ERR "PCI0: unable to allocate memory region (%d)\n", ret);
            goto error;
	}
        ret = request_resource(&iomem_resource, &pcih_io_resource);
        if (ret) {
            DBG_ERR(KERN_ERR "PCI0: unable to allocate IO region (%d)\n", ret);
            goto error;
        }
        pci_add_resource_offset(&sys->resources, &pcih_mem_resource, sys->mem_offset);
    }else{
        ret = request_resource(&iomem_resource, &pcieh1_mem_resource);
        if (ret) {
            DBG_ERR(KERN_ERR "PCI1: unable to allocate memory region (%d)\n", ret);
            goto error;
        }
        ret = request_resource(&iomem_resource, &pcieh1_io_resource);
	if (ret) {
            DBG_ERR(KERN_ERR "PCI1: unable to allocate IO region (%d)\n", ret);
		goto error;
	}
	pci_add_resource_offset(&sys->resources, &pcieh1_mem_resource, sys->mem_offset);
	}
	return 1;
error:
      return ret;
}

static struct pci_bus __init *
xcode_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
    struct pci_bus *bus;
    bus =  pci_scan_root_bus(NULL, sys->busnr, &xcode_pci_ops, sys, &sys->resources);
    return bus;
}

#define bus_to_port(bus)		(&xcode_pcih_controller[bus]) //TBD
static int __init xcode_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
   int irq =    dev->bus->number ? XCODE6_IRQ_PCIE1:XCODE6_IRQ_PCIE0; //IRQ is TBD
   DBG_ERR("irq:%x dev->bus->number:%x\n",irq, dev->bus->number);
   return irq;
    //return XCODE_PCIE_PORT ? XCODE6_IRQ_PCIE1:XCODE6_IRQ_PCIE0;
}

static struct hw_pci xcode_pci __initdata = { 
#ifdef CONFIG_PLAT_XCODE68xx
    .nr_controllers = 1,
#else
    .nr_controllers = 2,
#endif
//    .swizzle    = pci_std_swizzle,
    .setup      = xcode_pcie_setup,
    .scan       = xcode_pcie_scan_bus,
    .map_irq    = xcode_pcie_map_irq,
};

static int __init xcode_pcie_init(void)
{
#ifdef CONFIG_PLAT_XCODE64xx
    int board_id=mmr_read(CG_DUMMY_REG1);
    if((board_id & 0xFF00 )==0x1100) 
        return 0;
#endif

    pcibios_min_io=0;
    pcibios_min_mem=BASE_ADDR_X;
    pci_common_init(&xcode_pci);
	spin_lock_init(&xc5_register_lock);
    //printk("%s:%d off:0x20 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x20, 0));	
    //printk("%s:%d off:0x24 val:%x\n",__func__,__LINE__,readHostBridgeCfgReg (0x24, 0));	
    //mmr_write(0xdead0000, PCIE_DUMMY_REG);
    return 0;
}

subsys_initcall(xcode_pcie_init);



