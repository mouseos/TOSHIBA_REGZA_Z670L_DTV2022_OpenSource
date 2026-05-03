#ifndef SYNOP_GMAC_HOST_H
#define SYNOP_GMAC_HOST_H


#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "synopGMAC_plat.h"
#include "synopGMAC_Dev.h"

#define ETH_NUM  2

typedef struct synopGMACAdapterStruct{
#if 0
    DmaDesc TxDescBuf[TRANSMIT_DESC_SIZE];
    DmaDesc RxDescBuf[RECEIVE_DESC_SIZE];
#else
    DmaDesc *TxDescBuf;
    DmaDesc *RxDescBuf;
    dma_addr_t TxDescBufPhys;
    dma_addr_t RxDescBufPhys;
#endif

    /*Device Dependent Data structure*/
    synopGMACdevice synopGMACdev[1];    // static allocation

    /*Os/Platform Dependent Data Structures*/
    struct net_device *synopGMACnetdev;

    struct net_device_stats synopGMACNetStats;
    //u32 synopGMACPciState[16];

    struct timer_list synopGMAC_cable_unplug_timer;

} synopGMACPciNetworkAdapter;

#endif
