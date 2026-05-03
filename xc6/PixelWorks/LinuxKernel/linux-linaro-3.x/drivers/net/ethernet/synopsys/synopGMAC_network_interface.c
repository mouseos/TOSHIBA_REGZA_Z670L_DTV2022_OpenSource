/** \file
 * This is the network dependent layer to handle network related functionality.
 * This file is tightly coupled to neworking frame work of linux 2.6.xx kernel.
 * The functionality carried out in this file should be treated as an example only
 * if the underlying operating system is not Linux. 
 * 
 * \note Many of the functions other than the device specific functions
 *  changes for operating system other than Linux 2.6.xx
 * \internal 
 *-----------------------------REVISION HISTORY-----------------------------------
 * Synopsys			01/Aug/2007				Created
 * Stanley                      11/Aug/2008                             Revised
 */


//#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
 
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include <net/sock.h> 
#include <net/checksum.h>
#include <net/tcp.h>
#include <linux/delay.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet_lro.h>
#include <linux/ctype.h>
#include <linux/random.h>

#include "synopGMAC_Host.h"
#include "synopGMAC_plat.h"
#include "synopGMAC_network_interface.h"
#include "synopGMAC_Dev.h"


#define IOCTL_READ_REGISTER  SIOCDEVPRIVATE+1
#define IOCTL_WRITE_REGISTER SIOCDEVPRIVATE+2
#define IOCTL_READ_IPSTRUCT  SIOCDEVPRIVATE+3
#define IOCTL_READ_RXDESC    SIOCDEVPRIVATE+4
#define IOCTL_READ_TXDESC    SIOCDEVPRIVATE+5
#define IOCTL_POWER_DOWN     SIOCDEVPRIVATE+6


#define DRV_MODULE_NAME     "XC3"
#define	DRV_MODULE_VERSION  "1.1"


#define DEFAULT_MAC_ADDRESS_10 { 0x00, 0x88, 0x23, 0x84, 0x85, 0x47 }
#define DEFAULT_MAC_ADDRESS_2 { 0x00, 0x20, 0x38, 0x48, 0x58, 0x99 }
#define NO_PHY 0xFFFFFFFF
struct sockaddr mac_addr;
struct sockaddr mac1_addr;

static void gmac_start(struct net_device *netdev);
static void gmac_stop(struct net_device *netdev);

//Mac address 
extern struct sockaddr mac_addr; /* Intialised while processing parameters in setup.c */
extern struct sockaddr mac1_addr; /* Intialised while processing parameters in setup.c */

extern struct net_device * g_netdev[];

extern int xc3_use_lro;

/*Sample Wake-up frame filter configurations*/

u32 synopGMAC_wakeup_filter_config0[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter1 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x5F5F5F5F,     // For Filter3 CRC is based on 0,1,2,3,4,6,8,9,10,11,12,14,16,17,18,19,20,22,24,25,26,27,28,30 bytes from offset
					0x09000000,     // Filter 0,1,2 are disabled, Filter3 is enabled and filtering applies to only multicast packets
					0x1C000000,     // Filter 0,1,2 (no significance), filter 3 offset is 28 bytes from start of Destination MAC address 
					0x00000000,     // No significance of CRC for Filter0 and Filter1
					0xBDCC0000      // No significance of CRC for Filter2, Filter3 CRC is 0xBDCC
					};
u32 synopGMAC_wakeup_filter_config1[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x00000000,	// For Filter1 CRC is not computed may be it is 0x0000
					0x7A7A7A7A,	// For Filter2 CRC is based on 1,3,4,5,6,9,11,12,13,14,17,19,20,21,25,27,28,29,30 bytes from offset
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00010000,     // Filter 0,1,3 are disabled, Filter2 is enabled and filtering applies to only unicast packets
					0x00100000,     // Filter 0,1,3 (no significance), filter 2 offset is 16 bytes from start of Destination MAC address 
					0x00000000,     // No significance of CRC for Filter0 and Filter1
					0x0000A0FE      // No significance of CRC for Filter3, Filter2 CRC is 0xA0FE
					};
u32 synopGMAC_wakeup_filter_config2[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x000000FF,	// For Filter1 CRC is computed on 0,1,2,3,4,5,6,7 bytes from offset
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00000100,     // Filter 0,2,3 are disabled, Filter 1 is enabled and filtering applies to only unicast packets
					0x0000DF00,     // Filter 0,2,3 (no significance), filter 1 offset is 223 bytes from start of Destination MAC address 
					0xDB9E0000,     // No significance of CRC for Filter0, Filter1 CRC is 0xDB9E
					0x00000000      // No significance of CRC for Filter2 and Filter3 
					};

/*
The synopGMAC_wakeup_filter_config3[] is a sample configuration for wake up filter. 
Filter1 is used here
Filter1 offset is programmed to 50 (0x32)
Filter1 mask is set to 0x000000FF, indicating First 8 bytes are used by the filter
Filter1 CRC= 0x7EED this is the CRC computed on data 0x55 0x55 0x55 0x55 0x55 0x55 0x55 0x55

Refer accompanied software DWC_gmac_crc_example.c for CRC16 generation and how to use the same.
*/

u32 synopGMAC_wakeup_filter_config3[] = {
					0x00000000,	// For Filter0 CRC is not computed may be it is 0x0000
					0x000000FF,	// For Filter1 CRC is computed on 0,1,2,3,4,5,6,7 bytes from offset
					0x00000000,	// For Filter2 CRC is not computed may be it is 0x0000
					0x00000000,     // For Filter3 CRC is not computed may be it is 0x0000
					0x00000100,     // Filter 0,2,3 are disabled, Filter 1 is enabled and filtering applies to only unicast packets
					0x00003200,     // Filter 0,2,3 (no significance), filter 1 offset is 50 bytes from start of Destination MAC address 
					0x7eED0000,     // No significance of CRC for Filter0, Filter1 CRC is 0x7EED, 
					0x00000000      // No significance of CRC for Filter2 and Filter3 
					};

#if 1
/**
 * Function used to detect the cable plugging and unplugging.
 * This function gets scheduled once in every second and polls
 * the PHY register for network cable plug/unplug. Once the 
 * connection is back the GMAC device is configured as per
 * new Duplex mode and Speed of the connection.
 * @param[in] u32 type but is not used currently. 
 * \return returns void.
 * \note This function is tightly coupled with Linux 2.6.xx.
 * \callgraph
 */


static void synopGMAC_linux_cable_unplug_function(u32 param)
{
    s32 status;
    u16 data;
    unsigned int carrier;	
    struct ethtool_cmd ecmd;
    synopGMACPciNetworkAdapter *adapter ;
    synopGMACdevice            *gmacdev ; 
    struct net_device * netdev ;

	if (synopGMAC_is_MAC_to_MAC())
		return;

    adapter = (synopGMACPciNetworkAdapter *)param;
    gmacdev = adapter->synopGMACdev; 
    netdev = adapter->synopGMACnetdev;

    status = synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_STATUS_REG, &data);
    carrier = netif_carrier_ok(netdev);

    if(((data & Mii_Link) == 0) && carrier)
	{
    	printk("Link down: %s\n", netdev->name);
		gmacdev->autoneg_done = 0;
        netif_carrier_off(netdev);
    }
    else if((data & Mii_Link)&&!carrier)
	{
    	printk("Link up: %s\n", netdev->name);
		netif_carrier_on(netdev);
    }

	if((data & Mii_Link) && (!gmacdev->mii.force_media) && (!gmacdev->autoneg_done))
	{
		u16 data1;
		synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_STATUS_REG, &data1);
		if((data1 & Mii_AutoNegCmplt) != 0)
		{
	
			mii_ethtool_gset(&gmacdev->mii,&ecmd);
			gmacdev->Speed = ecmd.speed;	
			vixs_eth_fix_clk_source(gmacdev);
			gmacdev->DuplexMode = ecmd.duplex;
		
			gmacdev->autoneg_done = 1;
			netif_stop_queue(netdev);
			gmac_stop(netdev);
			gmac_start(netdev);
			netif_start_queue(netdev);
			
		}
	}

    adapter->synopGMAC_cable_unplug_timer.expires = CHECK_TIME + jiffies;
    add_timer(&adapter->synopGMAC_cable_unplug_timer);
}
#endif

static void synopGMAC_linux_powerdown_mac(synopGMACdevice *gmacdev)
{
	TR0("Put the GMAC to power down mode..\n");
	// Disable the Dma engines in tx path
	//GMAC_Power_down = 1;	// Let ISR know that Mac is going to be in the power down mode
	gmacdev->GMAC_Power_down = 1;	// Let ISR know that Mac is going to be in the power down mode
	synopGMAC_disable_dma_tx(gmacdev);
	plat_delay(10000);		//allow any pending transmission to complete
	// Disable the Mac for both tx and rx
	synopGMAC_tx_disable(gmacdev);
	synopGMAC_rx_disable(gmacdev);
        plat_delay(10000); 		//Allow any pending buffer to be read by host
	//Disable the Dma in rx path
        synopGMAC_disable_dma_rx(gmacdev);

	//enable the power down mode
	//synopGMAC_pmt_unicast_enable(gmacdev);
	
	//prepare the gmac for magic packet reception and wake up frame reception
	synopGMAC_magic_packet_enable(gmacdev);
	synopGMAC_write_wakeup_frame_register(gmacdev, synopGMAC_wakeup_filter_config3);

	synopGMAC_wakeup_frame_enable(gmacdev);

	//gate the application and transmit clock inputs to the code. This is not done in this driver :).

	//enable the Mac for reception
	synopGMAC_rx_enable(gmacdev);

	//Enable the assertion of PMT interrupt
	synopGMAC_pmt_int_enable(gmacdev);
	synopGMAC_rgmii_int_disable(gmacdev);
	//enter the power down mode
	synopGMAC_power_down_enable(gmacdev);
	return;
}

static void xc3_wakeup_notify_host(void)
{
    // trigger GPIO[7] low-high-low to notify host/MW to wakeup
}

static void synopGMAC_linux_powerup_mac(synopGMACdevice *gmacdev)
{
	if( synopGMAC_is_magic_packet_received(gmacdev))
	{
		TR0("GMAC wokeup due to Magic Pkt Received\n");
	    xc3_wakeup_notify_host();
    }

#ifdef CONFIG_PM
	//GMAC_Power_down = 0;	// Let ISR know that MAC is out of power down now
	gmacdev->GMAC_Power_down = 0;	// Let ISR know that MAC is out of power down now
    //Disable the assertion of PMT interrupt
	//synopGMAC_pmt_int_disable(gmacdev);
    synopGMAC_rgmii_int_disable(gmacdev);
	//Enable the mac and Dma rx and tx paths
	synopGMAC_rx_enable(gmacdev);
    synopGMAC_enable_dma_rx(gmacdev);

	synopGMAC_tx_enable(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);
#endif

	return;
}
/**
  * This sets up the transmit Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Device is interested only after the descriptors are setup. Therefore this function
  * is not included in the device driver API. This function should be treated as an
  * example code to design the descriptor structures for ring mode or chain mode.
  * This function depends on the pcidev structure for allocation consistent dma-able memory in case of linux.
  * This limitation is due to the fact that linux uses pci structure to allocate a dmable memory
  *	- Allocates the memory for the descriptors.
  *	- Initialize the Busy and Next descriptors indices to 0(Indicating first descriptor).
  *	- Initialize the Busy and Next descriptors to first descriptor address.
  * 	- Initialize the last descriptor with the endof ring in case of ring mode.
  *	- Initialize the descriptors in chain mode.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in tx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note This function fails if allocation fails for required number of descriptors in Ring mode, but in chain mode
  *  function returns -ESYNOPGMACNOMEM in the process of descriptor chain creation. once returned from this function
  *  user should for gmacdev->TxDescCount to see how many descriptors are there in the chain. Should continue further
  *  only if the number of descriptors in the chain meets the requirements  
  */
  
s32 synopGMAC_setup_tx_desc_queue(synopGMACdevice * gmacdev,u32 no_of_desc, u32 desc_mode,DmaDesc * desc, dma_addr_t dma_addr)
{
    s32 i;

    DmaDesc *first_desc = NULL;
    //DmaDesc *second_desc = NULL;
    gmacdev->TxDescCount = 0;

	if(desc_mode == RINGMODE){
		panic("ETH: RINGMODE is not supported\n");
	    TR("Total size of memory required for Tx Descriptors in Ring Mode = 0x%08x\n",((sizeof(DmaDesc) * no_of_desc)));
	    //first_desc = plat_alloc_consistent_dmaable_memory (pcidev, sizeof(DmaDesc) * no_of_desc,&dma_addr);
	    first_desc = (DmaDesc *)plat_alloc_consistent_dmaable_memory (sizeof(DmaDesc) * no_of_desc,&dma_addr);
	    if(first_desc == NULL){
		    TR("Error in Tx Descriptors memory allocation\n");
		    return -ESYNOPGMACNOMEM;
	    }

	    gmacdev->TxDescCount = no_of_desc;
	    gmacdev->TxDesc      = first_desc;
	    gmacdev->TxDescDma   = dma_addr;
	    TR("First Tx desc = %8x\n",first_desc);

	    for(i =0; i < gmacdev -> TxDescCount; i++){
		    synopGMAC_tx_desc_init_ring(gmacdev->TxDesc + i, i == gmacdev->TxDescCount-1);
		    TR("Tx Descriptor%02d %08x \n",i, (unsigned int)(gmacdev->TxDesc + i) );
	    }

    }
    else{

    	//rewrite by Stanley Aug 15,2008
    	int i;
    	//DmaDesc *pdescBuf = plat_alloc_consistent_dmaable_memory ((2+no_of_desc)*(sizeof(DmaDesc)),&dma_addr);
    	DmaDesc *pdescBuf = desc;
		dma_addr_t pdescBufPhys = dma_addr;

    	if(pdescBuf == NULL)
    	{
    		TR("Error in Tx Descriptor Memory allocation in Chain mode\n");
    		return -ESYNOPGMACNOMEM;
    	}
		flush_descriptors(pdescBuf, no_of_desc);
    	
    	gmacdev->TxDescCount = no_of_desc;
    	gmacdev->TxDesc       = pdescBuf;
    	gmacdev->TxDescDma    = dma_addr;

    	i = 0;
    	do {

			first_desc = pdescBuf++;
			pdescBufPhys += sizeof(DmaDesc);

			arc_write_uncached_32(&first_desc->buffer2, pdescBufPhys);
			arc_write_uncached_32(&first_desc->data2,(u32)pdescBuf);
			synopGMAC_tx_desc_init_chain((DmaDesc *)NON_CACHE_ADDR(first_desc));
			first_desc = pdescBuf;

			arc_write_uncached_32(&pdescBuf->buffer2,dma_addr);
			arc_write_uncached_32(&pdescBuf->data2, (u32)gmacdev->TxDesc);
		
			i++;
		} while(i < (no_of_desc-1));

		synopGMAC_tx_desc_init_chain((DmaDesc *)NON_CACHE_ADDR(pdescBuf));

    	TR("Tx===================================================================Tx\n");
    	
    	#ifdef DEBUG	
    	{
    		int i;
    		DmaDesc *pdesc= gmacdev->TxDesc;
    		
    		for(i=0;i<no_of_desc;i++,pdesc++)
    		{
#ifdef WE_DONT_HAVE_THIS_ON_ARM
    			TR("%02d  %08x %08x %08x %08x %08x %08x %08x \n",i, (u32)pdesc, 
    				arc_read_uncached_32(&pdesc->status),
    				arc_read_uncached_32(&pdesc->length),
    				arc_read_uncached_32(&pdesc->buffer1),
    				arc_read_uncached_32(&pdesc->buffer2),
    				arc_read_uncached_32(&pdesc->data1), 
    				arc_read_uncached_32(&pdesc->data2));
#endif
    		}
    	
    	}
    	#endif
    	
    	TR("Tx===================================================================Tx\n");

    }

	gmacdev->TxNext = 0;
	gmacdev->TxBusy = 0;
	gmacdev->TxNextDesc = gmacdev->TxDesc;
	gmacdev->TxBusyDesc = gmacdev->TxDesc;

	return -ESYNOPGMACNOERR;
}


/**
  * This sets up the receive Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Device is interested only after the descriptors are setup. Therefore this function
  * is not included in the device driver API. This function should be treated as an
  * example code to design the descriptor structures in ring mode or chain mode.
  * This function depends on the pcidev structure for allocation of consistent dma-able memory in case of linux.
  * This limitation is due to the fact that linux uses pci structure to allocate a dmable memory
  *	- Allocates the memory for the descriptors.
  *	- Initialize the Busy and Next descriptors indices to 0(Indicating first descriptor).
  *	- Initialize the Busy and Next descriptors to first descriptor address.
  * 	- Initialize the last descriptor with the endof ring in case of ring mode.
  *	- Initialize the descriptors in chain mode.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in rx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note This function fails if allocation fails for required number of descriptors in Ring mode, but in chain mode
  *  function returns -ESYNOPGMACNOMEM in the process of descriptor chain creation. once returned from this function
  *  user should for gmacdev->RxDescCount to see how many descriptors are there in the chain. Should continue further
  *  only if the number of descriptors in the chain meets the requirements  
  */
//s32 synopGMAC_setup_rx_desc_queue(synopGMACdevice * gmacdev,struct pci_dev * pcidev,u32 no_of_desc, u32 desc_mode)
s32 synopGMAC_setup_rx_desc_queue(synopGMACdevice * gmacdev,u32 no_of_desc, u32 desc_mode,DmaDesc * desc, dma_addr_t dma_addr)
{
    s32 i;

    DmaDesc *first_desc = NULL;
    //DmaDesc *second_desc = NULL;
    gmacdev->RxDescCount = 0;

    if(desc_mode == RINGMODE){
		panic("ETH: RINGMODE is not supported\n");
    	TR("total size of memory required for Rx Descriptors in Ring Mode = 0x%08x\n",((sizeof(DmaDesc) * no_of_desc)));
    	//first_desc = plat_alloc_consistent_dmaable_memory (pcidev, sizeof(DmaDesc) * no_of_desc, &dma_addr);
    	first_desc = plat_alloc_consistent_dmaable_memory (sizeof(DmaDesc) * no_of_desc,&dma_addr);
    	if(first_desc == NULL){
    		TR("Error in Rx Descriptor Memory allocation in Ring mode\n");
    		return -ESYNOPGMACNOMEM;
    	}
    	gmacdev->RxDescCount = no_of_desc;
    	gmacdev->RxDesc      = first_desc;
    	gmacdev->RxDescDma   = dma_addr;
    	
    	for(i =0; i < gmacdev -> RxDescCount; i++){
    		synopGMAC_rx_desc_init_ring(gmacdev->RxDesc + i, i == gmacdev->RxDescCount-1);
    		TR("Rx Descriptor%02d %08x \n",i, (unsigned int)(gmacdev->RxDesc + i));

    	}

    }
    else{
    	//rewrite by Stanley Aug 15,2008
    	int i;
    	//DmaDesc *pdescBuf = plat_alloc_consistent_dmaable_memory ((2+no_of_desc)*(sizeof(DmaDesc)),&dma_addr);
    	DmaDesc *pdescBuf = desc;
		dma_addr_t pdescBufPhys = dma_addr;

    	if(pdescBuf == NULL)
    	{
    		TR("Error in Rx Descriptor Memory allocation in Ring mode\n");
    		return -ESYNOPGMACNOMEM;
    	}
    	flush_descriptors(pdescBuf, no_of_desc);

		gmacdev->RxDescCount = no_of_desc;
		gmacdev->RxDesc       = pdescBuf;
		gmacdev->RxDescDma    = dma_addr;
		
		i = 0;
		do {
			first_desc = pdescBuf++;
			pdescBufPhys+=sizeof(DmaDesc);

			arc_write_uncached_32(&first_desc->buffer2, pdescBufPhys);
			arc_write_uncached_32(&first_desc->data2,(u32)pdescBuf);
			synopGMAC_rx_desc_init_chain((DmaDesc *)NON_CACHE_ADDR(first_desc));

			first_desc = pdescBuf;

			arc_write_uncached_32(&pdescBuf->buffer2,dma_addr);
			arc_write_uncached_32(&pdescBuf->data2, (u32)gmacdev->RxDesc);
			
			i++;
		} while(i < (no_of_desc-1));

		synopGMAC_rx_desc_init_chain((DmaDesc *)NON_CACHE_ADDR(pdescBuf));

    	TR("Rx===================================================================Rx\n");
    	
    	#ifdef DEBUG	
    	{
    		int i;
    		DmaDesc *pdesc= gmacdev->RxDesc;

    		for(i=0;i<no_of_desc;i++,pdesc++)
    		{
    			TR0("%02d  %08x %08x %08x %08x %08x %08x %08x \n",i, (u32)pdesc, 
    				arc_read_uncached_32(&pdesc->status),
    				arc_read_uncached_32(&pdesc->length),
    				arc_read_uncached_32(&pdesc->buffer1),
    				arc_read_uncached_32(&pdesc->buffer2),
    				arc_read_uncached_32(&pdesc->data1), 
    				arc_read_uncached_32(&pdesc->data2));
    		}
    	}
    	#endif
    	
    	TR("Rx===================================================================Rx\n");


    }

	gmacdev->RxNext = 0;
	gmacdev->RxBusy = 0;
	gmacdev->RxNextDesc = gmacdev->RxDesc;
	gmacdev->RxBusyDesc = gmacdev->RxDesc;

	return -ESYNOPGMACNOERR;
}

/**
  * This gives up the receive Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Once device's Dma is stopped the memory descriptor memory and the buffer memory deallocation,
  * is completely handled by the operating system, this call is kept outside the device driver Api.
  * This function should be treated as an example code to de-allocate the descriptor structures in ring mode or chain mode
  * and network buffer deallocation.
  * This function depends on the pcidev structure for dma-able memory deallocation for both descriptor memory and the
  * network buffer memory under linux.
  * The responsibility of this function is to 
  *     - Free the network buffer memory if any.
  *	- Fee the memory allocated for the descriptors.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in rx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note No referece should be made to descriptors once this function is called. This function is invoked when the device is closed.
  */
//void synopGMAC_giveup_rx_desc_queue(synopGMACdevice * gmacdev, struct pci_dev *pcidev, u32 desc_mode)
void synopGMAC_giveup_rx_desc_queue(synopGMACdevice * gmacdev, u32 desc_mode)
{
s32 i;

DmaDesc *first_desc = NULL;
dma_addr_t first_desc_dma_addr;
u32 status;
dma_addr_t dma_addr1;
dma_addr_t dma_addr2;
u32 length1;
u32 length2;
u32 data1;
u32 data2;

if(desc_mode == RINGMODE){
	for(i =0; i < gmacdev -> RxDescCount; i++){
		synopGMAC_get_desc_data(gmacdev->RxDesc + i, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		if((length1 != 0) && (data1 != 0)){
			//pci_unmap_single(pcidev,dma_addr1,0,PCI_DMA_FROMDEVICE);
			dev_kfree_skb((struct sk_buff *) data1);	// free buffer1
			TR("(Ring mode) rx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		if((length2 != 0) && (data2 != 0)){
			//pci_unmap_single(pcidev,dma_addr2,0,PCI_DMA_FROMDEVICE);
			dev_kfree_skb((struct sk_buff *) data2);	//free buffer2
			TR("(Ring mode) rx buffer2 %08x of size %d from %d rx descriptor is given back\n",data2, length2, i);
		}
	}
	//plat_free_consistent_dmaable_memory(pcidev,(sizeof(DmaDesc) * gmacdev->RxDescCount),gmacdev->RxDesc,gmacdev->RxDescDma); //free descriptors memory
	plat_free_consistent_dmaable_memory((sizeof(DmaDesc) * gmacdev->RxDescCount),gmacdev->RxDesc); //free
	TR("Memory allocated %08x  for Rx Desriptors (ring) is given back\n",(u32)gmacdev->RxDesc);
}
else{
	TR("rx-------------------------------------------------------------------rx\n");
	first_desc          = gmacdev->RxDesc;
	first_desc_dma_addr = gmacdev->RxDescDma;
	for(i =0; i < gmacdev -> RxDescCount; i++){
		synopGMAC_get_desc_data(first_desc, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		//TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,first_desc->status,first_desc->length,first_desc->buffer1,first_desc->buffer2,first_desc->data1,first_desc->data2);
#ifdef WE_DONT_HAVE_THIS_ON_ARM
		TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,
				arc_read_uncached_32(&first_desc->status),
				arc_read_uncached_32(&first_desc->length),
				arc_read_uncached_32(&first_desc->buffer1),
				arc_read_uncached_32(&first_desc->buffer2),
				arc_read_uncached_32(&first_desc->data1),
				arc_read_uncached_32(&first_desc->data2));
#endif
		
		if((length1 != 0) && (data1 != 0)){
			//pci_unmap_single(pcidev,dma_addr1,0,PCI_DMA_FROMDEVICE);
			dev_kfree_skb((struct sk_buff *) data1);	// free buffer1
			TR("(Chain mode) rx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		//plat_free_consistent_dmaable_memory(pcidev,(sizeof(DmaDesc)),first_desc,first_desc_dma_addr); //free descriptors
		//plat_free_consistent_dmaable_memory((sizeof(DmaDesc)),first_desc); //free descriptors

		TR("Memory allocated %08x for Rx Descriptor (chain) at  %d is given back\n",data2,i);

		first_desc = (DmaDesc *)data2;
		first_desc_dma_addr = dma_addr2;
	}

	TR("rx-------------------------------------------------------------------rx\n");
}
gmacdev->RxDesc    = NULL;
gmacdev->RxDescDma = 0;
return;
}

/**
  * This gives up the transmit Descriptor queue in ring or chain mode.
  * This function is tightly coupled to the platform and operating system
  * Once device's Dma is stopped the memory descriptor memory and the buffer memory deallocation,
  * is completely handled by the operating system, this call is kept outside the device driver Api.
  * This function should be treated as an example code to de-allocate the descriptor structures in ring mode or chain mode
  * and network buffer deallocation.
  * This function depends on the pcidev structure for dma-able memory deallocation for both descriptor memory and the
  * network buffer memory under linux.
  * The responsibility of this function is to 
  *     - Free the network buffer memory if any.
  *	- Fee the memory allocated for the descriptors.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to pci_device structure.
  * @param[in] number of descriptor expected in tx descriptor queue.
  * @param[in] whether descriptors to be created in RING mode or CHAIN mode.
  * \return 0 upon success. Error code upon failure.
  * \note No reference should be made to descriptors once this function is called. This function is invoked when the device is closed.
  */
//void synopGMAC_giveup_tx_desc_queue(synopGMACdevice * gmacdev,struct pci_dev * pcidev, u32 desc_mode)
void synopGMAC_giveup_tx_desc_queue(synopGMACdevice * gmacdev, u32 desc_mode)
{
s32 i;

DmaDesc *first_desc = NULL;
dma_addr_t first_desc_dma_addr;
u32 status;
dma_addr_t dma_addr1;
dma_addr_t dma_addr2;
u32 length1;
u32 length2;
u32 data1;
u32 data2;

if(desc_mode == RINGMODE){
	for(i =0; i < gmacdev -> TxDescCount; i++){
		synopGMAC_get_desc_data(gmacdev->TxDesc + i,&status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		if((length1 != 0) && (data1 != 0)){
			//pci_unmap_single(pcidev,dma_addr1,0,PCI_DMA_TODEVICE);
			dev_kfree_skb((struct sk_buff *) data1);	// free buffer1
			TR("(Ring mode) tx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		if((length2 != 0) && (data2 != 0)){
			//pci_unmap_single(pcidev,dma_addr2,0,PCI_DMA_TODEVICE);
			dev_kfree_skb((struct sk_buff *) data2);	//free buffer2
			TR("(Ring mode) tx buffer2 %08x of size %d from %d rx descriptor is given back\n",data2, length2, i);
		}
	}
	//plat_free_consistent_dmaable_memory(pcidev,(sizeof(DmaDesc) * gmacdev->TxDescCount),gmacdev->TxDesc,gmacdev->TxDescDma); //free descriptors
	plat_free_consistent_dmaable_memory((sizeof(DmaDesc) * gmacdev->TxDescCount),gmacdev->TxDesc); //free descriptors
	TR("Memory allocated %08x for Tx Desriptors (ring) is given back\n",(u32)gmacdev->TxDesc);
}
else{
	TR("tx-------------------------------------------------------------------tx\n");
	first_desc          = gmacdev->TxDesc;
	first_desc_dma_addr = gmacdev->TxDescDma;
	for(i =0; i < gmacdev -> TxDescCount; i++){
		synopGMAC_get_desc_data(first_desc, &status, &dma_addr1, &length1, &data1, &dma_addr2, &length2, &data2);
		//TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,first_desc->status,first_desc->length,first_desc->buffer1,first_desc->buffer2,first_desc->data1,first_desc->data2);
#ifdef WE_DONT_HAVE_THIS_ON_ARM
		TR("%02d %08x %08x %08x %08x %08x %08x %08x\n",i,(u32)first_desc,
				arc_read_uncached_32(&first_desc->status),
				arc_read_uncached_32(&first_desc->length),
				arc_read_uncached_32(&first_desc->buffer1),
				arc_read_uncached_32(&first_desc->buffer2),
				arc_read_uncached_32(&first_desc->data1),
				arc_read_uncached_32(&first_desc->data2));
#endif

		if((length1 != 0) && (data1 != 0)){
			//pci_unmap_single(pcidev,dma_addr1,0,PCI_DMA_TODEVICE);
			dev_kfree_skb((struct sk_buff *) data2);	// free buffer1
			TR("(Chain mode) tx buffer1 %08x of size %d from %d rx descriptor is given back\n",data1, length1, i);
		}
		//plat_free_consistent_dmaable_memory(pcidev,(sizeof(DmaDesc)),first_desc,first_desc_dma_addr); //free descriptors
		//plat_free_consistent_dmaable_memory((sizeof(DmaDesc)),first_desc); //free descriptors
		TR("Memory allocated %08x for Tx Descriptor (chain) at  %d is given back\n",data2,i);

		first_desc = (DmaDesc *)data2;
		first_desc_dma_addr = dma_addr2;
	}
	TR("tx-------------------------------------------------------------------tx\n");

}
gmacdev->TxDesc    = NULL;
gmacdev->TxDescDma = 0;
return;
}

static int xc3_get_skb_hdr(struct sk_buff *skb, void **iphdr,
		       void **tcph, u64 *hdr_flags, void *priv)
{
	//synopGMACdevice * gmacdev = priv;
	unsigned int ip_len;
	struct iphdr *iph;
	struct tcphdr *th;

	if(skb->protocol != htons(ETH_P_IP))
		return -1;
	
        /* non tcp/udp packets */
	//if (!cqe->header_length)
	//	return -1;

    //printk("xc3_get_skb_hdr, skb at %p\n", skb);
    
    /* non tcp packet */
    skb_reset_network_header(skb);
    //skb->nh.raw = skb->mac.raw + ETH_HLEN;
    iph = ip_hdr(skb);
    //iph = (struct iphdr *)(skb->nh.raw);
	if (iph->protocol != IPPROTO_TCP)
	{
	    //printk("iph at 0x%p\n", iph);
	    /*if (iph->protocol != 17 && iph->protocol!= 29 &&
	        iph->protocol != 23 && iph->protocol != 20 &&
	        iph->protocol != 27)
    	  */
    	//printk("return -1, iph->protocol=%d\n", iph->protocol);
		return -1;
	}

    ip_len = ip_hdrlen(skb);
	//ip_len = (iph->ihl) * 4;
    //skb->h.raw = skb->nh.raw + ip_len;
    skb_set_transport_header(skb, ip_len);
    //th = (struct tcphdr*)(skb->h.raw);	
    th = tcp_hdr(skb);
    
        /* check if ip header and tcp header are complete */
	//if (iph->tot_len < ip_len + th->doff * 4) 
	if (iph->tot_len < ip_len + tcp_hdrlen(skb))
	{
    	//printk("return -2, check if ip header and tcp header are complete\n");
    	//printk("iph->tot_len=%d, ip_len=%d, tcp_len=%d\n", iph->tot_len, ip_len, th->doff * 4); 
		return -1;
    }
    
	*hdr_flags = LRO_IPV4 | LRO_TCP;
	*iphdr = iph;
	*tcph = th;

    //printk("exit xc3_get_skb_hdr\n");

	return 0;
}

void print_lro_statistic(void)
{
#if 0
    struct net_device *netdev = g_netdev[0];
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice *gmacdev;

	adapter = netdev->priv;
	gmacdev = adapter->synopGMACdev;

    printk("lro, total=%ld, aggregated=%ld, flushed=%ld, no_desc=%ld\n", 
            gmacdev->lro_mgr.stats.total,
            gmacdev->lro_mgr.stats.aggregated,
            gmacdev->lro_mgr.stats.flushed,
            gmacdev->lro_mgr.stats.no_desc);

   gmacdev->lro_mgr.stats.total = 0;
   gmacdev->lro_mgr.stats.aggregated = 0;
   gmacdev->lro_mgr.stats.flushed = 0;
   gmacdev->lro_mgr.stats.no_desc = 0;
#endif
}

static void xc3_proc_skb(synopGMACdevice * gmacdev, struct sk_buff *skb)
{
	if (xc3_use_lro) {
		lro_receive_skb(&gmacdev->lro_mgr, skb, gmacdev);
	} else {
        #ifdef CONFIG_SYNOPSYS_NAPI
        netif_receive_skb(skb);
        #else
        netif_rx(skb);        /* send the packet up protocol stack */
        #endif
	}
}


/**
 * Function to Receive a packet from the interface.
 * After Receiving a packet, DMA transfers the received packet to the system memory
 * and generates corresponding interrupt (if it is enabled). This function prepares
 * the sk_buff for received packet after removing the ethernet CRC, and hands it over
 * to linux networking stack.
 * 	- Updataes the networking interface statistics
 *	- Keeps track of the rx descriptors
 * @param[in] pointer to net_device structure. 
 * \return void.
 * \note This function runs in interrupt context.
 */

static int synop_handle_received_data(struct net_device *netdev, int limit)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	s32 desc_index;
	u32 count = 0;
	
	u32 data1;
	u32 len;
	u32 status;
	u32 dma_addr1;

	struct sk_buff *skb; //This is the pointer to hold the received data

	TR("%s\n",__FUNCTION__);	

	adapter = netdev_priv(netdev);
	if(adapter == NULL){
		TR0("Unknown Device\n");
		return count;
	}
	
	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR0("GMAC device structure is missing\n");
		return count;
	} 

	/*Handle the Receive Descriptors*/
	do{
		u32 length1;
		desc_index = xc3_get_rx_qptr(gmacdev, &status,&dma_addr1,&length1, &data1);

		if(desc_index >= 0 && data1 != 0)
		{
			TR("Received Rx Descriptor %p for skb 0x%08x whose status is %08x\n",gmacdev->RxBusyDesc,data1,status);
			/*At first step unmapped the dma address*/

            count++;
            
			skb = (struct sk_buff *)data1;
		
			if(synopGMAC_is_rx_desc_valid(status)){
				len =  synopGMAC_get_rx_desc_frame_length(status) - 4; //Not interested in Ethernet CRC bytes
				inv_dcache_range((unsigned long)skb->tail,(unsigned long)(skb->tail + len + 4));

				skb_put(skb,len);
				
				#ifdef IPC_OFFLOAD
						// Now lets check for the IPC offloading
				/*  Since we have enabled the checksum offloading in hardware, lets inform the kernel
				    not to perform the checksum computation on the incoming packet. Note that ip header 
  				    checksum will be computed by the kernel immaterial of what we inform. Similary TCP/UDP/ICMP
				    pseudo header checksum will be computed by the stack. What we can inform is not to perform
				    payload checksum. 		
   				    When CHECKSUM_UNNECESSARY is set kernel bypasses the checksum computation.		    
				*/

				TR("Checksum Offloading will be done now\n");
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				//modified the code to support 3.30a			
				//Linux Kernel doesnot care for ipv4 header checksum. So we will simply proceed by printing a warning ....
		        if(synopGMAC_is_rx_ipv4header_checksum_error(gmacdev, status)){
					TR0("Error in 16bit IPv4 Header Checksum\n");
				}
				
				// Control should not come here if we allow DMA to drop the packets if error
				if(synopGMAC_is_rx_payload_checksum_error(gmacdev, status)){
	    			TR0("Error in Payload Checksum Error\n");
    				/* In case control is here tell the networking stack to recompute the checksum since hardware reported
				   some issues.   This is done by informing the stack saying Checksum is not cont computed
  				   and CHECKSUM_NONE is set.
		    		*/
					skb->ip_summed = CHECKSUM_NONE;	
				}
					
				#endif
				
				skb->dev = netdev;
				skb->protocol = eth_type_trans(skb, netdev);

                xc3_proc_skb(gmacdev, skb);
				
				netdev->last_rx = jiffies;
				adapter->synopGMACNetStats.rx_packets++;
				adapter->synopGMACNetStats.rx_bytes += len;
			}
			else
			{
				/* 
				 * only output message when last descriptor received
				 * According to IP spec the error bit is valid only when bit 8 (last desc) set
				 */ 
				if(status & (1 << 8))
			    	TR0("rx_desc status invalid = 0x%08x\n", status);
				adapter->synopGMACNetStats.rx_errors++;
				adapter->synopGMACNetStats.collisions       += synopGMAC_is_rx_frame_collision(status);
				adapter->synopGMACNetStats.rx_crc_errors    += synopGMAC_is_rx_crc(status);
				adapter->synopGMACNetStats.rx_frame_errors  += synopGMAC_is_frame_dribbling_errors(status);
				adapter->synopGMACNetStats.rx_length_errors += synopGMAC_is_rx_frame_length_errors(status);
				
				//synop_handle_received_data() is not only called in interrupt handle,but also is called in NAPI poll();
				dev_kfree_skb_any(skb);
			}
			
			// get skb from pre-allocated skb_list first, if list is empty then use normal skb allocation
			skb = xc_get_skb_from_list();

			if (skb == NULL)
			{
				// list is empty, have to do alloc skb 
				skb = netdev_alloc_skb(netdev, netdev->mtu + ETHERNET_PACKET_EXTRA + 64);  // two cache lines extra
				if(skb == NULL){
					TR0("low memory, SKB memory allocation failed \n");
					adapter->synopGMACNetStats.rx_dropped++;
					return count;
				}

	            skb_reserve(skb, 32 - ((unsigned int)skb->tail & 0x1f) + 2);  // recv buffer align to cache line
    	        TR("skb=%p, head=0x%x, tail=0x%x\n", skb, skb->head, skb->tail);
			}

            dma_addr1 = virt_to_phys(skb->tail);  

			desc_index = synopGMAC_set_rx_qptr(gmacdev,dma_addr1, TRIM_SKB_LENGTH(skb_tailroom(skb)+32), (u32)skb,0,0,0);

			if(desc_index < 0){
				TR0("Cannot set Rx Descriptor for skb %08x\n",(u32)skb);

				//synop_handle_received_data() is not only called in interrupt handle,but also is called in NAPI poll();
				dev_kfree_skb_any(skb);
			}

			// handle all received frames, total handled frame counter maybe larger than total recv desc 
			if(count >= limit)
               break;
		}
	}while(desc_index >= 0);

	return count;
}


#ifdef CONFIG_SYNOPSYS_NAPI

static int xc3_napipoll(struct napi_struct *napi, int budget)
{
    synopGMACdevice            *gmacdev;
	struct net_device * netdev;
	int done;

	gmacdev = container_of(napi, synopGMACdevice, napi);
	netdev=gmacdev->netdev;
    
	done = synop_handle_received_data(netdev, budget);

	if(0 < done )
	{
		if (xc3_use_lro)
		{
			lro_flush_all(&gmacdev->lro_mgr);
		}
	
	}
	if(0 == done)
	{
		netif_rx_complete(napi);
		synopGMAC_enable_interrupt(gmacdev, DmaIntEnable);
	}

	return done;

}
#endif

irqreturn_t synopGMAC_intr_handler(s32 intr_num, void * dev_id, struct pt_regs *regs)
{ 
 
	/*Kernels passes the netdev structure in the dev_id. So grab it*/
	struct net_device *netdev;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	//struct pci_dev *pcidev;
	u32 interrupt;
	s32 status;
	u32 dma_addr;

	netdev  = (struct net_device *) dev_id;
	if(netdev == NULL){
		TR("Unknown Device\n");
		return -1;
	}

	adapter = netdev_priv(netdev);
	if(adapter == NULL){
		TR("Adapter Structure Missing\n");
		return -1;
	}

	gmacdev = adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR("GMAC device structure Missing\n");
		return -1;
	}
	
#ifdef USE_IIA
	// plate dependent
	if(gmacdev->reg == 0)
	{
        if(!IIALocalReadInt(IIA_ETHR0_INT))
			return IRQ_NONE;
	}
	
	if(gmacdev->reg == 1)
	{
        if(!IIALocalReadInt(IIA_ETHR1_INT))
			return IRQ_NONE;
	}
#endif

	/*Read the Dma interrupt status to know whether the interrupt got generated by our device or not*/
	interrupt = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaStatus);

	TR("%s:Dma Status Reg: 0x%08x\n",__FUNCTION__,interrupt);

	if(interrupt == 0)
		return IRQ_NONE;

	//synopGMAC_disable_interrupt_all(gmacdev);
    TR("%s::Clearing the interrupts (Still unservice interrupt: %08x)\n",__FUNCTION__,interrupt);
	synopGMAC_clear_interrupt(gmacdev);

	if(interrupt & (GmacLPIIntr|GmacTTIIntr)){
		TR("%s:: Interrupt due to LPI or TTI module\n",__FUNCTION__);
		// read back to clear interrupt
		synopGMACReadReg((u32 *)gmacdev->MacBase, GmacLPICtrlStatus);
	}
	
	if(interrupt & GmacPmtIntr){
		TR0("%s:: Interrupt due to PMT module\n",__FUNCTION__);
		synopGMAC_linux_powerup_mac(gmacdev);
	}
	
	if(interrupt & GmacMmcIntr){
		TR("%s:: Interrupt due to MMC module\n",__FUNCTION__);
		TR("%s:: synopGMAC_rx_int_status = %08x\n",__FUNCTION__,synopGMAC_read_mmc_rx_int_status(gmacdev));
		TR("%s:: synopGMAC_tx_int_status = %08x\n",__FUNCTION__,synopGMAC_read_mmc_tx_int_status(gmacdev));
	}

	if(interrupt & GmacLineIntfIntr){
		int gmacIntStatus;
		gmacIntStatus = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacInterruptStatus);
		TR("%s:: Interrupt due to GMAC LINE module (GmacInt 0x%08x)\n",__FUNCTION__, gmacIntStatus);
		synopGMAC_rgmii_int_disable(gmacdev);
	}

	/*Now lets handle the DMA interrupts*/  
	//interrupt = synopGMAC_get_interrupt_type(gmacdev);
	//TR("%s:Interrupts to be handled: 0x%08x\n",__FUNCTION__,interrupt);

	if(interrupt & DmaIntErrorMask){

		u8 mac_addr0[6] = DEFAULT_MAC_ADDRESS;//after soft reset, configure the MAC address to default value
		TR0("%s::Fatal Bus Error Inetrrupt Seen\n",__FUNCTION__);
		synopGMAC_disable_dma_tx(gmacdev);
		synopGMAC_disable_dma_rx(gmacdev);
                
		synopGMAC_take_desc_ownership_tx(gmacdev);
		synopGMAC_take_desc_ownership_rx(gmacdev);
		
		synopGMAC_init_tx_rx_desc_queue(gmacdev);
		
		synopGMAC_reset(gmacdev);//reset the DMA engine and the GMAC ip
		
		synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, mac_addr0); 
		synopGMAC_dma_bus_mode_init(gmacdev, DMA_BUS_MODE_CFG);
	 	synopGMAC_dma_control_init(gmacdev, DmaRxStoreAndForward|DmaStoreAndForward);	
		synopGMAC_init_rx_desc_base(gmacdev);
		synopGMAC_init_tx_desc_base(gmacdev);
		synopGMAC_mac_init(gmacdev);
		synopGMAC_enable_dma_rx(gmacdev);
		synopGMAC_enable_dma_tx(gmacdev);
	}

	if(interrupt & DmaIntRxNormMask){
		TR("%s:: Rx Normal \n", __FUNCTION__);
        #ifdef CONFIG_SYNOPSYS_NAPI
		    synopGMAC_disable_interrupt_all(gmacdev);
            synopGMAC_enable_interrupt(gmacdev, DmaIntEnable_NAPI); 
			netif_rx_schedule(&gmacdev->napi);
        #else
			synop_handle_received_data(netdev, RECEIVE_DESC_SIZE);   /* handle received packets */
        #endif

		// don't handle error bits if normal recv is done
		return IRQ_HANDLED;
	}

	if(interrupt & DmaIntRxAbnMask){
		TR0("%s::Abnormal Rx Interrupt Seen, Dma Status Reg: 0x%08x\n",__FUNCTION__, interrupt);

		#ifdef DEBUG
    	{
    		int i;
    		DmaDesc *pdesc= gmacdev->RxDesc;

    		for(i=0; i < RECEIVE_DESC_SIZE; i++,pdesc++)
    		{
    			TR0("%02d  %08x %08x %08x %08x %08x %08x %08x \n",i, (u32)pdesc, 
    				arc_read_uncached_32(&pdesc->status),
    				arc_read_uncached_32(&pdesc->length),
    				arc_read_uncached_32(&pdesc->buffer1),
    				arc_read_uncached_32(&pdesc->buffer2),
    				arc_read_uncached_32(&pdesc->data1), 
    				arc_read_uncached_32(&pdesc->data2));
    		}
    	
    	}
    	#endif

		if(gmacdev->GMAC_Power_down == 0){	// If Mac is not in powerdown
			adapter->synopGMACNetStats.rx_over_errors++;
		}
		
		synop_handle_received_data(netdev, RECEIVE_DESC_SIZE);   /* handle received packets */
	}

	if(interrupt & DmaIntRxStoppedMask){
		TR0("%s::Receiver stopped seeing Rx interrupts\n",__FUNCTION__); //Receiver gone in to stopped state
		#if 1
		if(gmacdev->GMAC_Power_down == 0){	// If Mac is not in powerdown
		    adapter->synopGMACNetStats.rx_over_errors++;
		do{
			struct sk_buff *skb = netdev_alloc_skb(netdev, netdev->mtu + ETHERNET_PACKET_EXTRA + 64);  // two cache lines extra
			if(skb == NULL){
				TR("%s::ERROR in skb buffer allocation Better Luck Next time\n",__FUNCTION__);
				break;
			}

            skb_reserve(skb, 32 - ((unsigned int)skb->tail % 32) + 2);  // recv buffer align to cache line
            TR("skb=%p, head=0x%x, tail=0x%x\n", skb, skb->head, skb->tail);
            dma_addr = virt_to_phys(skb->tail);  

			flush_dcache_range((u32)skb->tail, (u32)skb->tail + TRIM_SKB_LENGTH(skb_tailroom(skb)+32));
			inv_dcache_range((u32)skb->tail, (u32)skb->tail + TRIM_SKB_LENGTH(skb_tailroom(skb)+32));
			
			status = synopGMAC_set_rx_qptr(gmacdev,dma_addr, TRIM_SKB_LENGTH(skb_tailroom(skb)+32), (u32)skb,0,0,0);
			TR("%s::Set Rx Descriptor no %08x for skb %08x \n",__FUNCTION__,status,(u32)skb);
			if(status < 0)
				dev_kfree_skb_irq(skb);   //dev_kfree_skb(skb); changed by Stanley

		}while(status >= 0);
		
		synopGMAC_enable_dma_rx(gmacdev);
		}
		#endif
	}

	if(interrupt & DmaIntTxNormMask){
		//xmit function has done its job
		TR("%s::Finished Normal Transmission \n",__FUNCTION__);
		xc3_release_tx_qptr(netdev, gmacdev);
		netif_wake_queue(netdev);
	}

	if(interrupt & DmaIntTxAbnMask){
		TR0("%s::Abnormal Tx Interrupt Seen\n",__FUNCTION__);
		#if 1
		if(gmacdev->GMAC_Power_down == 0){	// If Mac is not in powerdown
			xc3_release_tx_qptr(netdev, gmacdev);
			netif_wake_queue(netdev);
		}
		#endif
	}

	if(interrupt & DmaIntTxStoppedMask){
		TR0("%s::Transmitter stopped sending the packets\n",__FUNCTION__);
		#if 1
		if(gmacdev->GMAC_Power_down == 0){	// If Mac is not in powerdown
    		synopGMAC_disable_dma_tx(gmacdev);
    		synopGMAC_take_desc_ownership_tx(gmacdev);
    		
    		synopGMAC_enable_dma_tx(gmacdev);
    		TR("%s::Transmission Resumed\n",__FUNCTION__);
		}
		#endif
	}

	return IRQ_HANDLED;

}


static s32 AutoDectectPhy(synopGMACdevice * gmacdev)
{
	int i;
	u16 data1;
	u16 data2;
    u32 phyid=0;

	for(i=0;i<32;i++)
	{
		//gmacdev->PhyBase = i ;
		synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,i,PHY_ID_HI_REG,&data1);
		synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,i,PHY_ID_LOW_REG,&data2);
		if((data1!=0xffff) && (data2!=0xffff))
		{
			TR0("Find ethernet phy device:0x%04x%04x, at phy index:%d \n",data1,data2,i);
			gmacdev->PhyBase = i;
			break;
		}
	}
	
	if(i == 32 )
	{
		TR0("Can not find phy.\n");
		gmacdev->PhyBase = NO_PHY;
		return -1;
	}
        phyid = (data1<<16)|data2;	
	return phyid;
}

static s32 malloc_rx_skbs(struct net_device *netdev,synopGMACdevice * gmacdev)
{
	struct sk_buff *skb;
	u32 dma_addr;
	s32 status = 0;

	do{
		skb = netdev_alloc_skb(netdev, netdev->mtu + ETHERNET_PACKET_EXTRA + 64);	// two cache lines extra
		if(skb == NULL)
		{
			TR0("ERROR in skb buffer allocation\n");
			return -1;
		}
	
		skb_reserve(skb, 32 - ((unsigned int)skb->tail % 32) + 2);	// recv buffer align to cache line
		TR("skb=%p, head=0x%x, tail=0x%x\n", skb, skb->head, skb->tail);
		dma_addr = virt_to_phys(skb->tail);  

		flush_dcache_range((u32)skb->tail, (u32)skb->tail + TRIM_SKB_LENGTH(skb_tailroom(skb)+32));
		
		status = synopGMAC_set_rx_qptr(gmacdev,dma_addr, TRIM_SKB_LENGTH(skb_tailroom(skb)+32), (u32)skb,0,0,0);
		if(status < 0)
			dev_kfree_skb(skb);
					
	}while(status >= 0);

	return 0;
}
static void gmac_start(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;
	
	synopGMAC_reset(gmacdev);

	/*Lets read the version of ip in to device structure*/	
	synopGMAC_read_version(gmacdev);

	/*Check for Phy initialization*/
	synopGMAC_set_mdc_clk_div(gmacdev,GmiiCsrClk5);
	gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);
	synopGMAC_check_phy_init(gmacdev);
	
	synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 
		   
	TR0("transmit desc at 0x%p 0x%p\n", adapter->TxDescBuf, (void *)adapter->TxDescBufPhys);
	synopGMAC_setup_tx_desc_queue(gmacdev,TRANSMIT_DESC_SIZE, CHAINMODE ,adapter->TxDescBuf, adapter->TxDescBufPhys);
	gmacdev->FreeSendDesc= TRANSMIT_DESC_SIZE;

	
	TR("recv desc at 0x%p 0x%p\n", adapter->RxDescBuf, adapter->RxDescBufPhys);
	synopGMAC_setup_rx_desc_queue(gmacdev,RECEIVE_DESC_SIZE, CHAINMODE, adapter->RxDescBuf, adapter->RxDescBufPhys);
	malloc_rx_skbs(netdev,gmacdev);

	/*Set up the tx and rx descriptor queue/ring*/
	synopGMAC_init_tx_desc_base(gmacdev);	//Program the transmit descriptor base address in to DmaTxBase addr
	synopGMAC_init_rx_desc_base(gmacdev);	//Program the recv descriptor base address in to DmaRxBase addr
	   
	synopGMAC_dma_bus_mode_init(gmacdev, DMA_BUS_MODE_CFG);
	synopGMAC_dma_control_init(gmacdev, DmaRxStoreAndForward|DmaStoreAndForward|DmaTxSecondFrame );	
	
	/*Initialize the mac interface*/
	synopGMAC_mac_init(gmacdev);
	synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation
	
#ifdef IPC_OFFLOAD
	/*IPC Checksum offloading is enabled for this driver. Should only be used if Full Ip checksumm offload engine is configured in the hardware*/
	synopGMAC_enable_rx_chksum_offload(gmacdev);	//Enable the offload engine in the receive path
	synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev); // This is default configuration, DMA drops the packets if error in encapsulated ethernet payload
														// The FEF bit in DMA control register is configured to 0 indicating DMA to drop the errored frames.
	/*Inform the Linux Networking stack about the hardware capability of checksum offloading*/
	//netdev->features |= NETIF_F_IP_CSUM;
	TR0("enable HW checksum\n");
#endif

    TR0("enable tx\n");
	synopGMAC_enable_dma_tx(gmacdev);

    TR0("enable rx\n");	
    synopGMAC_enable_dma_rx(gmacdev);

    TR0("clear pending interrupt\n");
	synopGMAC_clear_interrupt(gmacdev);

    /*
    Disable the interrupts generated by MMC and IPC counters.
    If these are not disabled ISR should be modified accordingly to handle these interrupts.
    */  
    synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
    synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
    synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);

    TR0("enable interrupt\n");
    synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);

}

static void gmac_stop(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	
	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

	/*Disable all the interrupts*/
	synopGMAC_disable_interrupt_all(gmacdev);
	TR("the synopGMAC interrupt has been disabled\n");
	
	/*Disable  reception*/	
	synopGMAC_disable_dma_rx(gmacdev);
	TR("the synopGMAC Reception has been disabled\n");
	
	/*Disable  transmission*/
	synopGMAC_take_desc_ownership_tx(gmacdev);
	TR("the synopGMAC Transmission has been disabled\n");
	
	/*Free the Rx Descriptor contents*/
	synopGMAC_take_desc_ownership_rx(gmacdev);
	synopGMAC_giveup_rx_desc_queue(gmacdev, CHAINMODE);

	
	/*Free the Tx Descriptor contents*/
	synopGMAC_disable_dma_tx(gmacdev);
	synopGMAC_giveup_tx_desc_queue(gmacdev, CHAINMODE);
}


/**
 * Function used when the interface is opened for use.
 * We register synopGMAC_linux_open function to linux open(). Basically this 
 * function prepares the the device for operation . This function is called whenever ifconfig (in Linux)
 * activates the device (for example "ifconfig eth0 up"). This function registers
 * system resources needed 
 * 	- Attaches device to device specific structure
 * 	- Programs the MDC clock for PHY configuration
 * 	- Check and initialize the PHY interface 
 *	- ISR registration
 * 	- Setup and initialize Tx and Rx descriptors
 *	- Initialize MAC and DMA
 *	- Allocate Memory for RX descriptors (The should be DMAable)
 * 	- Initialize one second timer to detect cable plug/unplug
 *	- Configure and Enable Interrupts
 *	- Enable Tx and Rx
 *	- start the Linux network queue interface
 * @param[in] pointer to net_device structure. 
 * \return Returns 0 on success and error status upon failure.
 * \callgraph
 */

s32 synopGMAC_linux_open(struct net_device *netdev)
{
	s32 status = 0;
	s32 retval = 0;
	s32 ijk;
	u32 dma_addr;
	struct sk_buff *skb;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	u8 mac_addr0[6] = DEFAULT_MAC_ADDRESS;	

	TR("%s called \n",__FUNCTION__);

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

    synopGMAC_reset(gmacdev);

    TR("adapter= %08x gmacdev = %08x netdev = %08x\n",(u32)adapter,(u32)gmacdev,(u32)netdev);

	//synopGMAC_attach(gmacdev,MACBASE,DMABASE, DEFAULT_PHY_BASE);
	
	/*Now platform dependent initialization.*/
	//AutoDectectPhy(gmacdev);
	
	#if 0
	if( 1 == gmacdev->reg )
	{
		u32 data1;
		synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG,&data1);
		synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG,
						data1 & (~Mii_Loopback ) );
	}
	#endif

	/*Lets reset the IP*/
	//TR("adapter= %08x gmacdev = %08x netdev = %08x pcidev= %08x\n",(u32)adapter,(u32)gmacdev,(u32)netdev,(u32)pcidev);
		
	/*Attach the device to MAC struct This will configure all the required base addresses
	  such as Mac base, configuration base, phy base address(out of 32 possible phys )*/
	//synopGMAC_attach(synopGMACadapter->synopGMACdev,(u32) synopGMACMappedAddr + MACBASE,(u32) synopGMACMappedAddr + DMABASE, DEFAULT_PHY_BASE);
	//synopGMAC_attach(gmacdev,(u32) synopGMACMappedAddr + MACBASE,(u32) synopGMACMappedAddr + DMABASE, DEFAULT_PHY_BASE);
	

	/*Lets read the version of ip in to device structure*/	
	synopGMAC_read_version(gmacdev);
	
	//added by Stanley
	memcpy(mac_addr0,netdev->dev_addr,6);
	synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, mac_addr0); 

	//synopGMAC_get_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 
	synopGMAC_get_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 

	/*Now set the broadcast address*/	
	for(ijk = 0; ijk <6; ijk++){
		netdev->broadcast[ijk] = 0xff;
	}

	for(ijk = 0; ijk <6; ijk++){
		TR("netdev->dev_addr[%d] = %02x and netdev->broadcast[%d] = %02x\n",ijk,netdev->dev_addr[ijk],ijk,netdev->broadcast[ijk]);
	}
	
	/*Check for Phy initialization*/
	synopGMAC_set_mdc_clk_div(gmacdev,GmiiCsrClk5);
	gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);
	
	//AutoDectectPhy(gmacdev);
	status = synopGMAC_check_phy_init(gmacdev);

	printk("request irq %d\n", netdev->irq);
	/*Request for a shared interrupt.*/
	if(request_irq (netdev->irq, (void *)synopGMAC_intr_handler, IRQF_SHARED, netdev->name, netdev)){
		TR0("Error in request_irq\n");
		goto error_in_irq;	
	}
        
    //TR("%s owns a shared interrupt on line %d\n",netdev->name, pcidev->irq);
    TR("%s owns a non-shared interrupt on line %d\n",netdev->name,netdev->irq);
    /*Set up the tx and rx descriptor queue/ring*/

	TR0("transmit desc at 0x%p 0x%p\n", (void *)adapter->TxDescBuf, (void *)adapter->TxDescBufPhys);
    synopGMAC_setup_tx_desc_queue(gmacdev,TRANSMIT_DESC_SIZE, CHAINMODE ,adapter->TxDescBuf, adapter->TxDescBufPhys);
    synopGMAC_init_tx_desc_base(gmacdev);   //Program the transmit descriptor base address in to DmaTxBase addr

	TR0("recv desc at 0x%p 0x%p\n", (void *)adapter->RxDescBuf, (void *)adapter->RxDescBufPhys);
    synopGMAC_setup_rx_desc_queue(gmacdev,RECEIVE_DESC_SIZE, CHAINMODE, adapter->RxDescBuf, adapter->RxDescBufPhys);
    synopGMAC_init_rx_desc_base(gmacdev);   //Program the recv descriptor base address in to DmaRxBase addr
   
    synopGMAC_dma_bus_mode_init(gmacdev, DMA_BUS_MODE_CFG);
    synopGMAC_dma_control_init(gmacdev, DmaRxStoreAndForward|DmaStoreAndForward|DmaTxSecondFrame );   

    /*Initialize the mac interface*/
    synopGMAC_mac_init(gmacdev);
	//synopGMAC_promisc_enable(gmacdev);
	synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation

#ifdef IPC_OFFLOAD
    //if (gmacdev->reg == 0)
    {
        /*IPC Checksum offloading is enabled for this driver. Should only be used if Full Ip checksumm offload engine is configured in the hardware*/
        synopGMAC_enable_rx_chksum_offload(gmacdev);    //Enable the offload engine in the receive path
        synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev); // This is default configuration, DMA drops the packets if error in encapsulated ethernet payload
                                // The FEF bit in DMA control register is configured to 0 indicating DMA to drop the errored frames.
        /*Inform the Linux Networking stack about the hardware capability of checksum offloading*/
        //netdev->features =  NETIF_F_HW_CSUM;
        netdev->features |= NETIF_F_IP_CSUM;
        TR0("enable HW checksum\n");
    }
#endif

#ifdef SYNOPSYS_TSO_ENABLE
    //if (gmacdev->reg == 0)
    {
        netdev->features |= NETIF_F_TSO | NETIF_F_IP_CSUM | NETIF_F_SG;
        TR0("enable TCP segmentation offload\n");
    }
#endif
    
#ifdef CONFIG_SYNOPSYS_NAPI
     netif_napi_add(netdev, &gmacdev->napi, xc3_napipoll, RECEIVE_DESC_SIZE>>1);
#endif
    
    gmacdev->lro_mgr.max_aggr = EHEA_LRO_MAX_AGGR;
    gmacdev->lro_mgr.max_desc = MAX_LRO_DESCRIPTORS;
    gmacdev->lro_mgr.lro_arr = gmacdev->lro_desc;
    gmacdev->lro_mgr.get_skb_header = xc3_get_skb_hdr;
    
#ifdef CONFIG_SYNOPSYS_NAPI    
    gmacdev->lro_mgr.features = LRO_F_NAPI;
#endif

    gmacdev->lro_mgr.dev = netdev;
    gmacdev->lro_mgr.ip_summed = CHECKSUM_UNNECESSARY;
    gmacdev->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;
 
    do{
        skb = netdev_alloc_skb(netdev, netdev->mtu + ETHERNET_PACKET_EXTRA + 64);  // two cache lines extra
        if(skb == NULL){
            TR0("ERROR in skb buffer allocation\n");
            break;
        }

        skb_reserve(skb, 32 - ((unsigned int)skb->tail % 32) + 2);  // recv buffer align to cache line
        TR("skb=%p, head=0x%x, tail=0x%x\n", skb, skb->head, skb->tail);
        dma_addr = virt_to_phys(skb->tail);  
		
		flush_dcache_range((u32)skb->tail, (u32)skb->tail + TRIM_SKB_LENGTH(skb_tailroom(skb)+32));

		status = synopGMAC_set_rx_qptr(gmacdev,dma_addr, TRIM_SKB_LENGTH(skb_tailroom(skb)+32), (u32)skb,0,0,0);
        if(status < 0)
            dev_kfree_skb(skb);
                
    }while(status >= 0);

	gmacdev->FreeSendDesc= TRANSMIT_DESC_SIZE;

    #if 1
    TR("Setting up the cable unplug timer\n");
    // don't start unplug_timer for now
        init_timer(&adapter->synopGMAC_cable_unplug_timer);
        adapter->synopGMAC_cable_unplug_timer.function = (void *)synopGMAC_linux_cable_unplug_function;
        adapter->synopGMAC_cable_unplug_timer.data = (u32) adapter;
        adapter->synopGMAC_cable_unplug_timer.expires = CHECK_TIME + jiffies;
        add_timer(&adapter->synopGMAC_cable_unplug_timer);
    #endif
    

	/*
	Disable the interrupts generated by MMC and IPC counters.
	If these are not disabled ISR should be modified accordingly to handle these interrupts.
	*/	
	synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
    synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);

	/*plat dependent interrupt initialization */
    if(gmacdev->reg == 0)
	{
    	writel(ETH_HOST_INT_MASK_REG_PMT_INTR_EN_MASK | ETH_HOST_INT_MASK_REG_SBD_INTR_EN_MASK, (unsigned long *)(gmacdev->baseAddr + ETH_HOST_INT_MASK_REG));
	}
    
    #ifdef CONFIG_PLAT_XCODE64xx
    if(gmacdev->reg == 1)
	{
        writel(ETH_HOST_INT_MASK_REG1_PMT_INTR_EN_MASK | ETH_HOST_INT_MASK_REG1_SBD_INTR_EN_MASK, (unsigned long *)(gmacdev->baseAddr + ETH_HOST_INT_MASK_REG1));
	}
    #endif
    
#ifdef USE_IIA
    if(gmacdev->reg == 0)
        IIALocalSetMask(IIA_ETHR0_INT);
    if(gmacdev->reg == 1)
        IIALocalSetMask(IIA_ETHR1_INT);
#endif

	synopGMAC_enable_dma_rx(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);

    // enable interrupt after starting rx/tx, to avoid abnormal fifo overflow interrupt
    synopGMAC_clear_interrupt(gmacdev);
	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);

	TR("Reg DmaBusMode:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaBusMode));
	TR("Reg DmaRxBaseAddr:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaRxBaseAddr));
	TR("Reg DmaTxBaseAddr:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaTxBaseAddr));
	TR("Reg DmaControl:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaControl));
	TR("Reg DmaInterrupt:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaInterrupt));

	TR("gmacdev->TxNext:0x%08x\n",gmacdev->TxNext);
	TR("gmacdev->TxBusy:0x%08x\n",gmacdev->TxBusy);

#if 0
	{
		TR0("--------------devid:%d\n",gmacdev->reg);
		TR0("--------------Mac Register:\n");
		int i;
		for(i=0;i<48;i+=4)
			TR0("Mac reg:%02x,value:%08x\n",i,synopGMACReadReg((u32)gmacdev->MacBase,i));

		TR0("--------------Dma Register:\n");
		for(i=0;i<58;i+=4)
			TR0("DMA reg:%02x,value:%08x\n",i,synopGMACReadReg((u32)gmacdev->DmaBase,i));

	}
#endif

	// enable magic wakeup packet, WOL
	synopGMAC_magic_packet_enable(gmacdev);
	//synopGMAC_write_wakeup_frame_register(gmacdev, synopGMAC_wakeup_filter_config3);
	//synopGMAC_wakeup_frame_enable(gmacdev);
	//Enable the assertion of PMT interrupt
	synopGMAC_pmt_int_enable(gmacdev);
	synopGMAC_rgmii_int_disable(gmacdev);

	netif_start_queue(netdev);
#if 0
{
    //test mmap
    unsigned int physfb, *pmmfb, *pmmr, physmmr;
    unsigned int fbsize, mmrsize;

    physfb = 0x80000000; // Above 2G is all kernel memory space
    fbsize = 0x4000000;  // make it 64M
    pmmfb = ioremap_nocache(physfb, fbsize);
    printk("pmmfb = 0x%lx\n", pmmfb);
    *(pmmfb + 8) = 0xdeadbeef;
    printk("read pmmfb+8 = 0x%08x\n", *(pmmfb + 8));
    
    physmmr = 0x1FFF0000; 
    mmrsize = 0x10000; //64k
    pmmr = ioremap_nocache(physmmr, mmrsize);
    printk("pmmr = 0x%lx\n", pmmr);
}
#endif

#ifdef CONFIG_SYNOPSYS_NAPI
	napi_enable(&gmacdev->napi);
	netif_rx_schedule(&gmacdev->napi);
#endif

	gmacdev->opened = 1;
	return retval;

error_in_irq:

	return -ESYNOPGMACBUSY;
}

#ifdef CONFIG_PM
s32 synopGMAC_linux_resume(struct net_device *netdev)
{
	s32 status = 0;
	s32 retval = 0;
	s32 ijk;
	u32 dma_addr;
	struct sk_buff *skb;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	u8 mac_addr0[6] = DEFAULT_MAC_ADDRESS;	

	TR("%s called \n",__FUNCTION__);

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

    synopGMAC_reset(gmacdev);

    TR("adapter= %08x gmacdev = %08x netdev = %08x\n",(u32)adapter,(u32)gmacdev,(u32)netdev);

	/*Lets read the version of ip in to device structure*/	
	synopGMAC_read_version(gmacdev);
	
	//added by Stanley
	memcpy(mac_addr0,netdev->dev_addr,6);
	synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, mac_addr0); 

	//synopGMAC_get_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 
	synopGMAC_get_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 

	/*Now set the broadcast address*/	
	for(ijk = 0; ijk <6; ijk++){
		netdev->broadcast[ijk] = 0xff;
	}

	for(ijk = 0; ijk <6; ijk++){
		TR("netdev->dev_addr[%d] = %02x and netdev->broadcast[%d] = %02x\n",ijk,netdev->dev_addr[ijk],ijk,netdev->broadcast[ijk]);
	}
	
	/*Check for Phy initialization*/
	synopGMAC_set_mdc_clk_div(gmacdev,GmiiCsrClk5);
	gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);
	
	//AutoDectectPhy(gmacdev);
	status = synopGMAC_check_phy_init(gmacdev);

    TR("%s owns a non-shared interrupt on line %d\n",netdev->name,netdev->irq);
    /*Set up the tx and rx descriptor queue/ring*/

    TR("transmit desc at 0x%p\n", adapter->TxDescBuf);
    synopGMAC_setup_tx_desc_queue(gmacdev,TRANSMIT_DESC_SIZE, CHAINMODE ,adapter->TxDescBuf, adapter->TxDescBufPhys);
    synopGMAC_init_tx_desc_base(gmacdev);   //Program the transmit descriptor base address in to DmaTxBase addr

    TR("recv desc at 0x%p\n", adapter->RxDescBuf);
    synopGMAC_setup_rx_desc_queue(gmacdev,RECEIVE_DESC_SIZE, CHAINMODE, adapter->RxDescBuf, adapter->RxDescBufPhys);
    synopGMAC_init_rx_desc_base(gmacdev);   //Program the recv descriptor base address in to DmaRxBase addr

    synopGMAC_dma_bus_mode_init(gmacdev, DMA_BUS_MODE_CFG);
    synopGMAC_dma_control_init(gmacdev, DmaRxStoreAndForward|DmaStoreAndForward|DmaTxSecondFrame );

    /*Initialize the mac interface*/
    synopGMAC_mac_init(gmacdev);
	//synopGMAC_promisc_enable(gmacdev);
	synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation

#ifdef IPC_OFFLOAD
    //if (gmacdev->reg == 0)
    {
        /*IPC Checksum offloading is enabled for this driver. Should only be used if Full Ip checksumm offload engine is configured in the hardware*/
        synopGMAC_enable_rx_chksum_offload(gmacdev);    //Enable the offload engine in the receive path
        synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev); // This is default configuration, DMA drops the packets if error in encapsulated ethernet payload
                                // The FEF bit in DMA control register is configured to 0 indicating DMA to drop the errored frames.
        /*Inform the Linux Networking stack about the hardware capability of checksum offloading*/
        //netdev->features =  NETIF_F_HW_CSUM;
        netdev->features |= NETIF_F_IP_CSUM;
        TR0("enable HW checksum\n");
    }
#endif

#ifdef SYNOPSYS_TSO_ENABLE
    //if (gmacdev->reg == 0)
    {
        netdev->features |= NETIF_F_TSO | NETIF_F_IP_CSUM | NETIF_F_SG;
        TR0("enable TCP segmentation offload\n");
    }
#endif
    
#ifdef CONFIG_SYNOPSYS_NAPI
     netif_napi_add(netdev, &gmacdev->napi, xc3_napipoll, RECEIVE_DESC_SIZE>>1);
#endif
    
    gmacdev->lro_mgr.max_aggr = EHEA_LRO_MAX_AGGR;
    gmacdev->lro_mgr.max_desc = MAX_LRO_DESCRIPTORS;
    gmacdev->lro_mgr.lro_arr = gmacdev->lro_desc;
    gmacdev->lro_mgr.get_skb_header = xc3_get_skb_hdr;
    
#ifdef CONFIG_SYNOPSYS_NAPI    
    gmacdev->lro_mgr.features = LRO_F_NAPI;
#endif

    gmacdev->lro_mgr.dev = netdev;
    gmacdev->lro_mgr.ip_summed = CHECKSUM_UNNECESSARY;
    gmacdev->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;
 
    do{
        skb = netdev_alloc_skb(netdev, netdev->mtu + ETHERNET_PACKET_EXTRA + 64);  // two cache lines extra
        if(skb == NULL){
            TR0("ERROR in skb buffer allocation\n");
            break;
        }

        skb_reserve(skb, 32 - ((unsigned int)skb->tail % 32) + 2);  // recv buffer align to cache line
        TR("skb=%p, head=0x%x, tail=0x%x\n", skb, skb->head, skb->tail);
        dma_addr = virt_to_phys(skb->tail);  
        status = synopGMAC_set_rx_qptr(gmacdev,dma_addr, TRIM_SKB_LENGTH(skb_tailroom(skb)+32), (u32)skb,0,0,0);
        if(status < 0)
            dev_kfree_skb(skb);
                
    }while(status >= 0);

	gmacdev->FreeSendDesc= TRANSMIT_DESC_SIZE;
		
    TR("Setting up the cable unplug timer\n");
    // don't start unplug_timer for now
        init_timer(&adapter->synopGMAC_cable_unplug_timer);
        adapter->synopGMAC_cable_unplug_timer.function = (void *)synopGMAC_linux_cable_unplug_function;
        adapter->synopGMAC_cable_unplug_timer.data = (u32) adapter;
        adapter->synopGMAC_cable_unplug_timer.expires = CHECK_TIME + jiffies;
        add_timer(&adapter->synopGMAC_cable_unplug_timer);
    
    synopGMAC_clear_interrupt(gmacdev);

	/*
	Disable the interrupts generated by MMC and IPC counters.
	If these are not disabled ISR should be modified accordingly to handle these interrupts.
	*/	
	synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
    synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);

	/*plat dependent interrupt initialization */
    if(gmacdev->reg == 0)
    	writel(ETH_HOST_INT_MASK_REG_PMT_INTR_EN_MASK | ETH_HOST_INT_MASK_REG_SBD_INTR_EN_MASK, (unsigned long *)(gmacdev->baseAddr + ETH_HOST_INT_MASK_REG));
    
    #ifdef CONFIG_PLAT_XCODE64xx
    if(gmacdev->reg == 1)
        writel(ETH_HOST_INT_MASK_REG1_PMT_INTR_EN_MASK | ETH_HOST_INT_MASK_REG1_SBD_INTR_EN_MASK, (unsigned long *)(gmacdev->baseAddr + ETH_HOST_INT_MASK_REG1));
	#endif

	//enable_irq(netdev->irq);

	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	synopGMAC_enable_dma_rx(gmacdev);
	synopGMAC_enable_dma_tx(gmacdev);

	TR("Reg DmaBusMode:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaBusMode));
	TR("Reg DmaRxBaseAddr:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaRxBaseAddr));
	TR("Reg DmaTxBaseAddr:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaTxBaseAddr));
	TR("Reg DmaControl:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaControl));
	TR("Reg DmaInterrupt:0x%08x\n",synopGMACReadReg((u32)gmacdev->DmaBase,DmaInterrupt));

	TR("gmacdev->TxNext:0x%08x\n",gmacdev->TxNext);
	TR("gmacdev->TxBusy:0x%08x\n",gmacdev->TxBusy);

	// enable magic wakeup packet, WOL
  synopGMAC_magic_packet_enable(gmacdev);
  //synopGMAC_write_wakeup_frame_register(gmacdev, synopGMAC_wakeup_filter_config3);
  //synopGMAC_wakeup_frame_enable(gmacdev);
  //Enable the assertion of PMT interrupt
  synopGMAC_pmt_int_enable(gmacdev);
  synopGMAC_rgmii_int_disable(gmacdev);

	netif_start_queue(netdev);

#ifdef CONFIG_SYNOPSYS_NAPI
	napi_enable(&gmacdev->napi);
	netif_rx_schedule(&gmacdev->napi);
#endif

    netif_device_attach(netdev);

	return retval;
}

/**
 * Function used when the interface is closed.
 *
 * This function is registered to linux stop() function. This function is 
 * called whenever ifconfig (in Linux) closes the device (for example "ifconfig eth0 down").
 * This releases all the system resources allocated during open call.
 * system resources int needs 
 * 	- Disable the device interrupts
 * 	- Stop the receiver and get back all the rx descriptors from the DMA
 * 	- Stop the transmitter and get back all the tx descriptors from the DMA 
 * 	- Stop the Linux network queue interface
 *	- Free the irq (ISR registered is removed from the kernel)
 * 	- Release the TX and RX descripor memory
 *	- De-initialize one second timer rgistered for cable plug/unplug tracking
 * @param[in] pointer to net_device structure. 
 * \return Returns 0 on success and error status upon failure.
 * \callgraph
 */

s32 synopGMAC_linux_suspend(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice * gmacdev;
	
	TR0("%s\n",__FUNCTION__);
	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	if(adapter == NULL){
		TR0("OOPS adapter is null\n");
		return -1;
	}

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR0("OOPS gmacdev is null\n");
		return -1;
	}

    netif_device_detach(netdev);

	#ifdef CONFIG_SYNOPSYS_NAPI
	napi_disable(&gmacdev->napi);
	#endif

	/*Disable all the interrupts*/
	synopGMAC_disable_interrupt_all(gmacdev);
	TR("the synopGMAC interrupt has been disabled\n");


	/*Disable the reception*/	
	synopGMAC_disable_dma_rx(gmacdev);
	synopGMAC_take_desc_ownership_rx(gmacdev);
	TR("the synopGMAC Reception has been disabled\n");

	/*Disable the transmission*/
	synopGMAC_disable_dma_tx(gmacdev);
	synopGMAC_take_desc_ownership_tx(gmacdev);

	TR("the synopGMAC Transmission has been disabled\n");
	//netif_stop_queue(netdev);
	/*Now free the irq: This will detach the interrupt handler registered*/
	
#if 1
	/*Free the Rx Descriptor contents*/
	TR("Now calling synopGMAC_giveup_rx_desc_queue \n");
	synopGMAC_giveup_rx_desc_queue(gmacdev, CHAINMODE);

	TR("Now calling synopGMAC_giveup_tx_desc_queue \n");
	synopGMAC_giveup_tx_desc_queue(gmacdev, CHAINMODE);
#endif
	
	TR("Freeing the cable unplug timer\n");	
	del_timer(&adapter->synopGMAC_cable_unplug_timer);

	return 0;

//	TR("%s called \n",__FUNCTION__);
}
#endif
 
s32 synopGMAC_linux_close(struct net_device *netdev)
{
	synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice * gmacdev;
	
	TR0("%s\n",__FUNCTION__);
	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	if(adapter == NULL){
		TR0("OOPS adapter is null\n");
		return -1;
	}

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(gmacdev == NULL){
		TR0("OOPS gmacdev is null\n");
		return -1;
	}

	#ifdef CONFIG_SYNOPSYS_NAPI
	napi_disable(&gmacdev->napi);
	#endif

	/*Disable all the interrupts*/
	synopGMAC_disable_interrupt_all(gmacdev);
	TR("the synopGMAC interrupt has been disabled\n");


	/*Disable the reception*/	
	synopGMAC_disable_dma_rx(gmacdev);
	synopGMAC_take_desc_ownership_rx(gmacdev);
	TR("the synopGMAC Reception has been disabled\n");

	/*Disable the transmission*/
	synopGMAC_disable_dma_tx(gmacdev);
	synopGMAC_take_desc_ownership_tx(gmacdev);

	TR("the synopGMAC Transmission has been disabled\n");
	netif_stop_queue(netdev);
	/*Now free the irq: This will detach the interrupt handler registered*/
	
	//free_irq(pcidev->irq, netdev);
	//TR("the synopGMAC interrupt handler has been removed\n");
	free_irq(netdev->irq, netdev);
	
	/*Free the Rx Descriptor contents*/
	TR("Now calling synopGMAC_giveup_rx_desc_queue \n");
	synopGMAC_giveup_rx_desc_queue(gmacdev, CHAINMODE);

	TR("Now calling synopGMAC_giveup_tx_desc_queue \n");
	synopGMAC_giveup_tx_desc_queue(gmacdev, CHAINMODE);
	
	TR("Freeing the cable unplug timer\n");	
	del_timer_sync(&adapter->synopGMAC_cable_unplug_timer);

	gmacdev->opened = 0;
	
	return -ESYNOPGMACNOERR;

//	TR("%s called \n",__FUNCTION__);
}


static __inline__ int xc3_tso_start_xmit(struct net_device *netdev, struct sk_buff *skb, synopGMACdevice * gmacdev,u32 offload_needed)
{
  	struct iphdr    *iph = ip_hdr(skb);
	struct tcphdr   *th = tcp_hdr(skb);
	//struct iphdr    *iph = skb->nh.iph;
	//struct tcphdr   *th = (struct tcphdr*)(skb->nh.raw + (iph->ihl) * 4);
	unsigned int    doffset = (iph->ihl + th->doff) << 2 ;
	//unsigned int    offset = 0;
	u32             seq = ntohl(th->seq);
	u16             id  = ntohs(iph->id);
	skb_frag_t      *frag;
	unsigned int    frag_index = 0;
	unsigned int    frag_offset = 0;
	int             r;
    struct iphdr    *new_iph;
    struct tcphdr   *new_th;
	unsigned int    frag_size;	
    //unsigned int    partial_sum;

	u32	sh_len = skb_transport_offset(skb) + tcp_hdrlen(skb);	// L2+L3+L4 header, ETH+IP+TCP header
	u32 total_head_len = skb_headlen(skb);		// 	total data length in no-fragment, this maybe larger than gso_size 1514 
	u32 total_data_left = skb->len - sh_len;  	//	Total L4 data len;
	u32 gso_size = skb_shinfo(skb)->gso_size;	// 	normally 1448, plus 66 equal to 1514
	u32 data_size_frame;
	u32 data_size_desc;
	u32 header_offset = 0;
	u32	tb_index_old;
    
    if (unlikely(!skb_is_nonlinear(skb)))
	{
        TR0("TSO: got non-paged(linear) skb, it's impossible!!!\n");
		return -1;
	}

	frag = &skb_shinfo(skb)->frags[frag_index];
	frag_size = skb_frag_size(frag);

	total_head_len -= sh_len;

    TR("transmit tso size=%d, total_head_len=%d\n", skb->len, total_head_len);
	TR("sh_len=%d, gso_size=%d\n", sh_len, gso_size);
	
	do{
		
		data_size_frame = min(total_data_left, gso_size);
		total_data_left -= data_size_frame;

		TR("-------data_size_frame=%d, total_data_left=%d\n", data_size_frame, total_data_left);
		TR("-------TCP seq=0x%x, id=0x%x\n", seq, id);
		
		// Generate the head of L2+L3+L4
		tb_index_old = gmacdev->tso_index + 2;	// plus 2 is to guarant ip header is dword alignment because we have to touch ip header
		
		memcpy((unsigned char *)gmacdev->tso_buf + gmacdev->tso_index + 2, skb->data, sh_len);

		gmacdev->tso_index += (sh_len + 2);

        // update new_iph
        new_iph = (struct iphdr *)((unsigned char *)gmacdev->tso_buf + tb_index_old + ETH_HLEN);
		new_iph->tot_len = htons(data_size_frame + doffset);
		new_iph->id = htons(id);
		TR("new_iph->id=%d\n", ntohs(new_iph->id));

        // update new_th
		new_th = (struct tcphdr*)((unsigned char*)new_iph + ((new_iph->ihl)<<2));
        new_th->seq = htonl(seq);
		new_th->check = 0;
		TR("new_th->seq=%d\n", ntohl(new_th->seq));

		if (total_data_left != 0)
			new_th->fin = new_th->psh = 0;
		
		seq += data_size_frame;
		id++;

		//TR0("total_head_len=%d\n", total_head_len);
		// for data not in fragment
		if (total_head_len > 0)
		{
			unsigned int temp;
			
			temp = min(total_head_len, gso_size);

			TR("total_head_len=%d, temp=%d\n", total_head_len, temp);

			if (header_offset == 0)
				header_offset += sh_len;
			
			memcpy((unsigned char *)gmacdev->tso_buf + gmacdev->tso_index, (void *)(skb->data + header_offset), temp);
			gmacdev->tso_index += temp;

			total_head_len -= temp;
			data_size_frame -= temp;
			header_offset += temp;
		}
		
		//Generate the desc of data
		while(data_size_frame)
		{
			data_size_desc = min(frag_size,data_size_frame);

			TR("frag, data_size_frame=%d, data_size_desc=%d\n", data_size_frame, data_size_desc);
			
			data_size_frame -= data_size_desc;

			memcpy((unsigned char *)gmacdev->tso_buf + gmacdev->tso_index, (void *)((unsigned char *)page_address(frag->page.p) + frag->page_offset + frag_offset), data_size_desc);
			gmacdev->tso_index += data_size_desc;
			
			frag_size -= data_size_desc;
			frag_offset += data_size_desc;
			if(frag_size == 0)
			{
				frag_index++; 
				frag = &skb_shinfo(skb)->frags[frag_index];
				frag_size = skb_frag_size(frag);
				frag_offset = 0;
			}
		}

	retry_set_tso:

		r = xc3_set_tx_qptr(gmacdev, (u32)gmacdev->tso_buf_p + tb_index_old, gmacdev->tso_index - tb_index_old, (total_data_left==0)?(u32)skb:0, offload_needed,DescTxCisTcpPseudoCs,1, 1);
		if (unlikely(r < 0))
		{
			#ifndef TR_INTR_ENABLE
				xc3_release_tx_qptr(netdev, gmacdev);
				TR("tso, no transmit descriptor\n");
				goto retry_set_tso;
			#else
				TR("Impossible,no desc for tso.\n");
			#endif
		}

		gmacdev->tso_index += (gmacdev->tso_index%4) ? (4 - gmacdev->tso_index%4) : 0;
		gmacdev->tso_index = (gmacdev->tso_index + 1514 >= TSO_BUF_SIZE) ? 0 : gmacdev->tso_index;
		
	}while(total_data_left>0);

	return 0;
}

void xc3_release_tx_qptr(struct net_device *netdev, synopGMACdevice *gmacdev)
{
	synopGMACPciNetworkAdapter *adapter;
	s32 desc_index;
	s32 status = 0;
	u32 dma_addr;
	u32 length1;
	u32 data1;

	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	
	/*Release the transmit Descriptors*/
	do {
		desc_index = xc3_get_tx_qptr(gmacdev, &status, &dma_addr, &length1, &data1);
		if(desc_index >= 0 && data1 != 0){
			TR("Finished Transmit at Tx Descriptor %d for skb 0x%08x and buffer = %08x whose status is %08x \n", desc_index,data1,dma_addr,status);
			#ifdef	IPC_OFFLOAD
			if(synopGMAC_is_tx_ipv4header_checksum_error(gmacdev, status)){
				TR("Harware Failed to Insert IPV4 Header Checksum\n");
			}
			if(synopGMAC_is_tx_payload_checksum_error(gmacdev, status)){
				TR("Harware Failed to Insert Payload Checksum\n");
			}
			#endif
			
			dev_kfree_skb_any((struct sk_buff *)data1);
			if(synopGMAC_is_desc_valid(status)){
				adapter->synopGMACNetStats.tx_bytes += length1;
				adapter->synopGMACNetStats.tx_packets++;
			}
			else {	
				TR("Error in Status %08x\n",status);
				adapter->synopGMACNetStats.tx_errors++;
				adapter->synopGMACNetStats.tx_aborted_errors += synopGMAC_is_tx_aborted(status);
				adapter->synopGMACNetStats.tx_carrier_errors += synopGMAC_is_tx_carrier_error(status);
			}
		}	
		
		adapter->synopGMACNetStats.collisions += synopGMAC_get_tx_collision_count(status);
	} while(desc_index >= 0);
}

#ifdef DEBUG
enum {
    S1K = 0,
    S2K,
    S4K,
    S8K,
    S16K,
    S32K,
    S64K,
    S128K,
    SUP128K,
    STOTAL,
    SMAXK,
};
unsigned int skb_xmit_buf_n[SMAXK];
#endif
/**
 * Function to transmit a given packet on the wire.
 * Whenever Linux Kernel has a packet ready to be transmitted, this function is called.
 * The function prepares a packet and prepares the descriptor and 
 * enables/resumes the transmission.
 * @param[in] pointer to sk_buff structure. 
 * @param[in] pointer to net_device structure.
 * \return Returns 0 on success and Error code on failure. 
 * \note structure sk_buff is used to hold packet in Linux networking stacks.
 */
s32 synopGMAC_linux_xmit_frames(struct sk_buff *skb, struct net_device *netdev)
{
	s32 status = 0;
	u32 offload_needed = 0;
	synopGMACPciNetworkAdapter *adapter;
	synopGMACdevice * gmacdev;
	u32 i;
	u32	tb_index_old;

	//TR0("%s called \n",__FUNCTION__);
	if(unlikely(skb == NULL))
	{
		TR0("skb is NULL What happened to Linux Kernel? \n ");
		return -1;
	}
	
	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	if(unlikely(adapter == NULL))
		return -1;

	gmacdev = (synopGMACdevice *) adapter->synopGMACdev;
	if(unlikely(gmacdev == NULL))
		return -1;

    #ifdef DEBUG
	// statistic infomation for skb len
	skb_xmit_buf_n[STOTAL] += skb->len;
	if (skb->len < 1024) 
	    skb_xmit_buf_n[S1K]++;
	else if (skb->len < 2048) 
	    skb_xmit_buf_n[S2K]++;
	else if (skb->len < 4096) 
	    skb_xmit_buf_n[S4K]++;
	else if (skb->len < 8192) 
	    skb_xmit_buf_n[S8K]++;
	else if (skb->len < 16384) 
	    skb_xmit_buf_n[S16K]++;
	else if (skb->len < 32768) 
	    skb_xmit_buf_n[S32K]++;
	else if (skb->len < 65536) 
	    skb_xmit_buf_n[S64K]++;
	else if (skb->len < 131072) 
	    skb_xmit_buf_n[S128K]++;
	else 
	    skb_xmit_buf_n[SUP128K]++;
	#endif
	    
	/*Stop the network queue*/	
	//netif_stop_queue(netdev);
	
	netdev->trans_start = jiffies;

#ifndef TR_INTR_ENABLE	
    xc3_release_tx_qptr(netdev, gmacdev);
#endif

	//if(skb->ip_summed == CHECKSUM_HW){
	if(skb->ip_summed == CHECKSUM_PARTIAL){
		/*	
		   In Linux networking, if kernel indicates skb->ip_summed = CHECKSUM_HW, then only checksum offloading should be performed
		   Make sure that the OS on which this code runs have proper support to enable offloading.
		*/
	
		offload_needed = 0x00000001;
	
		#if 0
		TR0("skb->ip_summed == CHECKSUM_PARTIAL\n");
		{
			int i;
			for(i=0;i<64;i++)
			{
				printk(KERN_CRIT"0x%02x ",*(skb->data + i) );
				if((i & 0xf) == 0xf)
					printk(KERN_CRIT"\n");
			}
			//printk(KERN_CRIT"\n");

		}
		#endif

		#if 0
		printk(KERN_CRIT"skb->ip_summed = CHECKSUM_HW\n");
		printk(KERN_CRIT"skb->h.th=%08x skb->h.th->check=%08x\n",(u32)(skb->h.th),(u32)(skb->h.th->check));
		printk(KERN_CRIT"skb->h.uh=%08x skb->h.uh->check=%08x\n",(u32)(skb->h.uh),(u32)(skb->h.uh->check));
		printk(KERN_CRIT"\n skb->len = %d skb->mac_len = %d skb->data = %08x skb->csum = %08x skb->h.raw = %08x\n",skb->len,skb->mac_len,(u32)(skb->data),skb->csum,(u32)(skb->h.raw));
		printk(KERN_CRIT"DST MAC addr:%02x %02x %02x %02x %02x %02x\n",*(skb->data+0),*(skb->data+1),*(skb->data+2),*(skb->data+3),*(skb->data+4),*(skb->data+5));
		printk(KERN_CRIT"SRC MAC addr:%02x %02x %02x %02x %02x %02x\n",*(skb->data+6),*(skb->data+7),*(skb->data+8),*(skb->data+9),*(skb->data+10),*(skb->data+11));
		printk(KERN_CRIT"Len/type    :%02x %02x\n",*(skb->data+12),*(skb->data+13));
		if(((*(skb->data+14)) & 0xF0) == 0x40){
			int counter;
			printk(KERN_CRIT"IPV4 Header:\n");
			printk(KERN_CRIT"%02x %02x %02x %02x\n",*(skb->data+14),*(skb->data+15),*(skb->data+16),*(skb->data+17));
			printk(KERN_CRIT"%02x %02x %02x %02x\n",*(skb->data+18),*(skb->data+19),*(skb->data+20),*(skb->data+21));
			printk(KERN_CRIT"%02x %02x %02x %02x\n",*(skb->data+22),*(skb->data+23),*(skb->data+24),*(skb->data+25));
			printk(KERN_CRIT"%02x %02x %02x %02x\n",*(skb->data+26),*(skb->data+27),*(skb->data+28),*(skb->data+29));
			printk(KERN_CRIT"%02x %02x %02x %02x\n\n",*(skb->data+30),*(skb->data+31),*(skb->data+32),*(skb->data+33));
			for(counter = 34; counter < skb->len; counter++)
				printk("%02X ",*(skb->data + counter));
		}
		else{
			int counter;
			printk(KERN_CRIT"IPV6 FRAME:\n");
			for(counter = 14; counter < skb->len; counter++)
				printk("%02X ",*(skb->data + counter));
		}
		#endif
	}

 	if(((skb_shinfo(skb)->nr_frags) + 1) > gmacdev->FreeSendDesc)
    {
	    netif_stop_queue(netdev);
		TR("netif_stop_queue because (skb_shinfo(skb)->nr_frags) + 1) > gmacdev->FreeSendDesc\n");
        return NETDEV_TX_BUSY;
	}
			
   	if (skb_is_gso(skb))
   	{
   		#ifdef TR_INTR_ENABLE
		if(((skb_shinfo(skb)->nr_frags)<<1) > gmacdev->FreeSendDesc)
		{
			netif_stop_queue(netdev);
        	return NETDEV_TX_BUSY;
		}
		#endif

        if (gmacdev->FreeSendDesc < 64)
        {
    	    netif_stop_queue(netdev);
			TR("netif_stop_queue because gmacdev->FreeSendDesc < 64\n");
            return NETDEV_TX_BUSY;
        }
		// handle TSO
   	    xc3_tso_start_xmit(netdev, skb, gmacdev,offload_needed);
	}
    else
    {
		//printk("skb_shinfo(skb)->nr_frags=%d\n", skb_shinfo(skb)->nr_frags);
        if(skb_shinfo(skb)->nr_frags == 0)
        {
        	/*Now we have skb ready and OS invoked this function. Lets make our DMA know about this*/
			tb_index_old = gmacdev->tso_index;
			memcpy(gmacdev->tso_buf + gmacdev->tso_index, (void *)skb->data, skb->len);
			gmacdev->tso_index += skb->len;
        }
        else
        {
			TR("skb_shinfo(skb)->nr_frags=%d\n", skb_shinfo(skb)->nr_frags);
			TR("skb->len=%d, skb->data_len=%d\n", skb->len, skb->data_len);
			
       	 	if(((skb_shinfo(skb)->nr_frags) + 1) > gmacdev->FreeSendDesc)
			{
				netif_stop_queue(netdev);
                TR("netif_stop_queue because (skb_shinfo(skb)->nr_frags) + 1) > gmacdev->FreeSendDesc\n");
        		return NETDEV_TX_BUSY;
			}

			tb_index_old = gmacdev->tso_index;
			
            // transmit header
            memcpy(gmacdev->tso_buf + gmacdev->tso_index, (void *)(skb->data), skb->len - skb->data_len);
			gmacdev->tso_index += (skb->len - skb->data_len);
			
        	// transmit data
            for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
            {
                skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

				TR("frag->size=%d\n", frag->size);
        		memcpy(gmacdev->tso_buf + gmacdev->tso_index, ((unsigned char *)page_address(frag->page.p) + frag->page_offset), skb_frag_size(frag));
 				gmacdev->tso_index += frag->size;
            }
        }

		retry_set_desc:
		status = xc3_set_tx_qptr(gmacdev, (u32)gmacdev->tso_buf_p + tb_index_old, gmacdev->tso_index - tb_index_old, (u32)skb, offload_needed,DescTxCisTcpOnlyCs,1, 1);
		if (unlikely(status < 0))
		{
			#ifndef TR_INTR_ENABLE
				xc3_release_tx_qptr(netdev, gmacdev);
				TR("tso, no transmit descriptor\n");
				goto retry_set_desc;
			#else
				TR("Impossible,no desc for tso.\n");
			#endif
		}

		gmacdev->tso_index += (gmacdev->tso_index%4) ? (4 - gmacdev->tso_index%4) : 0;
		gmacdev->tso_index = (gmacdev->tso_index + 1514 >= TSO_BUF_SIZE) ? 0 : gmacdev->tso_index;
    }	

	/*Now force the DMA to start transmission*/	
	synopGMAC_resume_dma_tx(gmacdev);
	
	/*Now start the netdev queue*/
	netif_wake_queue(netdev);
	
	return NETDEV_TX_OK;
}

/**
 * Function provides the network interface statistics.
 * Function is registered to linux get_stats() function. This function is 
 * called whenever ifconfig (in Linux) asks for networkig statistics
 * (for example "ifconfig eth0").
 * @param[in] pointer to net_device structure. 
 * \return Returns pointer to net_device_stats structure.
 * \callgraph
 */
struct net_device_stats *  synopGMAC_linux_get_stats(struct net_device *netdev)
{
TR("%s called \n",__FUNCTION__);
return( &(((synopGMACPciNetworkAdapter *)(netdev_priv(netdev)))->synopGMACNetStats) );
}

/**
 * Function to set multicast and promiscous mode.
 * @param[in] pointer to net_device structure. 
 * \return returns void.
 */


#define GmacFrameFilter 0x04
#define GmacHashHigh    0x08
#define GmacHashLow      0x0C
#define GmacPromiscuousModeOn	0x00000001
#define GmacMulticastFilterOff	0x00000010
#define GmacMcastHashFilterOn   0x00000004

#define GmacFilterOn	0
#define   TRUE      1
#define   FALSE     0
#define LSB        0x1
#define MSB_POLYNOMIAL 0x04C11DB7L    
#define COMPLIMENT     0xFFFFFFFFL
#define CRC_TABLE_SIZE 256
#define CRC32_LEN 4       //Size of the CRC-32 in bytes

typedef union CRC
{

	u32 CRC_32;
	u8 CRC_CHAR[CRC32_LEN];

}CRC_32, *LPCRC_32;


u32  CRC32Table[CRC_TABLE_SIZE]; 
u8  CRCTableCreated=FALSE;

/*
 ** ReflectInteger
 *
 *  PARAMETERS:
 *
 *  DESCRIPTION:
 *  Reflects any given integer (i.e. for a byte, swap bit 0 with bit 7) 
 *
 * RETURNS:
 * Reflected integer
 *
 */
u32 ReflectInteger(u32 Source, char SourceSize)
{
    
	u32 Refvalue = 0;
	u32 i=0;

	for(i = 1; i < (SourceSize+1); i++)
	{
		if (Source & LSB)
		{
			Refvalue |= 1 << (SourceSize - i);
		}  
		Source >>= 1;
	}
	return Refvalue;
}

/*
 ** CreateCRCTable
 *
 *  PARAMETERS:
 *
 *  N/A
 *
 *  DESCRIPTION:
 *  Create a lookup table for all possible polynomial combinatios
 *  of bytes 0->255
 *
 *  RETURNS:
 *
 *  N/A
 */
void CreateCRCTable(void)
{
	u32 i=0;
	u32 j=0;

	for (i=0; i < CRC_TABLE_SIZE; i++)
	{
		CRC32Table[i] = ( ( ReflectInteger(i,8) ) << 24);

	for (j=0; j<8; j++)
	{
		CRC32Table[i] = (CRC32Table[i] << 1) ^ ( (CRC32Table[i] & (1 << 31) )  ? MSB_POLYNOMIAL : 0);
	}
}

  CRCTableCreated = TRUE;

}

/*
 ** CalcFastCRC
 *
 *  PARAMETERS:
 *
 *  Payload....Pointer to data that CRC-32 is calculated over
 *  Payload_Size....Size of data.
 *  InsertCRC...Indicate wether or not caller requires the CRC-32 value to be 
 *  appended to Payload
 *
 *  DESCRIPTION:
 *  This function uses a lookup table to calculate the CRC-32
 *  value for any given payload. Once CRC-32 is calculated,
 *  it is then appended to data.
 *
 *  RETURNS:
 *  CRC-32 as specified by the IEEE 802.3 standard
 */
u32  CalcFastCRC(u8* Payload, u32 Payload_Size)
{
	u32 CRC=COMPLIMENT;
	u8 SourceByte=0;
	u8	CRCByte=0;
	u32 i=0;


	if (!(CRCTableCreated))
	{
		//We need to setup the CRC-32 lookup table
		CreateCRCTable();
	}

	if (Payload_Size)
	{
		for (i=0; i<Payload_Size; i++)
		{
			SourceByte = Payload[i];
			CRCByte = ReflectInteger( (CRC >> 24), 8);

			CRC = (CRC << 8) ^ CRC32Table[CRCByte ^ SourceByte];
		}

		//Compliment CRC as defined inside the 802.3 spec
		// printf("CRC_BEFORE = %x, COMPLIMENT = %x",CRC,COMPLIMENT);   
		CRC ^= COMPLIMENT;
		// printf("CRC_AFTER = %x\n",CRC);   

		CRC >>=26;
	}  
	else
	{
		CRC=0L;
	}

	return(CRC);
}

void synopGMAC_linux_set_multicast_list(struct net_device *netdev)
{
	u32	mc_filter[2];
	u32	data;

    synopGMACPciNetworkAdapter *adapter = NULL;
    synopGMACdevice * gmacdev = NULL;

	if(netdev == NULL)
		return;
    
    adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
    if(adapter == NULL)
	    return;

    gmacdev = adapter->synopGMACdev;
    if(gmacdev == NULL)
	    return;

	if(netdev->flags & IFF_PROMISC)	/* Set promiscuous */
	{
		TR("%s: Promiscuous mode enabled. \n", netdev->name);
		//data = TipsterReadMacReg( tc, GmacFrameFilter);
		data = GmacPromiscuousModeOn;
		synopGMACWriteReg(gmacdev->MacBase, GmacFrameFilter, data);
	}
	else if ((netdev_mc_count(netdev) > 32) || (netdev->flags & IFF_ALLMULTI))
	{
		TR("%s: all multicast mode enabled. \n", netdev->name);
		data = GmacMulticastFilterOff;
		synopGMACWriteReg(gmacdev->MacBase, GmacFrameFilter, data);
	}
	else if (netdev->flags & IFF_MULTICAST)
	{
		struct netdev_hw_addr *ha;

		TR("%s: support multicast. \n", netdev->name);
		
		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, netdev) {
			int bit_nr = CalcFastCRC(ha->addr, 6);

       		TR( "%x %x %x %x %x %x\t%d\n",
					ha->addr[0], ha->addr[1], ha->addr[2],
					ha->addr[3], ha->addr[4], ha->addr[5], bit_nr);
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31 );
		}
		synopGMACWriteReg(gmacdev->MacBase, GmacHashLow, mc_filter[0]);
		synopGMACWriteReg(gmacdev->MacBase, GmacHashHigh, mc_filter[1]);	
		synopGMACWriteReg(gmacdev->MacBase, GmacFrameFilter, GmacMcastHashFilterOn);
	}
	else
	{
		synopGMACWriteReg(gmacdev->MacBase, GmacFrameFilter, GmacFilterOn);
	}
}

/**
 * Function to set ethernet address of the NIC.
 * @param[in] pointer to net_device structure. 
 * @param[in] pointer to an address structure. 
 * \return Returns 0 on success Errorcode on failure.
 */
s32 synopGMAC_linux_set_mac_address(struct net_device *netdev, void * macaddr)
{

synopGMACPciNetworkAdapter *adapter = NULL;
synopGMACdevice * gmacdev = NULL;
struct sockaddr *addr = macaddr;

adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
if(adapter == NULL)
	return -1;

gmacdev = adapter->synopGMACdev;
if(gmacdev == NULL)
	return -1;

if(!is_valid_ether_addr(addr->sa_data))
	return -EADDRNOTAVAIL;

synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, addr->sa_data); 
//synopGMAC_get_mac_addr(synopGMACadapter->synopGMACdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr); 
synopGMAC_get_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, netdev->dev_addr);
 
TR("%s called \n",__FUNCTION__);
return 0;
}

/**
 * Function to change the Maximum Transfer Unit.
 * @param[in] pointer to net_device structure. 
 * @param[in] New value for maximum frame size.
 * \return Returns 0 on success Errorcode on failure.
 */
s32 synopGMAC_linux_change_mtu(struct net_device *netdev, s32 newmtu)
{
	TR("%s called \n",__FUNCTION__);
	netdev->mtu = newmtu;
	return 0;
}

/**
 * IOCTL interface.
 * This function is mainly for debugging purpose.
 * This provides hooks for Register read write, Retrieve descriptor status
 * and Retreiving Device structure information.
 * @param[in] pointer to net_device structure. 
 * @param[in] pointer to ifreq structure.
 * @param[in] ioctl command. 
 * \return Returns 0 on success Error code on failure.
 */
s32 synopGMAC_linux_do_ioctl(struct net_device *netdev, struct ifreq *ifr, s32 cmd)
{
    s32 retval = 0;
    u16 temp_data = 0;
    synopGMACPciNetworkAdapter *adapter = NULL;
    synopGMACdevice * gmacdev = NULL;
	struct mii_ioctl_data *data = if_mii(ifr);
    struct ifr_data_struct
    {
    	u32 unit;
    	u32 addr;
    	u32 data;
    } *req;


    if(netdev == NULL)
    	return -1;
    if(ifr == NULL)
    	return -1;

    req = (struct ifr_data_struct *)ifr->ifr_data;

    adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
    if(adapter == NULL)
    	return -1;

    gmacdev = adapter->synopGMACdev;
    if(gmacdev == NULL)
    	return -1;
    //TR("%s :: on device %s req->unit = %08x req->addr = %08x req->data = %08x cmd = %08x \n",__FUNCTION__,netdev->name,req->unit,req->addr,req->data,cmd);

    switch(cmd)
    {
    	case IOCTL_READ_REGISTER:		//IOCTL for reading IP registers : Read Registers
    		if      (req->unit == 0)	// Read Mac Register
    			req->data = synopGMACReadReg((u32 *)gmacdev->MacBase,req->addr);
    		else if (req->unit == 1)	// Read DMA Register
    			req->data = synopGMACReadReg((u32 *)gmacdev->DmaBase,req->addr);
    		else if (req->unit == 2){	// Read Phy Register
    			//retval = synopGMAC_read_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,&temp_data);
    			retval = synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,&temp_data);
    			req->data = (u32)temp_data;
    			if(retval != -ESYNOPGMACNOERR)
    				TR("ERROR in Phy read\n");	
    		}
    		break;

    	case IOCTL_WRITE_REGISTER:		//IOCTL for reading IP registers : Read Registers
    		if(req->unit == 0)	// Write Mac Register
    			synopGMACWriteReg((u32 *)gmacdev->MacBase,req->addr,req->data);
    		else if (req->unit == 1)	// Write DMA Register
    			synopGMACWriteReg((u32 *)gmacdev->DmaBase,req->addr,req->data);
    		else if (req->unit == 2){	// Write Phy Register
    			//retval = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,req->data);
    			retval = synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,req->addr,req->data);
    			if(retval != -ESYNOPGMACNOERR)
    				TR("ERROR in Phy read\n");	
    		}
    		break;

    	case IOCTL_READ_IPSTRUCT:		//IOCTL for reading GMAC DEVICE IP private structure
    	        memcpy(ifr->ifr_data, gmacdev, sizeof(synopGMACdevice));
    		break;

    	case IOCTL_READ_RXDESC:			//IOCTL for Reading Rx DMA DESCRIPTOR
    		//memcpy(ifr->ifr_data, gmacdev->RxDesc + ((DmaDesc *) (ifr->ifr_data))->data1, sizeof(DmaDesc) );
    		{
#ifdef WE_DONT_HAVE_THIS_ON_ARM
    			DmaDesc * desc = gmacdev->RxDesc + ((DmaDesc *) (ifr->ifr_data))->data1;
    			u32 * pbuf = (u32 *)(ifr->ifr_data);
    			*pbuf ++ = arc_read_uncached_32(&desc->status);
    			*pbuf ++ = arc_read_uncached_32(&desc->length);
    			*pbuf ++ = arc_read_uncached_32(&desc->buffer1);
    			*pbuf ++ = arc_read_uncached_32(&desc->buffer2);
    			*pbuf ++ = arc_read_uncached_32(&desc->data1);
    			*pbuf    = arc_read_uncached_32(&desc->data2);
#else
				panic("Not supported on ARM\n");
#endif
    		}

    		break;

    	case IOCTL_READ_TXDESC:			//IOCTL for Reading Tx DMA DESCRIPTOR
    		//memcpy(ifr->ifr_data, gmacdev->TxDesc + ((DmaDesc *) (ifr->ifr_data))->data1, sizeof(DmaDesc) );
    		{
#ifdef WE_DONT_HAVE_THIS_ON_ARM
    			DmaDesc * desc = gmacdev->TxDesc + ((DmaDesc *) (ifr->ifr_data))->data1;
    			u32 * pbuf = (u32 *)(ifr->ifr_data);
    			*pbuf ++ = arc_read_uncached_32(&desc->status);
    			*pbuf ++ = arc_read_uncached_32(&desc->length);
    			*pbuf ++ = arc_read_uncached_32(&desc->buffer1);
    			*pbuf ++ = arc_read_uncached_32(&desc->buffer2);
    			*pbuf ++ = arc_read_uncached_32(&desc->data1);
    			*pbuf    = arc_read_uncached_32(&desc->data2);
#else
				panic("Not supported on ARM\n");
#endif
    		}

    		break;
    	case IOCTL_POWER_DOWN:
    		if	(req->unit == 1){	//power down the mac
    			TR("============I will Power down the MAC now =============\n");
    			// If it is already in power down don't power down again
    			retval = 0;
    			if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) != GmacPmtPowerDown){
    			synopGMAC_linux_powerdown_mac(gmacdev);			
    			retval = 0;
    			}
    		}
    		if	(req->unit == 2){	//Disable the power down  and wake up the Mac locally
    			TR("============I will Power up the MAC now =============\n");
    			//If already powered down then only try to wake up
    			retval = -1;
    			if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) == GmacPmtPowerDown){
    			synopGMAC_power_down_disable(gmacdev);
    			synopGMAC_linux_powerup_mac(gmacdev);
    			retval = 0;
    			}
    		}
    		break;

		case SIOCGMIIPHY:
			TR("%s:%d\n",__FUNCTION__,__LINE__);
			data->phy_id = gmacdev->PhyBase;
			break;

		case SIOCGMIIREG:
			TR("%s:%d\n",__FUNCTION__,__LINE__);
			retval = synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,data->reg_num & 0x1F,&data->val_out);
			break;

		case SIOCSMIIREG:
			TR("%s:%d\n",__FUNCTION__,__LINE__);
			retval = synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,data->reg_num & 0x1F,data->val_in);
			break;
			
    	default:
    		retval = -1;

    }

    return retval;
}

#ifdef CONFIG_PM
void synopGMAC_pm(int devid, int op)
{
  struct net_device *netdev = g_netdev[devid];
  synopGMACPciNetworkAdapter *adapter = NULL;
  synopGMACdevice * gmacdev = NULL;

  adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
  gmacdev = adapter->synopGMACdev;

	if(op==0) //Sleep
	{
		if(gmacdev->opened)
      	synopGMAC_linux_suspend(netdev);

		if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) != GmacPmtPowerDown){
   			synopGMAC_linux_powerdown_mac(gmacdev);			
 		}
		
	}
	else //Wakeup
	{
		if(((synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus)) & GmacPmtPowerDown) == GmacPmtPowerDown){
				synopGMAC_power_down_disable(gmacdev);
				synopGMAC_linux_powerup_mac(gmacdev);
		}
		if(gmacdev->opened)
				synopGMAC_linux_resume(netdev);
	}
}
#endif

/**
 * Function to handle a Tx Hang.
 * This is a software hook (Linux) to handle transmitter hang if any.
 * We get transmitter hang in the device interrupt status, and is handled
 * in ISR. This function is here as a place holder.
 * @param[in] pointer to net_device structure 
 * \return void.
 */
void synopGMAC_linux_tx_timeout(struct net_device *netdev)
{
TR("%s called \n",__FUNCTION__);
//todo Function not yet implemented
return;
}

static u32 xc3_get_link(struct net_device *netdev)
{
    synopGMACPciNetworkAdapter *adapter;
    synopGMACdevice            *gmacdev;	
    u16     data;
       
	if (synopGMAC_is_MAC_to_MAC())
		return 1;

	TR("xc3_get_link\n");
	adapter = (synopGMACPciNetworkAdapter *)netdev_priv(netdev);
	gmacdev = (synopGMACdevice *)adapter->synopGMACdev;

    if (gmacdev->PhyBase == NO_PHY)
    {   
        // not open yet
        AutoDectectPhy(gmacdev);
    }
    
	synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_STATUS_REG, &data);

	if((data & Mii_Link) == 0){
		TR("No Link %s: %08x\n", netdev->name, data);
		return 0;
	}
	else{
		TR("Link UP %s: %08x\n", netdev->name, data);
		return 1;
	}
}

static int xc3_mdio_read(struct net_device *dev,int phy_id,int location)
{
    int regvalue;
    synopGMACPciNetworkAdapter *adapter = (synopGMACPciNetworkAdapter *)netdev_priv(dev);
    synopGMACdevice *gmacdev = adapter->synopGMACdev;
    
    synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,location, (u16 *)&regvalue);
    return regvalue;
}

static void xc3_mdio_write(struct net_device *dev,int phy_id,int location,int value)
{
    synopGMACPciNetworkAdapter *adapter = (synopGMACPciNetworkAdapter *)netdev_priv(dev);
    synopGMACdevice *gmacdev = adapter->synopGMACdev;
    
    synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,location,value);
}

static void xc3_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
}

static int xc3_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
    synopGMACPciNetworkAdapter *adapter = (synopGMACPciNetworkAdapter *)netdev_priv(dev);
    synopGMACdevice *gmacdev = adapter->synopGMACdev;

    mii_ethtool_gset(&gmacdev->mii,cmd);

    return 0;
}

static int xc3_set_settings(struct net_device *dev, struct ethtool_cmd* cmd)
{
    //unsigned long flags;
    synopGMACPciNetworkAdapter *adapter = (synopGMACPciNetworkAdapter *)netdev_priv(dev);
    synopGMACdevice *gmacdev = adapter->synopGMACdev;

    //TR0("cmd->speed:0x%x\n",cmd->speed);
    //TR0("cmd->duplex:0x%x\n",cmd->duplex);
    //TR0("cmd->port:0x%x\n",cmd->port);
    //TR0("cmd->transceiver:0x%x\n",cmd->transceiver);
    //TR0("cmd->phy_address:0x%x\n",cmd->phy_address);
    //TR0("cmd->autoneg:0x%x\n",cmd->autoneg);
    //TR0("cmd->advertising:0x%x\n",cmd->advertising);

    memcpy(&gmacdev->ecmd,cmd,sizeof(struct ethtool_cmd));   

    
    //local_irq_save(flags);    
	if(gmacdev->opened)
	{
    	synopGMAC_linux_close(dev);
    	synopGMAC_linux_open(dev);
	}
    else
        mii_ethtool_sset(&gmacdev->mii,&gmacdev->ecmd);
    
    //local_irq_restore(flags);    

    return 0;
}

static struct ethtool_ops xc3_ethtool_ops = {
    .get_drvinfo	= xc3_get_drvinfo,
	.get_link		= xc3_get_link,
    .get_settings   = xc3_get_settings,
    .set_settings   = xc3_set_settings,

};

static struct net_device_ops xc3_netdev_ops = {
	.ndo_open   = synopGMAC_linux_open,
	.ndo_stop   = synopGMAC_linux_close,
	.ndo_start_xmit   = synopGMAC_linux_xmit_frames,
	.ndo_get_stats = synopGMAC_linux_get_stats,
	.ndo_set_rx_mode = synopGMAC_linux_set_multicast_list,
	.ndo_set_mac_address  = synopGMAC_linux_set_mac_address,
	.ndo_change_mtu   = synopGMAC_linux_change_mtu,
	.ndo_do_ioctl = synopGMAC_linux_do_ioctl,
	.ndo_tx_timeout = synopGMAC_linux_tx_timeout,
};

/**
 * Function to initialize the Linux network interface.
 * 
 * Linux dependent Network interface is setup here. This provides 
 * an example to handle the network dependent functionality.
 *
 * \return Returns 0 on success and Error code on failure.
 */
s32 __init synopGMAC_init_network_interface(u32 devid)
{
	s32 err;
	struct net_device *netdev;
	synopGMACPciNetworkAdapter *adapter = NULL;
	synopGMACdevice * gmacdev = NULL;
	u8  macAddr1[6] = DEFAULT_MAC_ADDRESS_10;
	u8  macAddr2[6] = DEFAULT_MAC_ADDRESS_2;
	int eth_irq[ETH_NUM]={SYNOP_ETH0_IRQ, SYNOP_ETH1_IRQ};
	u32 phyid=0;
	unsigned long board_id;

	board_id = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + CG_DUMMY_REG1));
	TR("Now Going to Call register_netdev to register the network interface for GMAC core\n");
	/*
	Lets allocate and set up an ethernet device, it takes the sizeof the private structure. This is mandatory as a 32 byte 
	allignment is required for the private data structure.
	*/
	if(devid >= ETH_NUM)
		return -ESYNOPGMACPHYERR;

	netdev = alloc_etherdev(sizeof(synopGMACPciNetworkAdapter));
	if(!netdev){
	    err = -ESYNOPGMACNOMEM;
	    goto err_alloc_etherdev;
	}

	adapter = (synopGMACPciNetworkAdapter *) netdev_priv(netdev);
	memset(adapter,0,sizeof(synopGMACPciNetworkAdapter));
#if 1
	adapter->TxDescBuf = dma_alloc_coherent(NULL, sizeof(DmaDesc)*TRANSMIT_DESC_SIZE,  &(adapter->TxDescBufPhys), GFP_KERNEL);
	adapter->RxDescBuf = dma_alloc_coherent(NULL, sizeof(DmaDesc)*RECEIVE_DESC_SIZE,  &(adapter->RxDescBufPhys), GFP_KERNEL);
#else
	adapter->TxDescBuf =  kzalloc(sizeof(DmaDesc)*TRANSMIT_DESC_SIZE, GFP_KERNEL);
	adapter->RxDescBuf =  kzalloc(sizeof(DmaDesc)*RECEIVE_DESC_SIZE, GFP_KERNEL);
	adapter->TxDescBufPhys = virt_to_phys(adapter->TxDescBuf);
	adapter->RxDescBufPhys = virt_to_phys(adapter->RxDescBuf);
#endif

	if(!adapter->TxDescBuf || !adapter->RxDescBuf)
		return -ENOMEM;
	
	adapter->synopGMACnetdev = netdev;
	g_netdev[devid] = netdev;

	//Called during open ,so don't call them here
	//synopGMAC_attach(psynopGMACDev,(u32) synopGMACMappedAddr + MACBASE,(u32) synopGMACMappedAddr + DMABASE, DEFAULT_PHY_BASE);
	//synopGMAC_reset(psynopGMACDev);
	netdev->irq = eth_irq[devid];
    netdev->base_addr = XC_SOC_PROC_MMREG_BASE;

	if(0 == devid)
	{
		if(mac_addr.sa_data[0] ==0 && mac_addr.sa_data[1] ==0 && mac_addr.sa_data[2] ==0 && mac_addr.sa_data[3] ==0 && mac_addr.sa_data[4] ==0 && mac_addr.sa_data[5] ==0){
	                get_random_bytes(&macAddr1[4],2);
			memcpy( netdev->dev_addr, macAddr1, netdev->addr_len );       /* Set MAC address */
                }
		else
			memcpy( netdev->dev_addr, mac_addr.sa_data, netdev->addr_len );   
	}
	else if (1 == devid)
	{
		if(mac1_addr.sa_data[0] ==0 && mac1_addr.sa_data[1] ==0 && mac1_addr.sa_data[2] ==0 && mac1_addr.sa_data[3] ==0 && mac1_addr.sa_data[4] ==0 && mac1_addr.sa_data[5] ==0){
	                get_random_bytes(&macAddr2[4],2);
			memcpy( netdev->dev_addr, macAddr2, netdev->addr_len );       /* Set MAC address */}
		else
			memcpy( netdev->dev_addr, mac1_addr.sa_data, netdev->addr_len );   
	}

    gmacdev = (synopGMACdevice *)adapter->synopGMACdev;
    synopGMAC_attach(gmacdev, MACBASE, DMABASE, DEFAULT_PHY_BASE, devid);
    spin_lock_init(&gmacdev->lock);
    spin_lock_init(&gmacdev->tx_desc_lock);
    //Init mii interface;
    gmacdev->mii.dev = netdev;
    gmacdev->mii.mdio_read = xc3_mdio_read;
    gmacdev->mii.mdio_write = xc3_mdio_write;
    gmacdev->PhyBase=NO_PHY;
#ifdef SYNOPSYS_GETH_ENABLE
    switch (board_id & 0xFFFF){
        case 0x1100:
        case 0x1101:
        case 0x1102:
        case 0x1103:
        case 0x1104:
        case 0x110d:
        case 0x110e:
        case 0x1140:
        case 0x1141:
        case 0x1150:
        case 0x1151:
        case 0x1152:
            gmacdev->mii.supports_gmii = 0;
            break;

        case 0x1105:
        case 0x1106:
        case 0x1107:
        case 0x1108:
        case 0x1109:
        case 0x1110:
        case 0x1111:
        case 0x1112:
        case 0x1113:
        	gmacdev->mii.supports_gmii = 1;
            break;
        default:
            gmacdev->mii.supports_gmii = 1;
    }
	#else
	gmacdev->mii.supports_gmii = 0;
	#endif	
	
    gmacdev->ecmd.speed = SPEED_100;
    gmacdev->ecmd.duplex = DUPLEX_FULL;
    gmacdev->ecmd.port = PORT_MII;
    gmacdev->ecmd.transceiver = XCVR_INTERNAL;
    gmacdev->ecmd.phy_address = gmacdev->mii.phy_id;
    gmacdev->ecmd.autoneg = AUTONEG_ENABLE;
    gmacdev->ecmd.advertising = ADVERTISED_10baseT_Half|
	ADVERTISED_10baseT_Full |
        ADVERTISED_100baseT_Half;

    if (gmacdev->mii.supports_gmii == 1) {
        gmacdev->ecmd.advertising |= ADVERTISED_100baseT_Full |
            ADVERTISED_1000baseT_Half |	
            ADVERTISED_1000baseT_Full;
    } else {
        gmacdev->ecmd.advertising |= ADVERTISED_100baseT_Full;
    }

    if (synopGMAC_is_MAC_to_MAC()) {		
        gmacdev->ecmd.speed = synopGMAC_is_MAC_to_MAC();
        gmacdev->Speed = gmacdev->ecmd.speed;
        gmacdev->DuplexMode = DUPLEX_FULL;
        synopGMAC_reset(gmacdev);

        if(gmacdev->Speed == SPEED_1000)		
            synopGMAC_select_gmii(gmacdev);
        else
            synopGMAC_select_mii(gmacdev);

        goto skip_phy_init;
    }

    synopGMAC_reset(gmacdev);
    synopGMAC_set_mdc_clk_div(gmacdev,GmiiCsrClk5);
    gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);

    phyid=AutoDectectPhy(gmacdev);
    switch(phyid) {
    case 0x01410dd1:
        //
        // phyid 0x01410dd1 has internal rgnii tx_clk timing delay, we have to disable it on our side
        //
        TR0("disable rgmii tx_clk delay for phy id=0x01410dd1\n");
        writel(readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + ETH_CTRL_REG)) & (~ETH_CTRL_REG_CLK_SHIFT_EN_MASK),
                (unsigned long *)(XC_SOC_PROC_MMREG_BASE + ETH_CTRL_REG));
        break;
    case 0x01410e20:
    case 0x01410e21:
    case 0x01410e22:
    case 0x01410e23:
    case 0x01410e24:
    case 0x01410e25:
    case 0x01410e26:
    case 0x01410e27:
        {
            //
            // phyid 0x01410e20 Marvell 88E3016 has a specific phy control register to set the clock delay
            // Ref. datasheet register 28 bit 11:10
            //
            u32 t = 0;
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1c, (u16 *)&t);
            t = 0x0403;
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1c, (u16)t);
            udelay(100);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase, PHY_CONTROL_REG, (u16 *)&t);
            t |= 0x8000;
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase, PHY_CONTROL_REG, (u16)t);
            udelay(100);
        }
        break;
    case 0x00221555:
        {
            u32 t=0;
            synopGMACWriteReg(gmacdev->DmaBase, DmaBusMode,DmaResetOn);
            mdelay(500);
            t = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacGmiiAddr);
            t &=0x1c;
            t |=0x14;
            synopGMACWriteReg(gmacdev->MacBase, GmacGmiiAddr,t);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_BMCR, (u16 *)&t);

            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0xd, 0x001c);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0xe, 0x000d);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0xd, 0x401c);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0xe, 0x0808);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1f, (u16 *)&t);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1f, 0xa100);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x4, (u16 *)&t);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x4, 0x05e1);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1b, 0x0500);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x0, (u16 *)&t);
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,0x1b, (u16 *)&t);
            udelay(500);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_BMCR, 0x2000);
            udelay(500);
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_BMCR, 0x3000);
        }
        break;
        case 0x001cc916:
            {
                #define MII_RTL_PHYCR1  0x18
                #define MII_RTL_PHYCR2  0x19
                /*
                 * PHY specific control register 2
                 * CLKOUT 125M
                 * RX CLK enable
                 * CLKOUT enable
                 */
                synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase, MII_RTL_PHYCR2, 0x0843);
                /* 
                 * PHY specific control register:
                 *  PHYAD_0 = 0, need rescan the phyid after change.
                 *  MDIX
                 *  Preamble check enable
                 *  Jabber detect enable
                 */
                synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase, MII_RTL_PHYCR1, 0x0018);
                phyid=AutoDectectPhy(gmacdev);
                /*
                 * PHY BMCR
                 * Enable Auto-Negotiation
                 * FULL Duplex
                 * 1000M
                 * Uni-directional enable, allow packet transmit without respect to linkok status
                 */
                synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_BMCR, 0x1160);
                /*
                 * Reset PHY
                 */
                synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_BMCR, 0x8000);
                /* Delay 1ms for reset */
                udelay(1000);
            }
            break;
    default:
        break;
    }

    if (gmacdev->mii.supports_gmii == 1) {
        synopGMAC_select_gmii(gmacdev);
        printk("##################### DISABLE ADVERTISE OF EEE CAPABILITY\n");
        synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_MMD_CTRL, 0x07);//Address access to MMD device address 0x7
        synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_MMD_DATA, 0x3C);//MMD register address data 0x3C (EEE Advertisement Register)
        synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_MMD_CTRL, 0x4007);//Write data access to MMD device address 0x7
        synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_MMD_DATA, 0x00);//MMD register write data 0x00 (Set Advertise Capability for 1000Base-T/100Base-T = 0)
    } else {
        u16 regval;
        synopGMAC_select_mii(gmacdev);
        synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_CTRL1000, &regval);
        synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,MII_CTRL1000,regval & (~(ADVERTISE_1000HALF| ADVERTISE_1000FULL)));
	}

skip_phy_init:
	netdev->watchdog_timeo = 10 * HZ;
	netdev->netdev_ops = &xc3_netdev_ops;
	netdev->ethtool_ops = &xc3_ethtool_ops;

	gmacdev->netdev = netdev;
    //netdev->mtu = 600;

#ifdef	SYNOPSYS_TSO_ENABLE
	gmacdev->tso_index = 0;

	gmacdev->tso_buf = dma_alloc_coherent(NULL, TSO_BUF_SIZE, (dma_addr_t *)&gmacdev->tso_buf_p, GFP_KERNEL);
	if (gmacdev->tso_buf == NULL)
		TR0("Failed to allocate tso buf, size=%d\n", TSO_BUF_SIZE);
    else
		TR0("tso buf at %p, phy at %p size=0x%x\n", gmacdev->tso_buf, gmacdev->tso_buf_p, TSO_BUF_SIZE);
#endif

	/*Now start the network interface*/
	TR("Now Registering the netdevice\n");
	if((err = register_netdev(netdev)) != 0) {
		TR0("Error in Registering netdevice\n");
		return err;
	}  
	
 	return 0;
 	
err_alloc_etherdev:
	TR0("Problem in alloc_etherdev()..Take Necessary action\n");
	return err;
}


/**
 * Function to initialize the Linux network interface.
 * Linux dependent Network interface is setup here. This provides 
 * an example to handle the network dependent functionality.
 * \return Returns 0 on success and Error code on failure.
 */
void __exit synopGMAC_exit_network_interface(void)
{
	int i;
	TR0("Now Calling network_unregister\n");
	//unregister_netdev(synopGMACadapter->synopGMACnetdev);
	for(i=0;i<ETH_NUM;i++)
	{
		if(g_netdev[i]!= 0)
			unregister_netdev(g_netdev[i]);	
	}
}


static int __init xc_parse_mac(char * str,char *mac)
{

	int i, j;
	unsigned char result, value;
	char * macstr = str;
	
	for (i = 0; i < 6; i++) 
	{
		result = 0;

		if (i != 5 && *(macstr + 2) != ':')
			return 1;

		for (j = 0; j < 2; j++) 
		{
			if (isxdigit(*macstr)
			    && (value =
				isdigit(*macstr) ? *macstr -
				'0' : toupper(*macstr) - 'A' + 10) < 16) 
			{
				result = result * 16 + value;
				macstr++;
			} 
			else
				return 1;
		}

		macstr++;
		mac[i] = result;
	}

	return 0;

}
static int __init xc_set_mac(char * str)
{
	if(0 == xc_parse_mac(str,mac_addr.sa_data))
		printk("Set eth0 MAC address to %s\n",str);

	return 1;
}

static int __init xc_set_mac1(char * str)
{
	if(0 == xc_parse_mac(str,mac1_addr.sa_data))
		printk("Set eth1 MAC address to %s\n",str);

	return 1;
}

__setup("ethaddr=", xc_set_mac);
__setup("eth1addr=", xc_set_mac1);

