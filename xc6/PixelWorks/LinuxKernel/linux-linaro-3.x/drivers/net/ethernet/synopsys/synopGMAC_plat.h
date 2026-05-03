/**\file
 *  This file serves as the wrapper for the platform/OS dependent functions
 *  It is needed to modify these functions accordingly based on the platform and the
 *  OS. Whenever the synopsys GMAC driver ported on to different platform, this file
 *  should be handled at most care.
 *  The corresponding function definitions for non-inline functions are available in 
 *  synopGMAC_plat.c file.
 * \internal
 * -------------------------------------REVISION HISTORY---------------------------
 * Synopsys 				01/Aug/2007		 	   Created
 * Stanley                              11/Aug/2008                        Changed
 */
 
 
#ifndef SYNOP_GMAC_PLAT_H
#define SYNOP_GMAC_PLAT_H 1

#define FPGA_BUILD

#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "synopGMAC_Dev.h"


#define TR0(fmt, args...) printk(KERN_CRIT "SynopGMAC: " fmt, ##args)

//#define DEBUG

#ifdef DEBUG
#undef TR
#  define TR(fmt, args...) printk(KERN_CRIT "SynopGMAC: " fmt, ##args)
#else
# define TR(fmt, args...) /* not debugging: nothing */
#endif

#ifdef __arm__
#define flush_descriptors(desc_addr, num)
#else
#define flush_descriptors(desc_addr, num) \
	flush_dcache_range((unsigned int)(desc_addr),(unsigned int)(desc_addr)+ (num)*(sizeof(DmaDesc)));
#endif
#define arc_read_uncached_32(addr)		*(volatile unsigned int *)(addr)
#define arc_write_uncached_32(addr, val)		*(volatile unsigned int *)(addr)=(val)

#define SYNOP_ETH0_IRQ  		XCODE6_IRQ_ETH0
#define SYNOP_ETH1_IRQ  		XCODE6_IRQ_ETH1


#define DEFAULT_DELAY_VARIABLE  10
#define DEFAULT_LOOP_VARIABLE   10000


/* Error Codes */
#define ESYNOPGMACNOERR   0
#define ESYNOPGMACNOMEM   1
#define ESYNOPGMACPHYERR  2
#define ESYNOPGMACBUSY    3


/**
  * These are the wrapper function prototypes for OS/platform related routines
  */ 

void * plat_alloc_memory(u32 );
void   plat_free_memory(void *);

//void * plat_alloc_consistent_dmaable_memory(struct pci_dev *, u32, u32 *);
//void   plat_free_consistent_dmaable_memory (struct pci_dev *, u32, void *, u32);

void * plat_alloc_consistent_dmaable_memory( u32 size,u32 *dmaAddr);
void   plat_free_consistent_dmaable_memory ( u32 size,void * addr);

void   plat_delay(u32);

void  plat_eth_init(void);
void turnon_eth_clk(void);
void turnoff_eth_clk(void);


#endif
