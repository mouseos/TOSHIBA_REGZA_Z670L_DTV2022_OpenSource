/*
 * Driver for the ViXS System Inc USB SOC Device.
 *
 * ViXS System Inc. supported the development of this driver.
 * 
 * CODE STATUS HIGHLIGHTS
 *
 * This driver should work well with the Ethernet/RNDIS gadget drivers
 *
 */

/*drivers/
 * Copyright (C) 2007 Alan Tong
 * Copyright (C) 2007-ViXS System Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef	DEBUG		/* messages on error and most fault paths */
#undef	VERBOSE		/* extra debug messages (success too) */

//#define CDC_DEBUG 1

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/if_ether.h>
// For kernel 2.6.19
//#include <linux/usb_ch9.h>
//#include <linux/usb_gadget.h>
// For kernel 2.6.26
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/platform_device.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <linux/types.h>

#include <plat/xcodeRegDef.h>
#include "vixs_usbd.h"


#define	DRIVER_DESC         "ViXS System Inc"
#define	DRIVER_AUTHOR       "Alan Tong"
#define	DRIVER_VERSION      "2008 April 21"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

#define XC_DRV_NAME "xcode-udc"

#ifndef pvoid
typedef void * pvoid;
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define CPU_ARC

/* XXX: lets use Kconfig for better separate later */
#ifdef CPU_ARC
#define CPU_INTERRUPT_MASK  MIPS3_INTERRUPT_MASK
#define CPU_INTERRUPT_USBD_INT_MASK MIPS3_INTERRUPT_USBD_INT_MASK
#define CPU_INTERRUPT MIPS3_INTERRUPT
#define CPU_INTERRUPT_MASK_USBD_INT_MASK MIPS3_INTERRUPT_MASK_USBD_INT_MASK
#else
#ifdef CPU_MIPS
#define CPU_INTERRUPT_MASK  MIPS5_INTERRUPT_MASK
#define CPU_INTERRUPT_USBD_INT_MASK MIPS5_INTERRUPT_USBD_INT_MASK
#define CPU_INTERRUPT MIPS5_INTERRUPT
#define CPU_INTERRUPT_MASK_USBD_INT_MASK MIPS5_INTERRUPT_MASK_USBD_INT_MASK
#endif
#endif


static const char driver_name [] = "xcode-udc";
static const char driver_desc [] = DRIVER_DESC;

static const char ep0name [] = "ep0";
static const char *const ep_name [] = 
{
	ep0name, "ep1in", "ep2out", "ep3in","ep4out","ep5in", "ep6out", "ep7in","ep8out","ep9in","ep10int"

};

static struct vixs	*the_controller;
int vixs_usbd_skip_enum =0;
EXPORT_SYMBOL(vixs_usbd_skip_enum);

#define MMR_READ(reg)               readl( (int)((the_controller)->pmmr + (reg)) )
#define MMR_WRITE(reg, data)        writel( (data), ((the_controller)->pmmr + (reg)) )
#define MMFB_READ(off)              readl( ((the_controller)->pmmfb + (off)) )
#define MMFB_WRITE(off, data)       writel( (data), ((the_controller)->pmmfb + (off)) )

static struct usb_endpoint_descriptor vixs_ep0_desc = 
{
    .bLength =		USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =	USB_DT_ENDPOINT,
    .bEndpointAddress =	0,
    .bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
    .wMaxPacketSize =	USB_MAX_CTRL_PAYLOAD,
};

#define	DIR_STRING(bAddress) (((bAddress) & USB_DIR_IN) ? "in" : "out")


static void vixs_fifo_flush (struct usb_ep *_ep);
static void done (struct vixs_ep *ep, struct vixs_request *req, int status);
static void EP0_Send(struct vixs *dev, u16 length, unsigned int* data);
static void Interrupt_Send(struct vixs *dev,struct vixs_ep *ep, u16 length, unsigned int * data);
static int vixs_remove (struct platform_device *pdev);
void BulkOut_Dpc_Tasklet(u32 Data);
void BulkIn_Dpc_Tasklet(u32 Data);
static void write_fifo (struct vixs_ep *ep, struct usb_request *req);

DECLARE_TASKLET (BulkOut_tasklet, BulkOut_Dpc_Tasklet, 0);
DECLARE_TASKLET (BulkIn_tasklet, BulkIn_Dpc_Tasklet, 0);


//-------------------------------------------------------------------------//

/***************************************************************************
 *
 *  FUNCTION:       Submit_Dpc_Tasklet()
 *
 *  DESCRIPTION:
 *
 *  PARAMETERS:    NONE
 *
 ***************************************************************************/
static void Submit_Dpc_Tasklet(struct vixs *dev, pvoid DpcTasklet)
{
//    PDEBUG("HI HI, I am in Submit_Dpc_Tasklet, dev=0x%x, DpcTasklet=0x%x\n", dev, DpcTasklet);
    tasklet_schedule((struct tasklet_struct *)DpcTasklet);
}

/******************************************************************************
 * Function: BulkOut_Dpc_Tasklet()
 * 
 * Parameters:  OS specific.  None of the parameters are used by the driver.
 *
 * Return:      none
 *
 * Description: OS wrapper used to call the Bulk OUT Timer function
 *
 * Notes:
 ******************************************************************************/
unsigned int out_counter=0;
void BulkOut_Dpc_Tasklet(u32 Data)
{
    struct vixs *dev = Data;
    struct vixs_ep *ep;
    struct vixs_request *req;    
    struct list_head *next_element;
    unsigned int num_of_entry=0;
    unsigned int bytes_received=0;
    unsigned int flags=0;
    unsigned int i=0;
    unsigned char *content=NULL;
    volatile unsigned int desc_space_avaliable=0;
    volatile unsigned int temp=0;
	u32	epidx;

	// 4 Bulk Out EPs in total
	for(epidx= 0; epidx < 4; epidx++)
	{
	    ep = &dev->ep [(epidx+1)*2];

	    temp = MMR_READ(USBD_BULKOUT0_STATUS2 + epidx*0x20);
	    bytes_received = temp & USBD_BULKOUT0_STATUS2_RCVD_BYTES_MASK;
	    num_of_entry = (temp & USBD_BULKOUT0_STATUS2_FINISHED_ENTRIES_MASK)>>(USBD_BULKOUT0_STATUS2_FINISHED_ENTRIES_SHIFT);

		if(!num_of_entry)
			continue;
	//    PDEBUG("HI HI, I am in BulkOut_Dpc_Tasklet now, dev=0x%x, ep=0x%x\n", dev, ep);
	//    PDEBUG("BulkOut_Dpc_Tasklet: %s - Finished Entries=0x%x, Bytes=0x%x,\n", 
	//	   ep->ep.name, num_of_entry, bytes_received);

	    spin_lock_irqsave (&dev->lock, flags);

	    while (num_of_entry)
	    {
	        req = list_entry (ep->queue.next, struct vixs_request, queue);

	        //PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT: buff:%p, actual %d , length %d\n", req->req.buf, req->req.actual, req->req.length);
	        if (req->req.actual == req->req.length)
	        {
	            req->req.actual = bytes_received;
	            ep->num_of_desc_submitted-=1;            
#ifdef CDC_DEBUG
	            MMFB_WRITE(0x9C, dev->num_of_out_desc);
	            MMFB_WRITE(0x8C, MMFB_READ(0x8C)+1);
#endif
	            if ((MMR_READ(USBD_INT_STATUS)&(BULKOUT0_INT_TRANS_ERR_MASK<<epidx)))
	            {
		    //PDEBUG("BulkOut_Dpc_Tasklet: %s - Bytes=0x%x\n", ep->ep.name, bytes_received);
	                temp = MMR_READ(USBD_BULKOUT0_STATUS1+epidx*0x20);
	                while ((temp & USBD_BULKOUT0_STATUS1_FILLED_BYTES_MASK))
	                {
	                    temp = MMR_READ(USBD_BULKOUT0_STATUS1+epidx*0x20);
	                }
	                MMR_WRITE(USBD_SOFT_RST, BULKOUT0_SOFT_RST_MASK<<epidx);
	                temp = MMR_READ(USBD_SOFT_RST);
	                temp &= ~(BULKOUT0_SOFT_RST_MASK<<epidx);
	                MMR_WRITE(USBD_SOFT_RST, temp);
	                MMR_WRITE(USBD_INT_STATUS, (BULKOUT0_INT_TRANS_ERR_MASK<<epidx));
	            }
	            
	//	    PDEBUG("%s: req.buf 0x%08x \n",__func__,  req->req.buf);

	            inv_dcache_range((unsigned long)req->req.buf, (unsigned long)req->req.buf+req->req.length);

	            MMR_WRITE(USBD_BULKOUT0_STATUS2+epidx*0x20, 0);
	            done (ep, req, 0);
	        }
	        else
	        {
	            PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkOut_Dpc_Tasklet: - BULK OUT ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	        }

	        temp = MMR_READ(USBD_BULKOUT0_STATUS2+epidx*0x20);
	        bytes_received = temp & USBD_BULKOUT0_STATUS2_RCVD_BYTES_MASK;
	        num_of_entry = (temp & USBD_BULKOUT0_STATUS2_FINISHED_ENTRIES_MASK)>>(USBD_BULKOUT0_STATUS2_FINISHED_ENTRIES_SHIFT);            

#ifdef CDC_DEBUG
	        MMFB_WRITE(0xA4, MMFB_READ(0xA4)+num_of_entry);
#endif
	        //PDEBUG("BulkOut_Dpc_Tasklet: %s - Finished Entries=0x%x, Bytes=0x%x\n", ep->ep.name, num_of_entry, bytes_received);             
	    }

	    desc_space_avaliable = MMR_READ(USBD_BULKOUT0_STATUS1+epidx*0x20); 

	    // Setup another transfer descriptor 
	    while (!(desc_space_avaliable & USBD_BULKOUT0_STATUS1_DESC_FULL_MASK))
	    {
	        if((!list_empty (&ep->queue))&&(ep->pending_request)&&(ep->num_of_desc_submitted<MAX_TRANS_DESC))
	        {
	            req = list_entry (ep->queue.next, struct vixs_request, queue);
	            next_element = ep->queue.next->next;
	            //PDEBUG("BulkOut_Dpc_Tasklet: %s - Next req=0x%lx, insert=%d\n", ep->ep.name, req, req->insert);
	            while(req->insert==1)
	            {
	                if (!list_empty (&ep->queue))
	                {
	                    req = list_entry (next_element, struct vixs_request, queue);
	                    //PDEBUG("BulkOut_Dpc_Tasklet: %s - Next req=0x%lx, insert=%d\n", ep->ep.name, req, req->insert);
	                    next_element = next_element->next;
	                }
	                else
	                {
	                    // Request List Empty
	                    //PDEBUG("BulkOut_Dpc_Tasklet: - Request List Empty\n");
	                    break;
	                }
	            }
	            //PDEBUG("BulkOut_Dpc_Tasklet: %s - We can setup descriptor for req=0x%lx\n", ep->ep.name, req);                            
	            req->insert = 1;
#ifdef CDC_DEBUG
	            MMFB_WRITE(0x94, MMFB_READ(0x94)-1);
#endif
	            ep->pending_request--;
	            write_fifo(ep, &req->req);
	        }
	        else
	        {
	            //PDEBUG("BulkOut_Dpc_Tasklet: %s - No pending request\n");                            
	            break;
	        }        
	        desc_space_avaliable = MMR_READ(USBD_BULKOUT0_STATUS1+epidx*0x20);         
	    }    
	    //PDEBUG("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n", ep->ep.name);    
	    spin_unlock_irqrestore (&dev->lock, flags);

	}
}


/******************************************************************************
 * Function: BulkIn_Dpc_Tasklet()
 * 
 * Parameters:  OS specific.  None of the parameters are used by the driver.
 *
 * Return:      none
 *
 * Description: OS wrapper used to call the Bulk OUT Timer function
 *
 * Notes:
 ******************************************************************************/
void BulkIn_Dpc_Tasklet(u32 Data)
{
    struct vixs *dev = Data;
    struct vixs_ep *ep;
    struct vixs_request *req;    
    struct list_head *next_element;
    unsigned int num_of_entry=0;
    unsigned int bytes_received=0;
    unsigned int temp=0;
    unsigned int flags=0;
    volatile unsigned int desc_space_avaliable=0;
	u32	epidx;

	//5 Bulk In eps in total
	for(epidx= 0; epidx < 5; epidx++)
	{
	    ep = &dev->ep [(epidx+1)*2 -1];

	    temp = MMR_READ(USBD_BULKIN0_STATUS2 + epidx*0x20);
	    num_of_entry = (temp & USBD_BULKIN0_STATUS2_FINISHED_ENTRIES_MASK)>>(USBD_BULKIN0_STATUS2_FINISHED_ENTRIES_SHIFT);

		if(!num_of_entry)
			continue;
		
#ifdef CDC_DEBUG
	    MMFB_WRITE(0xA0, MMFB_READ(0xA0)+num_of_entry);
#endif
	//    PDEBUG("===>>> HI HI, I am in BulkIn_Dpc_Tasklet now, dev=0x%x, ep=0x%x\n", dev, ep);
	//    PDEBUG("BulkIn_Dpc_Tasklet: %s - Finished Entries=0x%x\n", ep->ep.name, num_of_entry);

	    spin_lock_irqsave (&dev->lock, flags);

	    // process all the finished entry
	    while (num_of_entry)
	    {               
	        req = list_entry (ep->queue.next, struct vixs_request, queue);

	        //PDEBUG("BulkIn_Dpc_Tasklet: %s - Request=0x%x, actual=%d, length=%d\n", ep->ep.name, req, req->req.actual, req->req.length);
	        if (req->req.actual == req->req.length)
	        {
	            ep->num_of_desc_submitted-=1;
#ifdef CDC_DEBUG           
	            MMFB_WRITE(0x98, MMFB_READ(0x98)-1);
	            MMFB_WRITE(0x84, MMFB_READ(0x84)+1);
#endif            
	            MMR_WRITE(USBD_BULKIN0_STATUS2 + epidx*0x20, 0);
	            done (ep, req, 0);
	        }
	        else
	        {
	            PDEBUG("BulkIn_Dpc_Tasklet: - BULK IN ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkIn_Dpc_Tasklet: - BULK IN ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkIn_Dpc_Tasklet: - BULK IN ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkIn_Dpc_Tasklet: - BULK IN ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	            PDEBUG("BulkIn_Dpc_Tasklet: - BULK IN ERROR ERROR ERROR: actual 0x%x != length 0x%x\n", req->req.actual, req->req.length);
	        }

	        num_of_entry --;
	        if (!num_of_entry)
	        {
	            temp = MMR_READ(USBD_BULKIN0_STATUS2 + epidx*0x20);
	            num_of_entry = (temp & USBD_BULKIN0_STATUS2_FINISHED_ENTRIES_MASK)>>(USBD_BULKIN0_STATUS2_FINISHED_ENTRIES_SHIFT);        
#ifdef CDC_DEBUG
	            MMFB_WRITE(0xA0, MMFB_READ(0xA0)+num_of_entry);
#endif
	        }
	        //PDEBUG("BulkIn_Dpc_Tasklet: %s - Finished Entries=0x%x, pending request=%d\n", ep->ep.name, num_of_entry, dev->pending_in_request);
	    }

	    desc_space_avaliable = MMR_READ(USBD_BULKIN0_STATUS1 + epidx*0x20);         

	    // Setup another transfer descriptor 
	    while (!(desc_space_avaliable & USBD_BULKIN0_STATUS1_DESC_FULL_MASK))
	    {
	        if((!list_empty (&ep->queue))&&(ep->pending_request)&&(ep->num_of_desc_submitted<MAX_TRANS_DESC))
	        {
	            req = list_entry (ep->queue.next, struct vixs_request, queue);
	            next_element = ep->queue.next->next;
	            //PDEBUG("BulkIn_Dpc_Tasklet: %s - Next req=0x%lx, insert=%d\n", ep->ep.name, req, req->insert);
	            while(req->insert==1)
	            {
	                if (!list_empty (&ep->queue))
	                {
	                    req = list_entry (next_element, struct vixs_request, queue);
	                    //PDEBUG("BulkIn_Dpc_Tasklet: %s - Next req=0x%lx, insert=%d\n", ep->ep.name, req, req->insert);
	                    next_element = next_element->next;
	                }
	                else
	                {
	                    // Request List Empty
	                    //PDEBUG("BulkIn_Dpc_Tasklet: - Request List Empty\n");
	                    break;
	                }
	            }
	            //PDEBUG("BulkIn_Dpc_Tasklet: %s - We can setup descriptor for req=0x%lx\n", ep->ep.name, req);                            
	            req->insert = 1;
#ifdef CDC_DEBUG
	            MMFB_WRITE(0x90, MMFB_READ(0x90)-1);
#endif
	            ep->pending_request--;
	            write_fifo(ep, &req->req);
	        }
	        else
	        {
	            //PDEBUG("BulkIn_Dpc_Tasklet: %s - No pending request\n");                            
	            break;
	        }        
	        desc_space_avaliable = MMR_READ(USBD_BULKIN0_STATUS1 + epidx*0x20);         
	    }
	    spin_unlock_irqrestore (&dev->lock, flags);
	}
    //PDEBUG("===================================================================\n");
}


//////////////////////////////////////
//
//  ViXS -- EP Reset
//
//////////////////////////////////////
static int vixs_ep_enable (struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
    struct vixs         *dev;
    struct vixs_ep      *ep;
    u32                 max, tmp;
    unsigned long       flags;
    int                 retval = 0;
	u32 	epnum, epidx;
	u32		max_pkt_size = 512;

    ep = container_of (_ep, struct vixs_ep, ep);

    if (!_ep || !desc || ep->desc || _ep->name == ep0name || desc->bDescriptorType != USB_DT_ENDPOINT)
    {
        PDEBUG("vixs_ep_enable -- ERROR01: EP = %s\n", _ep->name);        
        return -EINVAL;
    }

    dev = ep->dev;

    if (!dev->driver || (dev->gadget.speed == USB_SPEED_UNKNOWN))
    {
        PDEBUG("vixs_ep_enable -- ERROR02: EP = %s\n", _ep->name);
        return -ESHUTDOWN;
    }

    max = le16_to_cpu (desc->wMaxPacketSize);

    spin_lock_irqsave (&dev->lock, flags);
    ep->ep.maxpacket = max;
    ep->desc = desc;
    ep->stopped = 0;

	PDEBUG("Enable Endpoint %x, desc->bmAttributes: 0x%x\n", desc->bEndpointAddress, desc->bmAttributes);
	
    // Assume the max packet size from gadget is always correct
    switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
    {
        case USB_ENDPOINT_XFER_INT:
//            if (desc->bEndpointAddress == VIXS_INTERRUPT_IN_ADDR)
            {

				// Configure the Core	 
				// 3:0	 - Endpoint Number
				// 4	  - 0:OUT, 1:IN
				// 6:5	 - 10:Bulk, 11:Interrupt
				// 10:7 - Configuration Number
				// 14:11 - Interface Number
				// 18:15 - Alternate setting
				// 29:19 - Maximum packet size
				// 31:30 - Reserved for non-isochronous IN endpoint
				tmp = (0x0F&desc->bEndpointAddress)|(1<<4)|(3<<5)|(dev->mCurConfiguration<<7)|(dev->mCurInterface<<11)|(dev->mCurAltSetting<<15)|(64<<19);
				writel(tmp, USBD_EP0_WR_DATA + VIXS_MMR_BASE);
				tmp = (0x8 + (0x0F&desc->bEndpointAddress)*4) << REG_ADDR_SHIFT;
				tmp |= 1 << REG_WOP_SHIFT;
				tmp |= 0 << REG_CSR_SEL_SHIFT;
				tmp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;
				writel(tmp, USBD_EP0_CTRL + VIXS_MMR_BASE);

				
				tmp = MMR_READ(USBD_EP0_STATUS); 
				while (tmp & OP_BUSY_MASK)
				{
					tmp = MMR_READ(USBD_EP0_STATUS);			  
					cpu_relax();
				}

                // Enable Interrupt IN Soft Reset
                MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~INTIN0_SOFT_RST_MASK);
			
                // Enable Interrupt In
                tmp = MMR_READ(USBD_INTIN0_CTRL);
                tmp |= USBD_INTIN0_CTRL_ENABLE_MASK;
                tmp |= (0x0F&desc->bEndpointAddress) << USBD_INTIN0_CTRL_EP_NUM_SHIFT;
                tmp |= dev->mCurAltSetting<< USBD_INTIN0_CTRL_ALTERNATE_SHIFT;
                tmp |= dev->mCurInterface<< USBD_INTIN0_CTRL_INTERFACE_SHIFT;
                tmp |= dev->mCurConfiguration<< USBD_INTIN0_CTRL_CONFIG_SHIFT;
                MMR_WRITE(USBD_INTIN0_CTRL, tmp);
                PDEBUG("Vixs_ep_enable:(Interrupt In 0x%lx) %s - max size 0x%lx\n", MMR_READ(USBD_INTIN0_CTRL), _ep->name, max);

				// Enable the interrupt mask for this endpoint
				tmp = MMR_READ(USBD_MIPS_INT_MASK);
				tmp |= (MIPS_INTIN0_INT_EN_EMPTY_MASK);
				MMR_WRITE(USBD_MIPS_INT_MASK, tmp);

				/* JDEBUG */
				dump_stack();
            }
            break;
        case USB_ENDPOINT_XFER_BULK:

			if(dev->gadget.speed == USB_SPEED_HIGH)
			{
				max_pkt_size = 512;
			}
			else
			{
				max_pkt_size = 64;
			}
			
            if (desc->bEndpointAddress & 0x80)
            {
            	epnum = desc->bEndpointAddress&0xf;
				epidx = (epnum-1)/2;


				// Configure the Core	 
				// 3:0	 - Endpoint Number
				// 4	  - 0:OUT, 1:IN
				// 6:5	 - 10:Bulk, 11:Interrupt
				// 10:7 - Configuration Number
				// 14:11 - Interface Number
				// 18:15 - Alternate setting
				// 29:19 - Maximum packet size
				// 31:30 - Reserved for non-isochronous IN endpoint
				tmp = (0x0F&desc->bEndpointAddress)|(1<<4)|(2<<5)|(dev->mCurConfiguration<<7)|(dev->mCurInterface<<11)|(dev->mCurAltSetting<<15)|(max_pkt_size<<19);
				writel(tmp, USBD_EP0_WR_DATA + VIXS_MMR_BASE);
				tmp = (0x8 + (0x0F&desc->bEndpointAddress)*4) << REG_ADDR_SHIFT;
				tmp |= 1 << REG_WOP_SHIFT;
				tmp |= 0 << REG_CSR_SEL_SHIFT;
				tmp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;
				writel(tmp, USBD_EP0_CTRL + VIXS_MMR_BASE);

				
				tmp = MMR_READ(USBD_EP0_STATUS); 
				while (tmp & OP_BUSY_MASK)
				{
					tmp = MMR_READ(USBD_EP0_STATUS);			  
					cpu_relax();
				}

				
                // Enable Bulk In Soft Reset
                MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKIN0_SOFT_RST_MASK<<epidx));

                // Enable Bulk In
                tmp = USBD_BULKIN0_CTRL_ENABLE_MASK;
                tmp |= (epnum) << USBD_BULKIN0_CTRL_EP_NUM_SHIFT;
                tmp |= dev->mCurAltSetting << USBD_BULKIN0_CTRL_ALTERNATE_SHIFT;
                tmp |= dev->mCurInterface<< USBD_BULKIN0_CTRL_INTERFACE_SHIFT;
                tmp |= dev->mCurConfiguration<< USBD_BULKIN0_CTRL_CONFIG_SHIFT;
		        tmp |= max_pkt_size << USBD_BULKIN0_CTRL_PKT_SIZE_SHIFT;
                MMR_WRITE(USBD_BULKIN0_CTRL+epidx*0x20, tmp);

				// Enable the interrupt mask for this endpoint
				tmp = MMR_READ(USBD_MIPS_INT_MASK);
				tmp |= (MIPS_BULKIN0_INT_EN_TRANS_DONE_MASK<<epidx);
				MMR_WRITE(USBD_MIPS_INT_MASK, tmp);
				
//                PDEBUG("Vixs_ep_enable:(Bulk In) %s - max size 0x%lx\n", _ep->name, max);
            }
            else if (!(desc->bEndpointAddress&0x80))
            {
            	epnum = desc->bEndpointAddress&0xf;
				epidx = (epnum-1)/2;
                if(vixs_usbd_skip_enum)
                    epidx =0;
				// Configure the Core	 
				// 3:0	 - Endpoint Number
				// 4	  - 0:OUT, 1:IN
				// 6:5	 - 10:Bulk, 11:Interrupt
				// 10:7 - Configuration Number
				// 14:11 - Interface Number
				// 18:15 - Alternate setting
				// 29:19 - Maximum packet size
				// 31:30 - Reserved for non-isochronous IN endpoint
				tmp = (0x0F&desc->bEndpointAddress)|(0<<4)|(2<<5)|(dev->mCurConfiguration<<7)|(dev->mCurInterface<<11)|(dev->mCurAltSetting<<15)|(max_pkt_size<<19);
				writel(tmp, USBD_EP0_WR_DATA + VIXS_MMR_BASE);
				tmp = (0x8 + (0x0F&desc->bEndpointAddress)*4) << REG_ADDR_SHIFT;
				tmp |= 1 << REG_WOP_SHIFT;
				tmp |= 0 << REG_CSR_SEL_SHIFT;
				tmp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;
				writel(tmp, USBD_EP0_CTRL + VIXS_MMR_BASE);

				
				tmp = MMR_READ(USBD_EP0_STATUS); 
				while (tmp & OP_BUSY_MASK)
				{
					tmp = MMR_READ(USBD_EP0_STATUS);			  
					cpu_relax();
				}

			

                // Enable Bulk Out Soft Reset
                MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKOUT0_SOFT_RST_MASK<<epidx));
            
                // Enable Bulk Out
                tmp = USBD_BULKOUT0_CTRL_ENABLE_MASK;
                tmp |= (0x0F & desc->bEndpointAddress) << USBD_BULKOUT0_CTRL_EP_NUM_SHIFT;
                tmp |= dev->mCurAltSetting << USBD_BULKOUT0_CTRL_ALTERNATE_SHIFT;
                tmp |= dev->mCurInterface<< USBD_BULKOUT0_CTRL_INTERFACE_SHIFT;
                tmp |= dev->mCurConfiguration<< USBD_BULKOUT0_CTRL_CONFIG_SHIFT;
		        tmp |= max_pkt_size << USBD_BULKIN0_CTRL_PKT_SIZE_SHIFT;
                MMR_WRITE(USBD_BULKOUT0_CTRL + epidx*0x20, tmp);

				// Enable the interrupt mask for this endpoint
				tmp = MMR_READ(USBD_MIPS_INT_MASK);
				tmp |= (MIPS_BULKOUT0_INT_EN_TRANS_DONE_MASK<<epidx);
				MMR_WRITE(USBD_MIPS_INT_MASK, tmp);
				
//                PDEBUG("Vixs_ep_enable:(Bulk Out) %s - max size 0x%lx\n", _ep->name, max);
            }
            break;
        case USB_ENDPOINT_XFER_CONTROL:
            break;
        case USB_ENDPOINT_XFER_ISOC:
            break;
        default:
            goto en_done;
    }

    PDEBUG ("Vixs_ep_enable: EP Name = %s : EP Addr = 0x%lx : Dir = %s : max packet %d\n",
            ep->ep.name, ep->desc->bEndpointAddress,
            (desc->bEndpointAddress & USB_DIR_IN)?"in":"out",
            max);

if(vixs_usbd_skip_enum)
    vixs_fifo_flush (ep);

    spin_unlock_irqrestore (&dev->lock, flags);

en_done:    
    return retval;
}


static const struct usb_ep_ops vixs_ep_ops;


//////////////////////////////////////
//
//  ViXS -- Reset Endpoint
//
//////////////////////////////////////
static void ep_reset (struct vixs_ep *ep)
{
	u32 	epnum, epidx;


    if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    {
        // Reset Interrupt IN 
        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|INTIN0_SOFT_RST_MASK);
        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~INTIN0_SOFT_RST_MASK);

    }
    else if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
    {
		epnum = ep->desc->bEndpointAddress&0xf;
		epidx = (epnum-1)/2;

		if(ep->desc->bEndpointAddress & 0x80)	//bulk in
		{
			// Reset Bulk IN 
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|(BULKIN0_SOFT_RST_MASK<<epidx));
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKIN0_SOFT_RST_MASK<<epidx));
		}
		else
		{
	        // Reset Bulk OUT 
	        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|(BULKOUT0_SOFT_RST_MASK<<epidx));
	        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKOUT0_SOFT_RST_MASK<<epidx));
		}
    }

    ep->desc = NULL;
    INIT_LIST_HEAD (&ep->queue);

    ep->ep.maxpacket = ~0;
    ep->ep.ops = &vixs_ep_ops;
    udelay(2000);
}


//////////////////////////////////////
//
//  ViXS -- Nuke
//
//////////////////////////////////////
static void nuke (struct vixs_ep *ep)
{
    struct vixs_request	*req;

    PDEBUG("nuke = %s\n", ep->ep.name);

    // called with spinlock held
    ep->stopped = 1;

    // Flush FIFO
    vixs_fifo_flush(&ep->ep);

    // Whether this ep has request linked
    while (!list_empty (&ep->queue)) 
    {
        req = list_entry (ep->queue.next, struct vixs_request, queue);
        done (ep, req, -ESHUTDOWN);
    }
}


//////////////////////////////////////
//
//  ViXS -- Disable
//
//////////////////////////////////////
static int vixs_ep_disable (struct usb_ep *_ep)
{
    struct vixs_ep	*ep;
    unsigned long		flags;

    ep = container_of (_ep, struct vixs_ep, ep);

    if (!_ep || !ep->desc || _ep->name == ep0name)
        return -EINVAL;

    PDEBUG("vixs_ep_disable: EP = %s\n", _ep->name);

    if ((ep->desc->bEndpointAddress&0x80)&&((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))
    {
        // Disable Bulk IN 
        //MMR_WRITE(USBD_BULKIN0_CTRL, MMR_READ(USBD_BULKIN0_CTRL)&~USBD_BULKIN0_CTRL_ENABLE_MASK);
        //PDEBUG("Vixs_ep_disable:(Bulk In 0x%lx) %s\n", MMR_READ(USBD_INTIN0_CTRL), _ep->name);
    }
    else if ((!(ep->desc->bEndpointAddress & 0x80))&&((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))
    {
        // Disable Bulk OUT 
        //MMR_WRITE(USBD_BULKOUT0_CTRL, MMR_READ(USBD_BULKOUT0_CTRL)&~USBD_BULKOUT0_CTRL_ENABLE_MASK);
        //PDEBUG("Vixs_ep_disable:(Bulk Out 0x%lx) %s\n", MMR_READ(USBD_INTIN0_CTRL), _ep->name);
    }
    else if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    {
        // Disable Interrupt IN 
        //MMR_WRITE(USBD_INTIN0_CTRL, MMR_READ(USBD_INTIN0_CTRL)&~USBD_INTIN0_CTRL_ENABLE_MASK);
        //PDEBUG("Vixs_ep_disable:(Interrupt In 0x%lx) %s\n", MMR_READ(USBD_INTIN0_CTRL), _ep->name);
    }

    spin_lock_irqsave (&ep->dev->lock, flags);
    //nuke (ep);
    //ep_reset (ep);

    ep->desc = 0;
    ep->stopped = 1;

    spin_unlock_irqrestore (&ep->dev->lock, flags);
    return 0;
}

//-------------------------------------------------------------------------//

//////////////////////////////////////
//
//  ViXS -- Allocate Request
//
//////////////////////////////////////
static struct usb_request *vixs_alloc_request (struct usb_ep *_ep, gfp_t gfp_flags)
{
    struct vixs_ep	*ep;
    struct vixs_request	*req = NULL;

    ep = container_of (_ep, struct vixs_ep, ep);

    req = kzalloc(sizeof(*req), gfp_flags);
    if (!req)
    {
        PDEBUG("Vixs_alloc_request: ERROR -- EP %s\n", _ep->name);
        return NULL;
    }

    if (ep)
    {
        PDEBUG("Vixs_alloc_request $$$: %s, Req=0x%lx, Req size=0x%lx\n", _ep->name, req, sizeof *req);
    }
    else
    {
        PDEBUG("Vixs_alloc_request: _ep = NULL\n");
    }

    req->req.dma = DMA_ADDR_INVALID;
    req->insert = 0;
    INIT_LIST_HEAD (&req->queue);

    return &req->req;
}

//////////////////////////////////////
//
//  ViXS -- Free Request
//
//////////////////////////////////////
static void vixs_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
    struct vixs_ep	*ep;
    struct vixs_request	*req;

    ep = container_of (_ep, struct vixs_ep, ep);
    if (!_ep || !_req)
        return;

    req = container_of (_req, struct vixs_request, req);

    WARN_ON (!list_empty (&req->queue));

    if (ep)
    {
        PDEBUG("Vixs_free_request: EP Name = %s : Req = 0x%lx\n", ep->ep.name, req);
    }
    else
    {
        PDEBUG("Vixs_free_request: _ep = NULL\n");
    }
    
    if (_req)
    {   
        kfree (req);
    }
}

//-------------------------------------------------------------------------//

//////////////////////////////////////
//
//  ViXS -- Allocate Buffer
//
//////////////////////////////////////
static void * vixs_alloc_buffer (struct usb_ep	*_ep, unsigned bytes, dma_addr_t *dma, gfp_t gfp_flags)
{
    void			*retval;
    struct vixs_ep	*ep;

    ep = container_of (_ep, struct vixs_ep, ep);
    if ((!_ep)||(!bytes))
    {
        return NULL;
    }

    PDEBUG("Vixs_alloc_buffer: size = %d -- Enter\n", bytes);
    retval = kmalloc(bytes, gfp_flags);
    if (retval)
    {
        *dma = virt_to_phys(retval);
    }
    PDEBUG("Vixs_alloc_buffer: size = %d -- Leave\n", bytes);
    return retval;
}

static LIST_HEAD(buffers);


//////////////////////////////////////
//
//  ViXS -- Free Buffer
//
//////////////////////////////////////
static void vixs_free_buffer (struct usb_ep *_ep, void *address, dma_addr_t dma, unsigned bytes) 
{
    // free memory into the right allocator //
    if (address)
    {
        PDEBUG("Vixs_free_buffer: -- Addr = 0x%x\n", address);
        kfree (address);
    }
}

//-------------------------------------------------------------------------//
//////////////////////////////////////////////////////
//
//      ViXS - Write FIFO
//      *** load a packet into the FIFO we use for usb IN transfers.
//
//////////////////////////////////////////////////////

static void write_fifo (struct vixs_ep *ep, struct usb_request *req)
{
    struct vixs *dev;
    u8          *buf;
    u32         i=0;
    u32         tmp;
    unsigned    count, total, buf_pos=0;
    void        *phy_addr_buf;
    //u32         tmp_buf_idx=0;
    u32         num_of_entry=0, temp=0;
    struct vixs_request *vixs_req;
	u32 epnum, epidx;

    // usually actual is equal to zero
    if (req)
    {
        if (req->actual)
        {
            PDEBUG("Write Fifo: %s : actual = %d\n", ep->name, req->actual);
        }

        buf = req->buf + req->actual;
        total = req->length - req->actual;
    } 
    else 
    {
        total = 0;
        buf = NULL;
    }

    dev = ep->dev;



	if((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)//bulk transfer
	{
		epnum = ep->desc->bEndpointAddress&0xf;
		epidx = (epnum-1)/2;

	    if (ep->desc->bEndpointAddress & 0x80)
	    {
	        count = total;
	        //PDEBUG("Write Fifo: %s : Req=0x%x, Data size=%d, Length=%d, Actual=%d\n", ep->ep.name, req, count, req->length, req->actual);
	        //flush_dcache_range((unsigned long)req->buf, (unsigned long)req->buf+req->length);
	        
	        // Bulk IN 
	        while (count)
	        {
	            // Only fill in descriptor when the list is not full
	            tmp = MMR_READ(USBD_BULKIN0_STATUS1 + epidx*0x20);         
	            while ((tmp & USBD_BULKIN0_STATUS1_DESC_FULL_MASK))
	            {
	                i++;
	                tmp = MMR_READ(USBD_BULKIN0_STATUS1 + epidx*0x20);
	                cpu_relax();
	            }

	            ep->num_of_desc_submitted+=1;            
#ifdef CDC_DEBUG
	            MMFB_WRITE(0x98, MMFB_READ(0x98)+1);
	            MMFB_WRITE(0x80, MMFB_READ(0x80)+1);            
#endif
	            phy_addr_buf = (void *)virt_to_phys(req->buf);

	            if (ep->num_of_desc_submitted > MAX_TRANS_DESC)
	            {
	                PDEBUG("ERROR ERROR ERROR IN --- dev->num_of_in_desc=%d, length=%d, actual=%d\n", ep->num_of_desc_submitted, req->length, req->actual);
	            }
	            
	            MMR_WRITE(USBD_BULKIN0_DESC_FIFO_ADR_WR + epidx*0x20, (unsigned int)((unsigned char *)phy_addr_buf+buf_pos));
	            
	            if (count < MAX_TRANSFER_BYTES)
	            {
	                tmp = count;
	                count -= tmp;
	                buf_pos += tmp;                
	                req->actual += tmp;
	            }
	            else
	            {
	                tmp = MAX_TRANSFER_BYTES;
	                count -= tmp;
	                buf_pos += tmp; 
	                req->actual += tmp;
	            }
	            //PDEBUG("Write Fifo: %s : Receive %d bytes, bytes left = %d\n", ep->name, tmp, count);            


	            // Trigger interrupt to the host for the last transaction of this request
	            if (!count)
	            {
	                //PDEBUG("Write Fifo: %s : Trigger Interrupt: bytes %d\n", ep->name, tmp);
	                tmp |= USBD_BULKIN0_DESC_FIFO_LOAD_INT_EN_MASK;
	            }
	            
	            //PDEBUG("Write Fifo: %s : Req=0x%x, Data size=%d, Length=%d, Actual=%d\n", ep->name, req, count, req->length, req->actual);
	            MMR_WRITE(USBD_BULKIN0_DESC_FIFO_LOAD + epidx*0x20, tmp);

	        }
	    }
	    else if (!(ep->desc->bEndpointAddress & 0x80))
	    {
	        //inv_dcache_range((unsigned long)req->buf, (unsigned long)req->buf+req->length);

	        count = total;
	        PDEBUG("Write Fifo (Enter): %s : Data size=%d, Length=%d, Actual=%d\n", ep->ep.name, count, req->length, req->actual);
            if(vixs_usbd_skip_enum)
    	        epidx=0; 
	        // Bulk OUT
	        while (count)
	        {
	            // Only fill in descriptor when the list is not full
	            tmp = MMR_READ(USBD_BULKOUT0_STATUS1 + epidx*0x20);         
	            while ((tmp & USBD_BULKOUT0_STATUS1_DESC_FULL_MASK))
	            {
	                PDEBUG("BULK OUT -- FULL\n");
	                tmp = MMR_READ(USBD_BULKOUT0_STATUS1 + epidx*0x20);
	                cpu_relax();
	            }

	            tmp = phy_addr_buf = (void *)virt_to_phys(req->buf);
	            //PDEBUG("Write Fifo: %s : Phy addr = 0x%lx\n", ep->name, phy_addr_buf);
	            MMR_WRITE(USBD_BULKOUT0_DESC_FIFO_ADR_WR + epidx*0x20, (u32)phy_addr_buf);
	            //PDEBUG("Write Fifo: %s : USBD_BULKOUT0_DESC_FIFO_ADR_WR = 0x%lx\n", ep->name, MMR_READ(USBD_BULKOUT0_DESC_FIFO_ADR_WR));

	            if (count < MAX_TRANSFER_BYTES)
	            {
	                tmp = count;
	                count -= tmp;
	                buf_pos += tmp;                
	                req->actual += tmp;
	            }
	            else
	            {
	                tmp = MAX_TRANSFER_BYTES;
	                count -= tmp;
	                buf_pos += tmp; 
	                req->actual += tmp;
	            }

	            //PDEBUG("Write Fifo: %s : Req=0x%x, Data size=%d, Length=%d, Actual=%d, temp=%d\n", ep->name, req, count, req->length, req->actual, tmp);

	            // Trigger interrupt to the host for the last transaction of this request
	            if (!count)
	            {
	                //PDEBUG("Write Fifo: %s : Trigger Interrupt: bytes %d\n", ep->name, tmp);
	                tmp |= USBD_BULKOUT0_DESC_FIFO_LOAD_INT_EN_MASK;
	            }

	            //PDEBUG("Write Fifo (Exit): %s : Req=0x%x, Data size=%d, Length=%d, Actual=%d\n", ep->name, req, count, req->length, req->actual);            
	            MMR_WRITE(USBD_BULKOUT0_DESC_FIFO_LOAD + epidx*0x20, tmp);
	            ep->num_of_desc_submitted+=1;
	            if (ep->num_of_desc_submitted > MAX_TRANS_DESC)
	            {
	                PDEBUG("ERROR ERROR ERROR OUT --- dev->num_of_out_desc=%d, length=%d, actual=%d\n", ep->num_of_desc_submitted, req->length, req->actual);
	            }            
#ifdef CDC_DEBUG            
	            MMFB_WRITE(0x9C, ep->num_of_desc_submitted);            
	            MMFB_WRITE(0x88, MMFB_READ(0x88)+1);
#endif            
	        }
	    }
	}
    else if (ep->desc->bEndpointAddress == 0x0)	//control
    {
        // Control IN 
        EP0_Send(dev, total, req->buf);
        req->actual = total;
    }
    else if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    {
        // Interrupt IN
	    printk("ep->desc->bEndpointAddress %d\n", 
		   ep->desc->bEndpointAddress);
        Interrupt_Send(dev, ep, total, req->buf);
        req->actual = total;
    }
	
}


//////////////////////////////////////////////////////////////////////////
//
//  ViXS -- Done
//  done() - retire a request; caller blocked irqs
//  @status : request status to be set, only works when request is still in progress.
//
//////////////////////////////////////////////////////////////////////////
static void done (struct vixs_ep *ep, struct vixs_request *req, int status)
{
    struct vixs     *dev = NULL;
    unsigned char   stopped = ep->stopped;

    dev = (struct vixs*)ep->dev;

    // Removed the request from vixs_ep->queue
    list_del_init (&req->queue);

    // req.status should be set as -EINPROGRESS in ep_queue() 
    if (req->req.status == -EINPROGRESS)
    {  
        req->req.status = status;
    }
    else
    {  
        status = req->req.status;
    }

    if (req->mapped) 
    {
        dma_unmap_single (ep->dev->gadget.dev.parent, req->req.dma, req->req.length, ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
        req->req.dma = DMA_ADDR_INVALID;
        req->mapped = 0;
    }

    if (status && status != -ESHUTDOWN)
    {   
        //PDEBUG ("Done(1): -- complete %s, req 0x%p, stat %d, len %u/%u\n",
        //        ep->ep.name, &req->req, status,
        //        req->req.actual, req->req.length);
    }        

    // don't modify queue heads during completion callback //
    ep->stopped = 1;

    if (req->req.complete)
    { 
        //if ((ep->desc->bEndpointAddress == VIXS_BULK_IN_ADDR)||(ep->desc->bEndpointAddress == VIXS_BULK_OUT_ADDR))        
        //{
        //   PDEBUG ("Done(2): -- complete %s, req 0x%p, buf 0x%p, stat %d, len %u/%u\n",
        //            ep->ep.name, &req->req, req->req.buf, status,
        //            req->req.actual, req->req.length);
        //}  
        req->req.complete (&ep->ep, &req->req);
    }

    ep->stopped = stopped;
}

//-------------------------------------------------------------------------//

//////////////////////////////////////
//
//  ViXS -- Get_Control_Out_Packet
//
//////////////////////////////////////
void Get_Control_Out_Packet(struct vixs_ep *ep, struct vixs_request *req, struct usb_request *_req)
{
    unsigned int         temp=0, i=0;
    unsigned int         setup_out_data=0;
    unsigned int         bytes_obtain=0;
    unsigned int         bytes_left=0;

retry:
    // EP0 Receive SETUP Data
    temp = MMR_READ (USBD_INT_STATUS);
    DBK("temp:%x\n",temp);
    if (temp & EP0_INT_EP0_OUT_RCVD_MASK)
    {
        bytes_left = setup_out_data = _req->length;
        //PDEBUG("Get_Control_Out_Packet - EP %s Receive %d bytes Setup Data\n", ep->ep.name, _req->length);
        while (bytes_left)
        {
            if (bytes_left > USB_MAX_EP_FIFO)
            {
                setup_out_data = USB_MAX_EP_FIFO;
            }
            else
            {
                setup_out_data = bytes_left;
            }

            bytes_left -= setup_out_data;
                
            for (i=0; i<setup_out_data/4; i++)
            {
                temp = MMR_READ(USBD_EP0_STATUS); 
                while (temp & OP_BUSY_MASK)
                {
                    PDEBUG("EP0 (1) -- FULL\n");
                    temp = MMR_READ(USBD_EP0_STATUS);             
                    cpu_relax();
                }
            
                temp = (i%(USB_MAX_CTRL_PAYLOAD/4))*4;
                temp |= 0 << REG_WOP_SHIFT; //read operation
                temp |= 1 << REG_CSR_SEL_SHIFT; // EP0
                temp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;       
                MMR_WRITE(USBD_EP0_CTRL, temp);       

                temp = MMR_READ(USBD_EP0_STATUS); 
                while (temp & OP_BUSY_MASK)
                {
                    PDEBUG("EP0 (2) -- FULL\n");
                    temp = MMR_READ(USBD_EP0_STATUS);             
                    cpu_relax();
                }

                *((unsigned int *)_req->buf+i+(bytes_obtain/4)) = (unsigned int *)MMR_READ(USBD_EP0_RD_DATA); 
                //PDEBUG("Get_Control_Out_Packet - Data = 0x%lx, buf = 0x%lx\n",  MMR_READ(USBD_EP0_RD_DATA), *((unsigned int *)_req->buf+i+(bytes_obtain/4)));
            }
            dump_packet(_req->buf,bytes_obtain);
            bytes_obtain += setup_out_data;
            MMR_WRITE(USBD_ACK, OUT_DATA_FIFO_EMPTY_ACK_MASK);
            MMR_WRITE (USBD_INT_STATUS, EP0_INT_EP0_OUT_RCVD_MASK);
            //PDEBUG("DONE DONE DONE !!! : (After Reset) USBD_INT_STATUS = 0x%lx\n", MMR_READ (USBD_INT_STATUS));
            if (bytes_left)
            {
                temp = MMR_READ (USBD_INT_STATUS);
                while (!(temp & EP0_INT_EP0_OUT_RCVD_MASK))
                {
                    PDEBUG("EP0 (3) -- FULL\n");
                    temp = MMR_READ(USBD_INT_STATUS);             
                    cpu_relax();
                }
            }
        }
        done (ep, req, 0);            
    }
    else
    {
        //PDEBUG("(No Data) - EP %s Receive 0x%lx bytes Setup Data\n", ep->ep.name, _req->length);
        goto retry;
    }
}

//////////////////////////////////////
//
//  ViXS -- Queue requests
//
//////////////////////////////////////
static int vixs_queue (struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
    struct vixs_request *req;
    struct vixs_ep      *ep;
    struct vixs         *dev;
    unsigned long       flags=0;
    unsigned int        descriptor_num=0;

    req = container_of (_req, struct vixs_request, req);
    ep = container_of (_ep, struct vixs_ep, ep);

    // catch wrong parameters
#ifdef CONFIG_USB_ETH_RNDIS    
    if (!_req || !_req->complete || !_req->buf || !list_empty (&req->queue))
#else
    if (!_req || !_req->complete || !_req->buf)
#endif        
    {   
        PDEBUG ("vixs_queue: -- (%s) ERROR01: _req = 0x%x\n", ep->ep.name, _req);
        PDEBUG ("vixs_queue: -- (%s) ERROR01: _req->complete = 0x%x\n", ep->ep.name, _req->complete);
        PDEBUG ("vixs_queue: -- (%s) ERROR01: _req->buf = 0x%x\n", ep->ep.name, _req->buf);
        PDEBUG ("vixs_queue: -- (%s) ERROR01: list_empty (&req->queue) = 0x%x\n", ep->ep.name, list_empty (&req->queue));
        return -EINVAL;
    }

    if (!_ep || (!ep->desc && ep->num != 0))
    {   
        PDEBUG ("vixs_queue: -- ERROR02\n");
        return -EINVAL;
    }
    
    dev = ep->dev;
    if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
    {   
        PDEBUG ("vixs_queue: -- ERROR03\n");
        return -ESHUTDOWN;
    }

    req->ep = ep;


	if(req->req.length)
	{
	 	req->req.dma = dma_map_single (ep->dev->gadget.dev.parent, req->req.buf, req->req.length, ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->mapped = 1;
	}	
    spin_lock_irqsave (&dev->lock, flags);
    
    _req->status = -EINPROGRESS;
    _req->actual = 0;

    descriptor_num = ep->num_of_desc_submitted;

//        PDEBUG ("vixs_queue: -- IN: Req=0x%x, descriptor_num=%d\n", _req, descriptor_num);
    
    // Start process this i/o queue?
    //if (list_empty (&ep->queue) && !ep->stopped)
    if (descriptor_num < MAX_TRANS_DESC && !ep->stopped)
    {
        // maybe there's no control data, just status ack //
        if (ep->num == 0 && _req->length == 0) 
        {
            done (ep, req, 0);
            //PDEBUG ("Done: EP Name = %s : Status ack\n", ep->ep.name);
            goto done;
        }
        else if (ep->num == 0 && _req->length != 0 && ep->is_in == 0)
        {
            Get_Control_Out_Packet(ep, req, _req);
            goto done;            
        }

        req->insert = 1;
		
        write_fifo (ep, _req);
    }
    else
    {        
        req->insert = 0;
        
#ifdef CDC_DEBUG
        MMFB_WRITE(0x90, MMFB_READ(0x90)+1);
#endif
        ep->pending_request ++;
    }

    if (req)
    {
        list_add_tail (&req->queue, &ep->queue);
    }
done:

    spin_unlock_irqrestore (&dev->lock, flags);
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Dequeue
//
//////////////////////////////////////
static int vixs_dequeue (struct usb_ep *_ep, struct usb_request *_req)
{
    struct vixs_ep	*ep;
    struct vixs_request	*req;
    unsigned long		flags;
    int			stopped;

    PDEBUG("vixs_dequeue: -- Enter\n");
    ep = container_of (_ep, struct vixs_ep, ep);

    if (!_ep || (!ep->desc && ep->num != 0) || !_req)
        return -EINVAL;

    spin_lock_irqsave (&ep->dev->lock, flags);
    stopped = ep->stopped;

    ep->stopped = 1;

    // make sure it's still queued on this endpoint 
    list_for_each_entry (req, &ep->queue, queue) 
    {
        if (&req->req == _req)
            break;
    }

    if (&req->req != _req) 
    {
        spin_unlock_irqrestore (&ep->dev->lock, flags);
        return -EINVAL;
    }

    // queue head may be partially complete. 
    if (ep->queue.next == &req->queue) 
    {
        PDEBUG ("unlink (%s) pio\n", _ep->name);
        done (ep, req, -ECONNRESET);
        req = NULL;
    }
    // The request hasn't been processed //
    else
    {
    }

    if (req)
    {
        done (ep, req, -ECONNRESET);
    }
    ep->stopped = stopped;

    spin_unlock_irqrestore (&ep->dev->lock, flags);
    PDEBUG("vixs_dequeue: -- Leave\n"); 
    return 0;
}

//-------------------------------------------------------------------------//

static int vixs_fifo_status (struct usb_ep *_ep);


//////////////////////////////////////
//
//  ViXS -- Set Halt
//
//////////////////////////////////////

//TODO: [jlwang] This function may have problem since it should not disable EP, but stall EP
static int set_halt(struct vixs_ep *target_ep)
{
    unsigned int temp=0;
	u32 	epnum, epidx;


    if ((target_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    {
        // Interrupt IN 
        temp = MMR_READ(USBD_INTIN0_CTRL);
        temp &= ~(USBD_INTIN0_CTRL_ENABLE_MASK);
        MMR_WRITE(USBD_INTIN0_CTRL, temp);

    }
    else if ((target_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
    {
		epnum = target_ep->desc->bEndpointAddress&0xf;
		epidx = (epnum-1)/2;

		if(target_ep->desc->bEndpointAddress & 0x80)	//bulk in
		{

	        // Bulk IN 
	        // Set Halt = 1 / Clear Halt = 0
	        temp = MMR_READ(USBD_BULKIN0_CTRL + epidx*0x20);
	        temp &= ~(USBD_BULKIN0_CTRL_ENABLE_MASK);
	        MMR_WRITE(USBD_BULKIN0_CTRL + epidx*0x20, temp);

		}
		else
		{

            if(vixs_usbd_skip_enum)
                epidx=0;
	        // Bulk OUT 
	        // Set Halt = 1 / Clear Halt = 0
	        temp = MMR_READ(USBD_BULKOUT0_CTRL + epidx*0x20);
	        temp &= ~(USBD_BULKOUT0_CTRL_ENABLE_MASK);
	        MMR_WRITE(USBD_BULKOUT0_CTRL + epidx*0x20, temp);

		}
    }
	
    
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Clear Halt
//
//////////////////////////////////////
static int clear_halt(struct vixs_ep *target_ep)
{
    unsigned int temp=0;
	u32 	epnum, epidx;


    if ((target_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    {
        // Interrupt IN 
        temp = MMR_READ(USBD_INTIN0_CTRL);
        temp |= (USBD_INTIN0_CTRL_ENABLE_MASK);
        MMR_WRITE(USBD_INTIN0_CTRL, temp);

    }
    else if ((target_ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
    {
		epnum = target_ep->desc->bEndpointAddress&0xf;
		epidx = (epnum-1)/2;

		if(target_ep->desc->bEndpointAddress & 0x80)	//bulk in
		{

	        // Bulk IN 
	        // Set Halt = 1 / Clear Halt = 0
	        temp = MMR_READ(USBD_BULKIN0_CTRL + epidx*0x20);
			temp |= (USBD_BULKIN0_CTRL_ENABLE_MASK);
	        MMR_WRITE(USBD_BULKIN0_CTRL + epidx*0x20, temp);

		}
		else
		{
            if(vixs_usbd_skip_enum)
                epidx=0;
	        // Bulk OUT 
	        // Set Halt = 1 / Clear Halt = 0
	        temp = MMR_READ(USBD_BULKOUT0_CTRL + epidx*0x20);
			temp |= (USBD_BULKOUT0_CTRL_ENABLE_MASK);
	        MMR_WRITE(USBD_BULKOUT0_CTRL + epidx*0x20, temp);

		}
    }
	
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Set Halt
//
//////////////////////////////////////
static int vixs_set_halt (struct usb_ep *_ep, int value)
{
    struct vixs_ep	*ep;
    unsigned long		flags;
    int			retval = 0;

    PDEBUG("Vixs_set_halt: -- Enter\n");
    ep = container_of (_ep, struct vixs_ep, ep);
    if (!_ep || (!ep->desc && ep->num != 0))
    {
        PDEBUG("Vixs_set_halt: -- Error1\n");
        return -EINVAL;
    }
    
    if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
    {
        PDEBUG("Vixs_set_halt: -- Error2\n");
        return -ESHUTDOWN;
    }

    spin_lock_irqsave (&ep->dev->lock, flags);
    if (!list_empty (&ep->queue))
    {
        PDEBUG("Vixs_set_halt: -- Error3\n");
        retval = -EAGAIN;
    }
    else if (ep->is_in && value && vixs_fifo_status (_ep) != 0)
    {
        PDEBUG("Vixs_set_halt: -- Error4\n");
        retval = -EAGAIN;
    }
    else 
    {
        // set/clear, then synch memory views with the device //
        if (value) 
        {
            if (ep->num == 0)
            {
                ep->dev->protocol_stall = 1;
            }
            else
            {
                PDEBUG("Vixs_set_halt: -- Set Halt\n");
                set_halt (ep);
            }
        } 
        else
        {
            PDEBUG("Vixs_set_halt: -- Clear Halt\n");
            clear_halt (ep);
        }
    }
    spin_unlock_irqrestore (&ep->dev->lock, flags);
    PDEBUG("Vixs_set_halt: -- Leave\n");
    return retval;
}


//////////////////////////////////////
//
//  ViXS -- FIFO Flush
//
//////////////////////////////////////
static int vixs_fifo_status (struct usb_ep *_ep)
{
    struct vixs_ep	*ep;
    u32			avail=0;

    PDEBUG("Vixs_fifo_status: -- Enter\n");

    ep = container_of (_ep, struct vixs_ep, ep);
    if (!_ep || (!ep->desc && ep->num != 0))
    {   
        PDEBUG("Vixs_fifo_status: -- Leave: ERROR 1\n");
        return -ENODEV;
    }
    if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
    {   
        PDEBUG("Vixs_fifo_status: -- Leave: ERROR 2\n");
        return -ESHUTDOWN;
    }

    PDEBUG("Vixs_fifo_status: -- Leave\n");

    return avail;
}


//////////////////////////////////////
//
//  ViXS -- FIFO Flush
//
//////////////////////////////////////
static void vixs_fifo_flush (struct usb_ep *_ep)
{
    struct vixs_ep	*ep;
	u32 	epnum, epidx;

    ep = container_of (_ep, struct vixs_ep, ep);
    if (!_ep || (!ep->desc && ep->num != 0))
        return;
    if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
        return;

    if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
    { 
        // Reset Interrupt IN 
        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|INTIN0_SOFT_RST_MASK);
        MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~INTIN0_SOFT_RST_MASK);
 
    }
    else if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK)
    {
		epnum = ep->desc->bEndpointAddress&0xf;
		epidx = (epnum-1)/2;

		if(ep->desc->bEndpointAddress & 0x80)	//bulk in
		{
			// Reset Bulk IN 
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|(BULKIN0_SOFT_RST_MASK<<epidx));
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKIN0_SOFT_RST_MASK<<epidx));
		}
		else
		{
            if(vixs_usbd_skip_enum)
                epidx=0;
			// Reset Bulk OUT 
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)|(BULKOUT0_SOFT_RST_MASK<<epidx));
			MMR_WRITE(USBD_SOFT_RST, MMR_READ(USBD_SOFT_RST)&~(BULKOUT0_SOFT_RST_MASK<<epidx));

		}
    }
		
}

static const struct usb_ep_ops vixs_ep_ops = {
    .enable         = vixs_ep_enable,
    .disable        = vixs_ep_disable,

    .alloc_request  = vixs_alloc_request,
    .free_request   = vixs_free_request,

//    .alloc_buffer   = vixs_alloc_buffer,
//    .free_buffer    = vixs_free_buffer,

    .queue          = vixs_queue,
    .dequeue        = vixs_dequeue,

    .set_halt       = vixs_set_halt,
    .fifo_status    = vixs_fifo_status,
    .fifo_flush     = vixs_fifo_flush,
};

//-------------------------------------------------------------------------//

//////////////////////////////////////
//
//  ViXS -- Gat Frame
//
//////////////////////////////////////
static int vixs_get_frame (struct usb_gadget *_gadget)
{
    struct vixs		*dev;
    u16			retval=0;

    if (!_gadget)
        return -ENODEV;
    dev = container_of (_gadget, struct vixs, gadget);
    return retval;
}


//////////////////////////////////////
//
//  ViXS -- Wake up
//
//////////////////////////////////////
static int vixs_wakeup (struct usb_gadget *_gadget)
{
    struct vixs		*dev;

    if (!_gadget)
        return 0;
    dev = container_of (_gadget, struct vixs, gadget);

    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Self Power
//
//////////////////////////////////////
static int vixs_set_selfpowered (struct usb_gadget *_gadget, int value)
{
    struct vixs		*dev;

    if (!_gadget)
        return 0;
    dev = container_of (_gadget, struct vixs, gadget);

    return 0;
}

//////////////////////////////////////
//
//  ViXS -- Pullup
//
//////////////////////////////////////
static void stop_activity(struct vixs *udc);


static int vixs_pullup(struct usb_gadget *_gadget, int is_on)
{
    struct vixs  *dev;
	u32 temp;

    if (!_gadget)
        return -ENODEV;
    dev = container_of (_gadget, struct vixs, gadget);
#if 1
	if(is_on)
	{
		//[jlwang] Only put phy out of susped state once a valid gadget driver is registered
		// Put the Phy out of Suspend state
		temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
		temp |= (PHY_PORT1_SUSPENDM_ENB_MASK);
		writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
		PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
		udelay(1000);
		
		temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
		temp |= (PHY_PORT1_VBUSVLDEXT_ENB_MASK);
		writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
		PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
		udelay(1000);
	
	}
	else
	{
		stop_activity(dev);

		// Put phy in  Suspend state
		temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
		temp &= ~(PHY_PORT1_SUSPENDM_ENB_MASK);
		writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
		PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
		udelay(1000);
		
		temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
		temp &= ~(PHY_PORT1_VBUSVLDEXT_ENB_MASK);
		writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
		PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
		udelay(1000);


		temp = readl (USBD_UDC20_CTRL + VIXS_MMR_BASE);
		temp |= SOFT_DISCON_TRIG_MASK;
		writel(temp, (USBD_UDC20_CTRL+VIXS_MMR_BASE));
		udelay(1000);
		
	}
#endif	
    return 0;
}

static const struct usb_gadget_ops vixs_ops = {
	.get_frame          = vixs_get_frame,
	.wakeup             = vixs_wakeup,
	.set_selfpowered    = vixs_set_selfpowered,
	.pullup             = vixs_pullup,
};


//////////////////////////////////////
//
//  ViXS -- USB Reinit
//
//////////////////////////////////////
static void usb_reinit (struct vixs *dev)
{
    u32	tmp;
    //PDEBUG("Usb_reinit: -- Enter\n");

	dev->mCurConfiguration = 1;
	dev->mCurInterface = 1;
	dev->mCurAltSetting = 1;
    
    // basic endpoint init
    for (tmp = 0; tmp < NUM_OF_EP; tmp++) 
    {
        struct vixs_ep	*ep = &dev->ep[tmp];

        //strcpy(ep->name, ep_name[tmp]);
        ep->ep.name = ep_name [tmp];
        ep->dev = dev;
        ep->num = tmp;
        ep->stopped = 0;
        ep->ep.ops = &vixs_ep_ops;
        ep->gadget = &dev->gadget;
        ep->is_in = 1;
		ep->num_of_desc_submitted = 0;
		ep->pending_request = 0;
        
        if (tmp==0 || tmp == 10) 
        {
            // Control / Interrupt Endpoint
            ep->fifo_size = USB_MAX_CTRL_PAYLOAD;
            dev->ep [tmp].ep.maxpacket = USB_MAX_CTRL_PAYLOAD;
        } 
        else
        {
            // Bulk In and Bulk Out Endpoint
            ep->fifo_size = USB_MAX_BULK_PAYLOAD;
            dev->ep [tmp].ep.maxpacket = USB_MAX_BULK_PAYLOAD;
            if (!(tmp%2))
            {
            	//ep2/ep4/... are out endpoint
                ep->is_in=0;
            }

        }

        // the queue lists any req for this ep
        INIT_LIST_HEAD (&ep->queue);

        //PDEBUG("Usb_reinit: EP Name = %s, EP Loc = %d, EP Max Size = 0x%lx\n", ep->ep.name, tmp, dev->ep [tmp].ep.maxpacket);
        //PDEBUG("Usb_reinit: EP = 0x%lx, EP = 0x%lx, ops = 0x%lx\n", ep, ep->ep, ep->ep.ops);

        // gagdet.ep_list used for ep_autoconfig so no ep0
        if (tmp)
            list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);
    }

    //PDEBUG("Usb_reinit: -- Leave\n");
}


//////////////////////////////////////
//
//  ViXS -- Register USB Gadget Driver
//
//////////////////////////////////////
int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
			int (*bind)(struct usb_gadget *))
{
    struct vixs		*dev = the_controller;
    int			retval;
    unsigned int       temp=0;

    // insist on high speed support from the driver, since
    // (dev->usb->xcvrdiag & FORCE_FULL_SPEED_MODE)
    // "must not be used in normal operation"    
    
    PDEBUG("@@@@usb_gadget_register_driver -- Driver=0x%x : Enter \n", driver);
    if (!driver
        || driver->speed != USB_SPEED_HIGH
        || !bind
//        || !driver->unbind // we compile the driver as built-in.
        || !driver->setup)
    {
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR01: driver = 0x%lx\n", driver);
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR01: driver->speed = 0x%lx, USB_SPEED_HIGH = 0x%lx\n", driver->speed, USB_SPEED_HIGH);
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR01: driver->unbind = 0x%lx\n", driver->unbind);
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR01: driver->setup = 0x%lx\n", driver->setup);
        return -EINVAL;
    }

    if (!dev)
    {
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR02: dev (vixs) = 0x%lx\n", dev);
        return -ENODEV;
    }

    if (dev->driver)
    {
        PDEBUG("@@@@usb_gadget_register_driver -- ERROR03: dev->driver = 0x%lx\n", dev->driver);
        return -EBUSY;
    }

    // hook up the driver ... //
    dev->softconnect = 1;
    driver->driver.bus = NULL;
    dev->driver = driver;
    dev->gadget.dev.driver = &driver->driver;
    retval = bind (&dev->gadget);
    if (retval) 
    {
        PDEBUG ("@@@@usb_gadget_register_driver: ERROR bind to driver %s --> %d\n", driver->driver.name, retval);
        dev->driver = NULL;
        dev->gadget.dev.driver = NULL;       
        PDEBUG("@@@@usb_gadget_register_driver -- Leave \n");
        return retval;
    }
    else
    {
        PDEBUG ("@@@@usb_gadget_register_driver: SUCCESS bind to driver %s --> %d\n", driver->driver.name, retval);
    }

    // Enable the interrupt mask for USBD from MIP4
    temp = MMR_READ (CPU_INTERRUPT_MASK);
    //PDEBUG("@@@@usb_gadget_register_driver : read = CPU_INTERRUPT_MASK = 0x%lx\n", temp);
    
    temp |= CPU_INTERRUPT_MASK_USBD_INT_MASK;
    MMR_WRITE(CPU_INTERRUPT_MASK, temp);
    //PDEBUG("@@@@usb_gadget_register_driver : write = CPU_INTERRUPT_MASK = 0x%lx\n", temp);

    // Enable the interrupt mask for USBD block
    temp = MMR_READ(USBD_MIPS_INT_MASK);
    //PDEBUG("@@@@usb_gadget_register_driver : read = USBD_MIPS_INT_MASK = 0x%lx\n", temp);

	//[jlwang] only enable EP0 interrupt, all other interrupt will be enabled when EP is enabled
    temp |= (MIPS_EP0_INT_EN_SETUP_RCVD_MASK|
            MIPS_INT_EN_SETCONFIGURATION_MASK|
            MIPS_INT_EN_SETINTERFACE_MASK|
            MIPS_INT_EN_RESET_MASK|
            MIPS_INT_EN_SUSPEND_MASK|
            MIPS_INT_EN_WAKEUP_MASK|
            MIPS_INT_EN_SPEED_MASK);
    MMR_WRITE(USBD_MIPS_INT_MASK, temp);
    //PDEBUG("@@@@usb_gadget_register_driver : write = USBD_MIPS_INT_MASK = 0x%lx\n", temp);

    // Put VBUS out of Reset
    //temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
    //temp |= (USBD_PHY_CTRL_DETECT_VBUS_0_ENABLE_MASK|USBD_PHY_CTRL_DETECT_VBUS_1_ENABLE_MASK);
    //writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
    //PDEBUG("@@@@usb_gadget_register_driver : USBD_PHY_CTRL(VBUS)=(0x%lx)\n", temp);
    //udelay(1000);

    // Take the Phy out of Suspend state
    //temp = readl (USBD_UDC20_CTRL + VIXS_MMR_BASE);
    //temp |= (USBD_UDC20_CTRL_PHY_SUSPEND_ENABLE_MASK);
    //writel(temp, (USBD_UDC20_CTRL+VIXS_MMR_BASE));
    //PDEBUG("@@@@usb_gadget_register_driver : USBD_UDC20_CTRL(Phys)=(0x%lx)\n", temp);
    //udelay(1000);


	//[jlwang] Only put phy out of susped state once a valid gadget driver is registered
	// Put the Phy out of Suspend state
	temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
	temp |= (PHY_PORT1_SUSPENDM_ENB_MASK);
	writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
	PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
	udelay(1000);
	
	temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
	temp |= (PHY_PORT1_VBUSVLDEXT_ENB_MASK);
	writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
	PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
	udelay(1000);

    PDEBUG("@@@@usb_gadget_register_driver -- Leave\n");
    return 0;
}

EXPORT_SYMBOL (usb_gadget_probe_driver);


//////////////////////////////////////
//
//  ViXS -- Reset all EP queues
//
//////////////////////////////////////
static inline int vixs_reset_ep_queue(struct vixs *dev, u8 pipe)
{
    struct vixs_ep *ep = &dev->ep[pipe];

    PDEBUG("vixs_reset_ep_queue: EP = %s -- Enter\n", ep->ep.name);

    if (!ep->name)
    {   
        PDEBUG("vixs_reset_ep_queue: Error1 => EP = %s", ep->ep.name);
        return 0;
    }

    nuke(ep);

    PDEBUG("vixs_reset_ep_queue: -- Leave\n");
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Reset all EP queues
//
//////////////////////////////////////
static int reset_queues(struct vixs *dev)
{
    u8 pipe;

    PDEBUG("reset_queues: -- Enter\n");

    for (pipe = 0; pipe < NUM_OF_EP; pipe++)
    {
        vixs_reset_ep_queue(dev, pipe);
    }

    // report disconnect; the driver is already quiesced
    if (dev->driver) 
    {
        dev->driver->disconnect (&dev->gadget);
    }

    PDEBUG("reset_queues: -- Leave\n");
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Stop all activity
//
//////////////////////////////////////
static void stop_activity (struct vixs *udc)
{
	struct usb_gadget_driver *driver = udc->driver;
	int i;

    PDEBUG("stop_activity: -- Enter\n");
    // don't disconnect if it's not connected
    if (udc->gadget.speed == USB_SPEED_UNKNOWN)
    {
        PDEBUG("stop_activity: -- Error1\n");
        driver = NULL;
    }
	udc->gadget.speed = USB_SPEED_UNKNOWN;

    reset_queues(udc);
	
	usb_reinit(udc);
	
    PDEBUG("stop_activity: -- Leave\n");
}


//////////////////////////////////////
//
//  ViXS -- Unregister USB Gadget Driver
//
//////////////////////////////////////
int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
    struct vixs	*dev = the_controller;
    unsigned long	flags;

    PDEBUG ("usb_gadget_unregister_driver: -- Enter\n");

    if (!dev)
    {   
        PDEBUG ("usb_gadget_unregister_driver: -- Leaving : ERROR 1\n");
        return -ENODEV;
    }
    if (!driver || driver != dev->driver || !driver->unbind)
    {   
        PDEBUG ("usb_gadget_unregister_driver: -- Leaving : ERROR 2\n");
        return -EINVAL;
    }

    // in fact, no needed //
    dev->usb_state = USB_STATE_ATTACHED;
    dev->ep0_state = WAIT_FOR_SETUP;
    dev->ep0_dir = 0;

    spin_lock_irqsave (&dev->lock, flags);
    stop_activity (dev);
    spin_unlock_irqrestore (&dev->lock, flags);

    vixs_pullup (&dev->gadget, 0);

    driver->unbind (&dev->gadget);
    dev->gadget.dev.driver = NULL;
    dev->driver = NULL;

    PDEBUG ("usb_gadget_unregister_driver(%s): -- Leaving\n", driver->driver.name);
    return 0;
}

EXPORT_SYMBOL (usb_gadget_unregister_driver);


//////////////////////////////////////
//
//  ViXS -- Get Endpoint by Address
//
//////////////////////////////////////
static struct vixs_ep *get_ep_by_addr (struct vixs *dev, u16 wIndex)
{
    struct vixs_ep	*ep;

    // Endpoint 0 - Control Endpoint
    if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
    {
        return &dev->ep [0];
    }
    
    list_for_each_entry (ep, &dev->gadget.ep_list, ep.ep_list) 
    {
        u8	bEndpointAddress;

        if (!ep->desc)
        {
            continue;
        }
        bEndpointAddress = ep->desc->bEndpointAddress;
        if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
        {
            continue;
        }
        if ((wIndex & 0x0f) == (bEndpointAddress & 0x0f))
        {
            return ep;
        }
    }
    return NULL;
}


//////////////////////////////////////
//
//  ViXS -- Speed IRQ
//
//////////////////////////////////////
static void USBD_speed_irq (struct vixs *dev)
{
    unsigned int    usb_speed=0, tmp=0;;

#ifdef CDC_DEBUG
    MMFB_WRITE(0x80, 0);
    MMFB_WRITE(0x84, 0);
    MMFB_WRITE(0x88, 0);
    MMFB_WRITE(0x8C, 0);
    MMFB_WRITE(0x90, 0);
    MMFB_WRITE(0x94, 0);
    MMFB_WRITE(0x98, 0);
    MMFB_WRITE(0x9C, 0);
    MMFB_WRITE(0xA0, 0);
    MMFB_WRITE(0xA4, 0);
    MMFB_WRITE(0xA8, 0);
    MMFB_WRITE(0xAC, 0);
#endif

    // We can determine the device speed now
    usb_speed = MMR_READ(USBD_UDC20_STATUS0);
    usb_speed &= UDC20_ENUM_SPEED_MASK;
    usb_speed = usb_speed >> UDC20_ENUM_SPEED_SHIFT;

    if (usb_speed == VIXS_HIGH_SPEED) 
    {
        dev->gadget.speed = USB_SPEED_HIGH;
        //PDEBUG("USBD_setup_irq: USB_SPEED_HIGH\n");
    }
    else if (usb_speed == VIXS_FULL_SPEED) 
    {    
        dev->gadget.speed = USB_SPEED_FULL;
        //PDEBUG("USBD_setup_irq: USB_SPEED_FULL\n");
    }
    else if (usb_speed == VIXS_LOW_SPEED) 
    {
        dev->gadget.speed = USB_SPEED_LOW;
        //PDEBUG("USBD_setup_irq: USB_SPEED_LOW\n");
    }
    else
    {
        //PDEBUG("USBD_setup_irq: USB_SPEED_UNKNOW\n");
    }
}


//////////////////////////////////////
//
//  ViXS -- Suspend IRQ
//
//////////////////////////////////////
static void USBD_suspend_irq (struct vixs *dev)
{
    //unsigned int temp=0;
    PDEBUG("USBD_suspend_irq -- Enter\n");
    dev->resume_state = dev->usb_state;
    dev->usb_state = USB_STATE_SUSPENDED;

    // report suspend to the driver, serial.c does not support this //
    if (dev->driver->suspend)
		dev->driver->suspend(&dev->gadget);
}

//////////////////////////////////////
//
//  ViXS -- Wakeup IRQ
//
//////////////////////////////////////
static void USBD_wakeup_irq (struct vixs *dev)
{
    unsigned int pipe=0;
    PDEBUG("USBD_wakeup_irq -- Enter\n");

    usb_reinit (dev);

    dev->usb_state = dev->resume_state;
    dev->resume_state = 0;

    for (pipe = 0; pipe < NUM_OF_EP; pipe++)
    {
        dev->ep[pipe].stopped=0;
    }

    // report resume to the driver, serial.c does not support this //
    if (dev->driver->resume)
        dev->driver->resume(&dev->gadget);
}


//////////////////////////////////////
//
//  ViXS -- Reset IRQ
//
//////////////////////////////////////
static void USBD_reset_irq (struct vixs *dev)
{
    //unsigned int temp=0;

    //PDEBUG("USBD_reset_irq -- Enter\n");
    dev->device_address = 0;

    // Clear usb state //
    dev->resume_state = 0;
    dev->ep0_dir = 0;
    dev->ep0_state = WAIT_FOR_SETUP;
    dev->remote_wakeup = 0;	// default to 0 on reset //
    dev->gadget.b_hnp_enable = 0;
    dev->gadget.a_hnp_support = 0;
    dev->gadget.a_alt_hnp_support = 0;
    dev->usb_state = USB_STATE_ATTACHED;
}


//////////////////////////////////////
//
//  ViXS -- Send Data Through EP0
//
//////////////////////////////////////
static void EP0_Send(struct vixs *dev, u16 length, unsigned int* data)
{
    unsigned int temp=0, temp1=0;
    unsigned int i=0;
    unsigned int trigger_send=0;
    unsigned int bytes_left=length;
    unsigned int bytes_sent=0;
    //unsigned int send_bytes=length;
    // Submit a zero length packet
    if (!length)
    {
        PDEBUG("EP0_Send:(Size 0)  Length=%d, Data=0x%lx\n", length, *data);
        temp = (length<<EP0_IN_LEN_SHIFT);
        temp |= (USB_MAX_EP_FIFO<<EP0_PKT_SIZE_SHIFT);
        temp |= EP0_IN_FIFO_EMPTY_INT_EN_MASK;       
        MMR_WRITE(USBD_EP0_CTRL1, temp);

        // Enable the interrupt mask
        temp1 = MMR_READ(USBD_MIPS_INT_MASK);
        temp1 |= MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
        MMR_WRITE(USBD_MIPS_INT_MASK, temp1);
    
        // Trigger the send
        MMR_WRITE(USBD_ACK, IN_DATA_FIFO_FULL_ACK_MASK);
        return;
    }

	//PDEBUG("EP0_Send:  Length=%d, Data=0x%lx, USBD_INT_STATUS=0x%lx\n", length, *data, MMR_READ(USBD_INT_STATUS));
    dump_packet(data,bytes_left);    

    while (bytes_left)
    {
        if (bytes_left > USB_MAX_EP_FIFO)
        {
            length = USB_MAX_EP_FIFO;
        }
        else
        {
            length= bytes_left;
        }
        bytes_left -= length;

        // Check if FIFO is empty
        temp = MMR_READ(USBD_INT_STATUS);
        while (!(temp & EP0_INT_EP0_IN_EMPTY_MASK))
        {
            temp = MMR_READ(USBD_INT_STATUS);
	        PDEBUG("EP0 (4) -- FULL\n");
            cpu_relax();
        }
     
        temp = (length<<EP0_IN_LEN_SHIFT);
        temp |= (USB_MAX_EP_FIFO<<EP0_PKT_SIZE_SHIFT);
        temp |= EP0_IN_FIFO_EMPTY_INT_EN_MASK;       
        MMR_WRITE(USBD_EP0_CTRL1, temp);
        PDEBUG("Transfer Length=%d, Left=%d, USBD_EP0_CTRL1=0x%lx\n", length, bytes_left, MMR_READ(USBD_EP0_CTRL1));  
        // Start put data into the FIFO
        for (i=0; i<(length+3)/4; i++)
        {
            trigger_send = 0;

#if 0
            temp = MMR_READ(USBD_EP0_STATUS); 
            while (temp & OP_BUSY_MASK)
            {
                PDEBUG("EP0 (5) -- FULL\n");
                temp = MMR_READ(USBD_EP0_STATUS);             
                cpu_relax();
            }
#endif
            MMR_WRITE(USBD_EP0_WR_DATA, *(data+i+(bytes_sent/4)));
            //PDEBUG("EP0_Send(%d): Length=0x%lx, Data=0x%lx\n", i, length, *(data+i+(bytes_sent/4)));

            temp = (i%(USB_MAX_CTRL_PAYLOAD/4))*4;
            temp |= 1 << REG_WOP_SHIFT; //write operation
            temp |= 1 << REG_CSR_SEL_SHIFT;
            temp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;       
            MMR_WRITE(USBD_EP0_CTRL, temp);

            temp = MMR_READ(USBD_EP0_STATUS); 
            while (temp & OP_BUSY_MASK)
            {
                PDEBUG("EP0 (5) -- FULL\n");
                temp = MMR_READ(USBD_EP0_STATUS);             
                cpu_relax();
            }

            
            // 64-bytes : FIFO Full
            if (i==((USB_MAX_CTRL_PAYLOAD/4)-1))
            {
                trigger_send = 1;               
                // Trigger the send
                MMR_WRITE(USBD_ACK, IN_DATA_FIFO_FULL_ACK_MASK);
                //PDEBUG("EP0_Send: Length=%d (Trigger transfer-01), USBD_INT_STATUS=0x%lx\n", length, MMR_READ(USBD_INT_STATUS));        

                if (!bytes_left)
                {
                    //PDEBUG("EP0_Send: Length=%d (Interrupt Enable-01): bytes_left=0x%lx\n\n", length, bytes_left);        
                    // Trigger interrupt on the last chunk of transfer
                    temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                    temp1 |= MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
                    MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
                }
                else
                {       
                    // Disable the interrupt if it is not the last chunk of transfer
                    //temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                    //temp1 &= ~USBD_MIPS_INT_MASK_MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
                    //MMR_WRITE(USBD_MIPS_INT_MASK, temp1);  
                    //PDEBUG("EP0_Send: Length=%d (Interrupt Disable-01): bytes_left=%d\n\n", length, bytes_left);
                    MMR_WRITE(USBD_INT_STATUS, EP0_INT_EP0_IN_EMPTY_MASK);
                }
                //udelay(1000);
            }
        }

        bytes_sent += length;

        // In this case, the data size is less than 64 bytes
        if (!trigger_send)
        {
            // Trigger the send
            MMR_WRITE(USBD_ACK, IN_DATA_FIFO_FULL_ACK_MASK);
            //PDEBUG("EP0_Send: Length=%d (Trigger transfer-02), USBD_INT_STATUS=0x%lx\n", length, MMR_READ(USBD_INT_STATUS));        

            if (!bytes_left)
            {
                PDEBUG("EP0_Send: Length=%d (Interrupt Enable-02)\n\n", length);        
                // Trigger interrupt on the last chunk of transfer
                temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                temp1 |= MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
                MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
            }
            else
            {
                // Disable the interrupt if it is not the last chunk of transfer
                //temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                //temp1 &= ~USBD_MIPS_INT_MASK_MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
                //MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
                PDEBUG("EP0_Send: Length=%d (Interrupt Disable-02)\n\n", length);        
                MMR_WRITE(USBD_INT_STATUS, EP0_INT_EP0_IN_EMPTY_MASK);
            }
        }
    }
}


///////////////////////////////////////////
//
//  ViXS -- Send Data Through Interrupt Endpoint
//
///////////////////////////////////////////
static void Interrupt_Send(struct vixs *dev, struct vixs_ep *ep,  u16 length, unsigned int * data)
{
    unsigned int temp=0, temp1=0;
    unsigned int i=0;
    unsigned int trigger_send=0;
    unsigned int bytes_left=length;

    // Submit a zero length packet
    if (!length)
    {
	    //PDEBUG("Interrupt_Send:(Size 0)  Length=0x%lx, Data=0x%lx\n", length, *data);
        temp = ((ep->desc->bEndpointAddress&0x0F) << USBD_INTIN0_CTRL_EP_NUM_SHIFT);
        temp |= (length << INTIN0_PKT_LEN_SHIFT);
        temp |= (1 << USBD_INTIN0_CTRL_ENABLE_SHIFT);
        temp |= (1 << INTIN0_EMPTY_INT_EN_SHIFT);
        temp |= dev->mCurAltSetting << USBD_INTIN0_CTRL_ALTERNATE_SHIFT;
        temp |= dev->mCurInterface << USBD_INTIN0_CTRL_INTERFACE_SHIFT;
        temp |= dev->mCurConfiguration << USBD_INTIN0_CTRL_CONFIG_SHIFT;
        MMR_WRITE(USBD_INTIN0_CTRL, temp);

        // Enable the interrupt mask
        temp1 = MMR_READ(USBD_MIPS_INT_MASK);
        temp1 |= MIPS_INTIN0_INT_EN_EMPTY_MASK;
        MMR_WRITE(USBD_MIPS_INT_MASK, temp1);
    
        // Trigger the send
        MMR_WRITE(USBD_ACK, INT_IN_FIFO_FULL_ACK_MASK);
        return;
    }

    /* JDEBUG */
    dump_stack();
    PDEBUG("Interrupt_Send: Length=0x%lx, Data=0x%lx, USBD_INTIN0_CTRL=0x%lx, USBD_INT_STATUS=0x%lx\n", length, *data, MMR_READ(USBD_INTIN0_CTRL), MMR_READ(USBD_INT_STATUS));

    while (bytes_left)
    {
        if (bytes_left > USB_MAX_EP_FIFO)
        {
            length = USB_MAX_EP_FIFO;
        }
        else
        {
            bytes_left = length;
        }
        bytes_left -= length;

        // Check if FIFO is empty
        temp = MMR_READ(USBD_INT_STATUS);
        while (!(temp & INTIN0_INT_EMPTY_MASK))
        {
//            PDEBUG("INT IN -- FULL\n");
            temp = MMR_READ(USBD_INT_STATUS);
            cpu_relax();
        }
        
        temp = (length<<INTIN0_PKT_LEN_SHIFT);
        temp |= (1<<USBD_INTIN0_CTRL_ENABLE_SHIFT);
        temp |= ((0x0F&ep->desc->bEndpointAddress)<<USBD_INTIN0_CTRL_EP_NUM_SHIFT);
        temp |= (1<<INTIN0_EMPTY_INT_EN_SHIFT);
        temp |= dev->mCurAltSetting << USBD_INTIN0_CTRL_ALTERNATE_SHIFT;
        temp |= dev->mCurInterface << USBD_INTIN0_CTRL_INTERFACE_SHIFT;
        temp |= dev->mCurConfiguration << USBD_INTIN0_CTRL_CONFIG_SHIFT;
        
        MMR_WRITE(USBD_INTIN0_CTRL, temp);

        // Start put data into the FIFO
        for (i=0; i<(length+3)/4; i++)
        {
            trigger_send = 0;
            temp = MMR_READ(USBD_EP0_STATUS); 
            while (temp & OP_BUSY_MASK)
            {
                PDEBUG("EP0 (6) -- FULL\n");
                temp = MMR_READ(USBD_EP0_STATUS);             
                cpu_relax();
            }

            MMR_WRITE(USBD_EP0_WR_DATA, *(data+i));
            //PDEBUG("Interrupt_Send: Length=0x%lx, Data=0x%lx\n", length, *(data+i));

            temp = (i%(USB_MAX_CTRL_PAYLOAD/4))*4;
            temp |= 1 << REG_WOP_SHIFT; //write operation
            temp |= 2 << REG_CSR_SEL_SHIFT;
            temp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;       
            MMR_WRITE(USBD_EP0_CTRL, temp);

            // 64-bytes : FIFO Full
            if (i==(USB_MAX_CTRL_PAYLOAD/4))
            {
                trigger_send = 1;
                // Trigger the send
                MMR_WRITE(USBD_ACK, INT_IN_FIFO_FULL_ACK_MASK);
                //PDEBUG("Interrupt_Send: Length=0x%lx (Trigger the transfer), USBD_INT_STATUS=0x%lx\n", length, MMR_READ(USBD_INT_STATUS));        
                
                if (!bytes_left)
                {
                    // Trigger interrupt on the last chunk of transfer
                    //PDEBUG("Interrupt_Send: Length=0x%lx (Interrupt Enable)\n", length);        
                    temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                    temp1 |= MIPS_INTIN0_INT_EN_EMPTY_MASK;
                    MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
                }
                else
                {
                    // Disable the interrupt if it is not the last chunk of transfer
                    //PDEBUG("Interrupt_Send: Length=0x%lx (Interrupt Disable)\n", length);        
                    temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                    temp1 &= ~MIPS_INTIN0_INT_EN_EMPTY_MASK;
                    MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
                    MMR_WRITE(USBD_INT_STATUS, INTIN0_INT_EMPTY_MASK);
                }                
            }
        }

        // In this case, the data size is less than 64 bytes
        if (!trigger_send)
        {
            // Trigger the send
            MMR_WRITE(USBD_ACK, INT_IN_FIFO_FULL_ACK_MASK);
            //PDEBUG("Interrupt_Send: Length=0x%lx (Trigger the transfer), USBD_INT_STATUS=0x%lx\n", length, MMR_READ(USBD_INT_STATUS));        
            
            if (!bytes_left)
            {
                // Trigger interrupt on the last chunk of transfer
                //PDEBUG("Interrupt_Send: Length=0x%lx (Interrupt Enable)\n", length);        
                temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                temp1 |= MIPS_INTIN0_INT_EN_EMPTY_MASK;
                MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
            }
            else
            {
                // Disable the interrupt if it is not the last chunk of transfer
                //PDEBUG("Interrupt_Send: Length=0x%lx (Interrupt Disable)\n", length);        
                temp1 = MMR_READ(USBD_MIPS_INT_MASK);
                temp1 &= ~MIPS_INTIN0_INT_EN_EMPTY_MASK;
                MMR_WRITE(USBD_MIPS_INT_MASK, temp1);            
                MMR_WRITE(USBD_INT_STATUS, INTIN0_INT_EMPTY_MASK);
            }            
        }
    }

}


//////////////////////////////////////
//
//  ViXS -- Set Address
//
//////////////////////////////////////
static void ch9setaddress(struct vixs *dev, u16 value, u16 index, u16 length)
{
    struct vixs_ep *ep;
    struct vixs_request *req = dev->status_req;

    PDEBUG("ch9setaddress: -- Enter\n");

    // Save the new address to device struct //
    dev->device_address = (u8) value;

    // Update usb state //
    dev->usb_state = USB_STATE_ADDRESS;

    dev->ep0_dir = USB_DIR_IN;
    ep = &dev->ep[0];
    dev->ep0_state = WAIT_FOR_OUT_STATUS;

    req->ep = ep;
    req->req.length = 0;
    req->req.status = -EINPROGRESS;
    req->req.actual = 0;
    req->req.complete = NULL;
    EP0_Send(dev, 0, NULL);
    list_add_tail(&req->queue, &ep->queue);

    PDEBUG("ch9setaddress: -- Leave\n");    
}

 
//////////////////////////////////////
//
//  ViXS -- Get Status
//
//////////////////////////////////////
static void ch9getstatus(struct vixs *dev, u8 request_type, u16 value, u16 index, u16 length)
{
    u16 tmp = 0;		

    struct vixs_request *req;
    struct vixs_ep *ep;
    int ep_status = 0;

    PDEBUG("ch9getstatus: -- Enter\n");

    ep = &dev->ep[0];

    if ((request_type & USB_RECIP_MASK) == USB_RECIP_DEVICE)
    {
        // Get device status //
        tmp = 1 << USB_DEVICE_SELF_POWERED;
        tmp |= dev->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP;
    } 
    else if ((request_type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) 
    {
        // Get interface status //
        tmp = 0;
    } 
    else if ((request_type & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) 
    {
        // Get endpoint status //
        struct vixs_ep *target_ep;

        target_ep = get_ep_by_addr(dev, index);

        //stall if endpoint doesn't exist
        if (!target_ep->desc)
        {
            PDEBUG("ch9getstatus: -- ERROR 1\n");
            if ((ep_index(target_ep) == 0x01)&&(ep_is_in(target_ep)==1))
            {
                // Reset Bulk IN 
                if (!(MMR_READ(USBD_BULKIN0_CTRL)&USBD_BULKIN0_CTRL_ENABLE_MASK))
                {
                    ep_status = 1;
                }
            }
            else if ((ep_index(target_ep) == 0x03)&&(ep_is_in(target_ep)==0))
            {
                // Reset Bulk OUT 
                if (!(MMR_READ(USBD_BULKOUT0_CTRL)&USBD_BULKOUT0_CTRL_ENABLE_MASK))
                {
                    ep_status = 1;
                }
            }
            else if ((ep_index(target_ep) == 0x02)&&(ep_is_in(target_ep)==1))
            {
                // Reset Interrupt IN 
                if (!(MMR_READ(USBD_INTIN0_CTRL)&USBD_INTIN0_CTRL_ENABLE_MASK))
                {
                    ep_status = 1;
                }
            }
        }
        // 0: not stalled; 1:stalled 
        tmp = ep_status << USB_ENDPOINT_HALT;
    }

    dev->ep0_dir = USB_DIR_IN;
    
    req = dev->status_req;
    *((u16 *) req->req.buf) = cpu_to_le16(tmp);
    req->ep = ep;
    req->req.length = 2;
    req->req.status = -EINPROGRESS;
    //req->req.actual = 0;
    req->req.actual = 2;
    req->req.complete = NULL;
    EP0_Send(dev, 2, req->req.buf);
    list_add_tail(&req->queue, &ep->queue);

    dev->ep0_state = DATA_STATE_XMIT;

    PDEBUG("ch9getstatus: -- Leave\n");
    return;
}


//////////////////////////////////////
//
//  ViXS -- SETUP IRQ
//
//////////////////////////////////////
static void USBD_setup_irq (struct vixs *dev, struct usb_ctrlrequest *setup)
{
    struct vixs_ep *ep;
    u16 wValue = le16_to_cpu(setup->wValue);
    u16 wIndex = le16_to_cpu(setup->wIndex);
    u16 wLength = le16_to_cpu(setup->wLength);

    // Set the defaul EP0 direction is IN
    dev->ep[0].is_in=1;
    DBK("Req:%x ReqType:%x Val:%x Idx:%x Len:%d\n",setup->bRequest,setup->bRequestType,wValue,wIndex,wLength);
    switch (setup->bRequest) 
    {
        // Request that need Data+Status phase from udc //
        case USB_REQ_GET_STATUS:            
            //PDEBUG("USBD_setup_irq: bRequestType = 0x%x\n", setup->bRequestType);
            //PDEBUG("USBD_setup_irq: bReqest = USB_REQ_GET_STATUS\n");
            //PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
            
            if ((setup->bRequestType & (USB_DIR_IN | USB_TYPE_STANDARD)) != (USB_DIR_IN | USB_TYPE_STANDARD))
            {
                dev->ep[0].is_in=0;
                goto pass_up;
            }
            ch9getstatus(dev, setup->bRequestType, wValue, wIndex, wLength);
            break;

        // Requests that need Status phase from udc //
        case USB_REQ_SET_ADDRESS:
//    		PDEBUG("USBD_setup_irq: bRequestType = 0x%x\n", setup->bRequestType);
//    		PDEBUG("USBD_setup_irq: bReqest = USB_REQ_SET_ADDRESS\n");
//    		PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
        
            if (setup->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE))
            {
                break;
            }
            ch9setaddress(dev, wValue, wIndex, wLength);
        break; 
        // Handled by udc, no data, status by udc //
        case USB_REQ_CLEAR_FEATURE:
        case USB_REQ_SET_FEATURE:
        {	// status transaction //
            int rc = -EOPNOTSUPP;

            if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) 
            {                
                //PDEBUG("USBD_setup_irq: bRequestType = 0x%x (USB_RECIP_ENDPOINT)\n", setup->bRequestType);
                //if (setup->bRequest & USB_REQ_CLEAR_FEATURE)
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_CLEAR_FEATURE\n");
                //else 
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_SET_FEATURE\n");
                //PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
                
                ep = get_ep_by_addr(dev, wIndex);

                if (wValue != 0 || wLength != 0)
                {
                    break;
                }

                spin_unlock(&dev->lock);
                rc = vixs_set_halt(&ep->ep, (setup->bRequest == USB_REQ_SET_FEATURE) ? 1 : 0);
                spin_lock(&dev->lock);
            } 
            else if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) 
            {                
                //PDEBUG("USBD_setup_irq: bRequestType = 0x%x (USB_RECIP_DEVICE)\n", setup->bRequestType);
                //if (setup->bRequest & USB_REQ_CLEAR_FEATURE)
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_CLEAR_FEATURE\n");
                //else 
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_SET_FEATURE\n");
                //PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
                
                if (!dev->gadget.is_otg)
                {
                    break;
                }
                else if (setup->bRequest == USB_DEVICE_B_HNP_ENABLE)
                {
                    dev->gadget.b_hnp_enable = 1;
                }
                else if (setup->bRequest == USB_DEVICE_A_HNP_SUPPORT)
                {
                    dev->gadget.a_hnp_support = 1;
                }
                else if (setup->bRequest == USB_DEVICE_A_ALT_HNP_SUPPORT)
                {
                    dev->gadget.a_alt_hnp_support = 1;
                }
                rc = 0;
            }
            else
            {                
                //PDEBUG("USBD_setup_irq: bRequestType = 0x%x (Unknow)\n", setup->bRequestType);
                //if (setup->bRequest & USB_REQ_CLEAR_FEATURE)
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_CLEAR_FEATURE\n");
                //else 
                //    PDEBUG("USBD_setup_irq: bReqest = USB_REQ_SET_FEATURE\n");
                //PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
                
                goto pass_up;
            }

            break;
        }
        // Requests handled by gadget //
        default:
pass_up:
            if (wLength) 
            {
                if ((setup->bRequest != USB_REQ_GET_STATUS) && 
                    (setup->bRequest != USB_REQ_CLEAR_FEATURE) && 
                    (setup->bRequest != USB_REQ_SET_ADDRESS) &&
                    (setup->bRequest != USB_REQ_SET_FEATURE))
                {
//                    PDEBUG("USBD_setup_irq: bRequestType = 0x%x\n", setup->bRequestType);
//                    PDEBUG("USBD_setup_irq: bReqest = 0x%x\n", setup->bRequest);
//                    PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);
                }
                // Data phase from gadget, status phase from udc //
                dev->ep0_dir = (setup->bRequestType & USB_DIR_IN) ? USB_DIR_IN : USB_DIR_OUT;
                spin_unlock(&dev->lock);

                if (dev->driver->setup(&dev->gadget, setup) < 0)
                {         
                    //ep0stall(dev);
                    PDEBUG("===>>> EP0 - Stall\n");
                }                
                spin_lock(&dev->lock);

                dev->ep0_state = (setup->bRequestType & USB_DIR_IN) ? DATA_STATE_XMIT : DATA_STATE_RECV;
            } 
            else 
            {
                if ((setup->bRequest != USB_REQ_GET_STATUS) && 
                    (setup->bRequest != USB_REQ_CLEAR_FEATURE) && 
                    (setup->bRequest != USB_REQ_SET_ADDRESS) &&
                    (setup->bRequest != USB_REQ_SET_FEATURE))
                {                  
//                    PDEBUG("USBD_setup_irq: bRequestType = 0x%x\n", setup->bRequestType);
//                    PDEBUG("USBD_setup_irq: bReqest = 0x%x\n", setup->bRequest);
//                    PDEBUG("USBD_setup_irq: wValue = 0x%x : wIndex = 0x%x : wLength = 0x%x\n", wValue, wIndex, wLength);                  
                }
                // No data phase, IN status from gadget //
                dev->ep0_dir = USB_DIR_IN;

				if(setup->bRequest == USB_REQ_SET_CONFIGURATION)
				{
					dev->mCurConfiguration = setup->wValue;
				}
				else if(setup->bRequest == USB_REQ_SET_INTERFACE)
				{
					dev->mCurInterface= setup->wIndex;
					dev->mCurAltSetting = setup->wValue;
				}

                spin_unlock(&dev->lock);

                if (dev->driver->setup(&dev->gadget, setup) < 0)
                {
                    //ep0stall(dev);
                    PDEBUG("USBD_setup_irq: EP0 -- Stall\n");
                }

                spin_lock(&dev->lock);
                dev->ep0_state = WAIT_FOR_OUT_STATUS;
            }
            break;
    }   
}


//////////////////////////////////////
//
//  ViXS -- IRQ (Intrrupt Handler)
//
//////////////////////////////////////
static irqreturn_t vixs_irq (int irq, void *_dev)
{
    struct vixs		*dev = _dev;
    struct vixs_ep  *ep;
    struct vixs_request *req;
    struct list_head *next_element;

    unsigned int retval = FALSE;
    unsigned int interruptvalue;
    unsigned int interruptstatus;
    unsigned int temp=0;
    //unsigned int i=0;
    unsigned int bytes_received=0;
    unsigned int num_of_entry=0;

    spin_lock (&dev->lock);

    interruptvalue = MMR_READ (CPU_INTERRUPT);

    if(!(interruptvalue & CPU_INTERRUPT_USBD_INT_MASK))
    {        
        spin_unlock (&dev->lock);
        return IRQ_NONE;
    }

    retval = TRUE;
    //PDEBUG("\n");
    PDEBUG("1: @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Enter\n");

    interruptstatus = MMR_READ(USBD_INT_STATUS);
    interruptstatus &= MMR_READ(USBD_MIPS_INT_MASK);

    PDEBUG("interruptstatus:%x\n",interruptstatus);

    // Device Speed
    if (interruptstatus & INT_SPEED_MASK)
    {
        USBD_speed_irq(dev);
        MMR_WRITE(USBD_INT_STATUS, INT_SPEED_MASK);
    }
    
    // Bulk In request completed
    // [Jlwang] TODO: Since USBD will keep generating interrupt once IN fifo is empty, we may need to disable this interrupt 
    // once we find there is nothing to send and enable it once there is something.
    if (interruptstatus & (BULKIN0_INT_TRANS_DONE_MASK|BULKIN1_INT_TRANS_DONE_MASK|BULKIN2_INT_TRANS_DONE_MASK|BULKIN3_INT_TRANS_DONE_MASK|BULKIN4_INT_TRANS_DONE_MASK))
    {
    	
        PDEBUG("@@@@@@@@@@@@@@@@@@@@ Vixs_irq: Bulk_in Transfer Done\n");

        // make sure any leftover request state is cleared //
        MMR_WRITE(USBD_INT_STATUS, interruptstatus & (BULKIN0_INT_TRANS_DONE_MASK|BULKIN1_INT_TRANS_DONE_MASK|BULKIN2_INT_TRANS_DONE_MASK|BULKIN3_INT_TRANS_DONE_MASK|BULKIN4_INT_TRANS_DONE_MASK));
        Submit_Dpc_Tasklet(dev, dev->KdpcBulkIn);               
    }

    // Bulk Out request completed
    if (interruptstatus & (BULKOUT0_INT_TRANS_DONE_MASK|BULKOUT1_INT_TRANS_DONE_MASK|BULKOUT2_INT_TRANS_DONE_MASK|BULKOUT3_INT_TRANS_DONE_MASK))
    {
    	
        PDEBUG("@@@@@@@@@@@@@@@@@@@@ Vixs_irq: Bulk_out Transfer Done\n");
        // make sure any leftover request state is cleared //
        MMR_WRITE(USBD_INT_STATUS, interruptstatus & (BULKOUT0_INT_TRANS_DONE_MASK|BULKOUT1_INT_TRANS_DONE_MASK|BULKOUT2_INT_TRANS_DONE_MASK|BULKOUT3_INT_TRANS_DONE_MASK));
        Submit_Dpc_Tasklet(dev, dev->KdpcBulkOut);               
    }

    // Interrupt In request completed
    if (interruptstatus & INTIN0_INT_EMPTY_MASK)
    {
        ep = &dev->ep [10];
        PDEBUG("Vixs_irq: Interrupt In Transfer Done %s\n", ep->ep.name);
        
        // make sure any leftover request state is cleared //
        while (!list_empty (&ep->queue)) 
        {
            req = list_entry (ep->queue.next, struct vixs_request, queue);
            done (ep, req, (req->req.actual == req->req.length)? 0 : -EPROTO);
        }

        // Disable the interrupt mask
        temp = MMR_READ(USBD_MIPS_INT_MASK);
        temp &= ~MIPS_INTIN0_INT_EN_EMPTY_MASK;
        MMR_WRITE(USBD_MIPS_INT_MASK, temp);
        
        MMR_WRITE(USBD_INT_STATUS, INTIN0_INT_EMPTY_MASK);
    }

    // Endpoint Control request completed
    if (interruptstatus & EP0_INT_EP0_IN_EMPTY_MASK)
    {
        ep = &dev->ep [0];
        PDEBUG("Vixs_irq: Control Transfer Done(1): %s, USBD_INT_STATUS=0x%lx\n", ep->ep.name, MMR_READ(USBD_INT_STATUS));                
        // make sure any leftover request state is cleared //
        while (!list_empty (&ep->queue)) 
        {
            req = list_entry (ep->queue.next, struct vixs_request, queue);
            done (ep, req, (req->req.actual == req->req.length)? 0 : -EPROTO);
        }
        //PDEBUG("Vixs_irq: Control Transfer Done(2): %s, USBD_INT_STATUS=0x%lx\n", ep->ep.name, MMR_READ(USBD_INT_STATUS));
        // Disable the interrupt mask
        temp = MMR_READ(USBD_MIPS_INT_MASK);
        temp &= ~MIPS_EP0_INT_EN_EP0_IN_EMPTY_MASK;
        MMR_WRITE(USBD_MIPS_INT_MASK, temp);
        
        //PDEBUG("Vixs_irq: Control Transfer Done(3): %s, USBD_INT_STATUS=0x%lx\n", ep->ep.name, MMR_READ(USBD_INT_STATUS));                
        MMR_WRITE(USBD_INT_STATUS, EP0_INT_EP0_IN_EMPTY_MASK);
        //PDEBUG("Vixs_irq: Control Transfer Done(4): %s, USBD_INT_STATUS=0x%lx\n\n", ep->ep.name, MMR_READ(USBD_INT_STATUS));                
    }
    
    // Setup packet received
    if (interruptstatus & EP0_INT_SETUP_RCVD_MASK)
    {
        // Read the setup packet
        PDEBUG("setup _packet is %x\n", dev->setup_packet);
        *dev->setup_packet = MMR_READ(USBD_SETUP_CMD0);
        *(dev->setup_packet+1) = MMR_READ(USBD_SETUP_CMD1);   
        PDEBUG("\n");                                     
        PDEBUG("Vixs_irq: Setup Packet -- 0x%lx 0x%lx : USBD_INT_STATUS=0x%lx\n", *dev->setup_packet, *(dev->setup_packet+1), MMR_READ(USBD_INT_STATUS));
        
        // Indicate firmware has get the setup packet 
        MMR_WRITE(USBD_ACK, MMR_READ(USBD_ACK)|SETUP_CMD_EMPTY_ACK_MASK);

        USBD_setup_irq(dev, (struct usb_ctrlrequest *)dev->setup_packet);

        // Clear setup up packet interrupt
        MMR_WRITE(USBD_INT_STATUS, EP0_INT_SETUP_RCVD_MASK);
        //PDEBUG("Vixs_irq: Setup Packet Clear Int : USBD_INT_STATUS=0x%lx\n", MMR_READ(USBD_INT_STATUS));
    }

    // Set Configuration
    if (interruptstatus & INT_SETCONFIGURATION_MASK)
    {
    	temp = MMR_READ(USBD_UDC20_STATUS0);
		temp = (temp&UDC20_CFG_MASK)>>UDC20_CFG_SHIFT;
		
        *dev->setup_packet = 0x0900|(temp<<16);
        *(dev->setup_packet+1) = 0x00000000;   
                //PDEBUG("Vixs_irq: Set Configuration-- 0x%lx 0x%lx\n", *dev->setup_packet, *(dev->setup_packet+1));
        USBD_setup_irq(dev, (struct usb_ctrlrequest *)dev->setup_packet);

        // Clear setup up packet interrupt
        MMR_WRITE(USBD_INT_STATUS, INT_SETCONFIGURATION_MASK);
    }

    // Set Interface
    if (interruptstatus & INT_SETINTERFACE_MASK)
    {

    	temp = MMR_READ(USBD_UDC20_STATUS0);

        *dev->setup_packet = 0x0B01|(((temp&UDC20_ALTINTF_MASK)>>UDC20_ALTINTF_SHIFT)<<16);
        *(dev->setup_packet+1) = ((temp&UDC20_INTF_MASK)>>UDC20_INTF_SHIFT);                                        
//                PDEBUG("Vixs_irq: Set Interface-- 0x%lx 0x%lx\n", *dev->setup_packet, *(dev->setup_packet+1));
        USBD_setup_irq(dev, (struct usb_ctrlrequest *)dev->setup_packet);

        // Clear setup up packet interrupt
        MMR_WRITE(USBD_INT_STATUS, INT_SETINTERFACE_MASK);
    }

    // Reset
    if (interruptstatus & INT_RESET_MASK)
    {
        USBD_reset_irq(dev);
        MMR_WRITE(USBD_INT_STATUS, INT_RESET_MASK);
    }

    // Suspend
    if (interruptstatus & INT_SUSPEND_MASK)
    {
        USBD_suspend_irq(dev);
        MMR_WRITE(USBD_INT_STATUS, INT_SUSPEND_MASK);           
    }

    // Wakeup
    if (interruptstatus & INT_WAKEUP_MASK)
    {
        USBD_wakeup_irq(dev);
        MMR_WRITE(USBD_INT_STATUS, INT_WAKEUP_MASK);
    }   

    spin_unlock (&dev->lock);
    PDEBUG("Vixs_irq: @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Leave\n");

    return IRQ_HANDLED;
}

//-------------------------------------------------------------------------//

//////////////////////////////////////
//
//  ViXS -- Gadget Release
//
//////////////////////////////////////
static void gadget_release (struct device *_dev)
{
    struct vixs	*dev = dev_get_drvdata (_dev);
    PDEBUG("gadget_release: -- dev = 0x%lx\n", dev);
    kfree (dev);
}

//////////////////////////////////////
//
//  ViXS -- Probe
//
//////////////////////////////////////
static int vixs_probe (struct platform_device *pdev)
{
    struct vixs         *dev;
    int                 retval;
    int                 temp;

    PDEBUG("Vixs_Probe: -- Enter\n");
    if (the_controller) 
    {
        PDEBUG("Vixs_Probe: -- Leave : ERROR 01\n");
        return -EBUSY;
    }

    // alloc, and start init
    dev = kzalloc (sizeof *dev, GFP_KERNEL);
    if (dev == NULL)
    {
        retval = -ENOMEM;
        goto done;
    }
    else
    {
        memset(dev, 0, sizeof(struct vixs));
        PDEBUG("Vixs_Probe: -- dev = 0x%lx\n", dev);    
    }
    dev->resume_state = USB_STATE_NOTATTACHED;
    dev->usb_state = USB_STATE_POWERED;
    dev->ep0_dir = 0;
    dev->remote_wakeup = 0; // default to 0 on reset

    spin_lock_init (&dev->lock);

    dev->dev = &pdev->dev;
    dev->gadget.ops = &vixs_ops;
    dev->gadget.is_dualspeed = 1;
    dev->gadget.ep0 = &dev->ep[0].ep;
    // Make the double link list point to itself
    INIT_LIST_HEAD(&dev->gadget.ep_list);

    // the "gadget" abstracts/virtualizes the controller
    dev_set_name(&dev->gadget.dev, "gadget");
    dev->gadget.dev.parent = &pdev->dev;
    dev->gadget.dev.dma_mask = pdev->dev.dma_mask;
    dev->gadget.dev.release = gadget_release;

    if(!vixs_usbd_skip_enum)
    dev->gadget.speed = USB_SPEED_UNKNOWN;
    else
    dev->gadget.speed = USB_SPEED_HIGH;

    dev->gadget.name = driver_name;

    if (device_register (&dev->gadget.dev) < 0)
    {
        PDEBUG("Vixs_Probe: -- device_register Fail\n");    
        retval = -ENODEV;
        goto done;
    }
    else
    {
        PDEBUG("Vixs_Probe: -- device_register Success\n");    
    }
    dev->enabled = 1;

    // Start Initialize XC3 USBD PCI device
    dev->physfb = 0x80000000; // Above 2G is all kernel memory space
    dev->fbsize = 0x4000000; // make it 64M
    dev->pmmfb = ioremap_nocache(dev->physfb, dev->fbsize);
    PDEBUG("Vixs_Probe: Virtual Frame Buffer Base Addr = 0x%lx\n", dev->pmmfb);

    dev->physmmr = VIXS_MMR_BASE;
    dev->mmrsize = 0x10000; //64k
    dev->pmmr = (unsigned char *)VIXS_MMR_BASE;
    PDEBUG("Vixs_Probe: Virtual Register Base Addr = 0x%lx\n", dev->pmmr);

    the_controller = dev;

    //usb_reset (dev);
    usb_reinit (dev);

    // Endpoint Zero (Control) Descriptor define here
    // For all other endpoint, gadget layer called ep_enable with defined desc
    dev->ep[0].desc = &vixs_ep0_desc;
    dev->ep[0].ep.maxpacket = USB_MAX_CTRL_PAYLOAD;
    
    // Buffer to hold the setup packet    
    dev->setup_packet = (unsigned int *) kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
    PDEBUG("Vixs_Probe: Setup Packet Address = 0x%lx\n", dev->setup_packet);
#if 0
    for (i=0; i<MAX_TRANS_DESC; i++)
    {
        dev->inter_out_buffer[i] = (unsigned int)kmalloc((2*1024), GFP_KERNEL);
        PDEBUG("Vixs_Probe: Inter Out Buffer Address[%d] = 0x%lx\n", i, dev->inter_out_buffer[i]);
    }

    for (i=0; i<MAX_TRANS_DESC; i++)
    {
        dev->inter_in_buffer[i] = (unsigned int)kmalloc((2*1024), GFP_KERNEL);
        PDEBUG("Vixs_Probe: Inter IN Buffer Address[%d] = 0x%lx\n", i, dev->inter_in_buffer[i]);
    }
#endif        
    //Allocate status request
    //Initialize ep0 status request structure
    //FIXME: vixs_alloc_request() ignores ep argument
    dev->status_req = container_of(vixs_alloc_request(NULL, GFP_KERNEL), struct vixs_request, req);
    // allocate a small amount of memory to get valid address
    dev->status_req->req.buf = kmalloc(8, GFP_KERNEL);
    dev->status_req->req.dma = virt_to_phys(dev->status_req->req.buf);
    
    dev->Pci_Interrupt_Line = 6;
    if (request_irq (dev->Pci_Interrupt_Line, vixs_irq, IRQF_SHARED, driver_name, dev)!= 0) 
    {
        PDEBUG ("Vixs_Probe: Request interrupt %d failed\n", dev->Pci_Interrupt_Line);
        retval = -EBUSY;
        dev->Pci_Interrupt_Line = 0;
        goto done;
    }
    else
    {
        PDEBUG("Vixs_Probe: Hook IRQ Service Routine Successfully: dev=0x%x\n", dev);
        dev->got_irq = 1;
    }

    // Initialize Tasklet
    memcpy(dev->KdpcBulkOut, &BulkOut_tasklet, sizeof(struct tasklet_struct));
    ((struct tasklet_struct *)dev->KdpcBulkOut)->data = (uint)dev;
       
    memcpy(dev->KdpcBulkIn, &BulkIn_tasklet, sizeof(struct tasklet_struct));
    ((struct tasklet_struct *)dev->KdpcBulkIn)->data = (uint)dev;

    return 0;

done:
    // Only come to here when there's error occures
    if (dev)
    {
        temp = vixs_remove (pdev);
    }
    return retval;
}


//////////////////////////////////////
//
//  ViXS -- Remove
//
//////////////////////////////////////
static int vixs_remove (struct platform_device *pdev)
{
    int i=0;
    PDEBUG("Vixs Remove - Enter\n");

    // Clean up the resources which allocated during probe()
    if (the_controller->setup_packet)
    {
        PDEBUG("Vixs Remove - free setup packet\n");
        kfree(the_controller->setup_packet);
    }

#if 0
    for (i=0; i<MAX_TRANS_DESC; i++)
    {
        if (the_controller->inter_out_buffer[i])
        {
            PDEBUG("Vixs Remove - free inter out buffer[%d]\n", i);
            kfree((pvoid)the_controller->inter_out_buffer[i]);
        }
    }

    for (i=0; i<MAX_TRANS_DESC; i++)
    {
        if (the_controller->inter_in_buffer[i])
        {
            PDEBUG("Vixs Remove - free inter in buffer[%d]\n", i);
            kfree((pvoid)the_controller->inter_in_buffer[i]);
        }
    }
#endif

    if (the_controller->status_req->req.buf)
    {
        PDEBUG("Vixs Remove - free buffer inside status request\n");
        kfree(the_controller->status_req->req.buf);
    }
    
    if (the_controller->status_req)
    {
        PDEBUG("Vixs Remove - free status request\n");
        kfree(the_controller->status_req);
    }
       
    if (the_controller->got_irq)
    {
        PDEBUG("Vixs Remove - free IRQ\n");   
        free_irq (the_controller->Pci_Interrupt_Line , the_controller);
    }

    if (the_controller->pmmfb)
    {
        PDEBUG("Vixs Remove - Unmap frame buffer\n");    
        iounmap (the_controller->pmmfb);
    }

    if (the_controller->enabled)
    {
        PDEBUG("Vixs Remove - Disable PCI\n");    
    }

    device_unregister (&the_controller->gadget.dev);
    //device_remove_file (&pdev->dev, &dev_attr_registers);  

    the_controller = NULL;
    PDEBUG("Vixs Remove - Leaving\n");
    return 0;
}


//////////////////////////////////////
//
//  ViXS -- Shutdown
//
//////////////////////////////////////

static void vixs_shutdown(struct platform_device *pdev /*, pm_message_t state */)
{
    unsigned int        temp;
    PDEBUG("Vixs Shutdown - Enter\n");
    // Disable  the USBD block//
    temp = MMR_READ(CG_RESET_REG);
    temp |= USBD_RESET_MASK;
    MMR_WRITE(CG_RESET_REG, temp);
    PDEBUG("Vixs Shutdown - (CG_RESET_REG=0x%lx) Leave\n", MMR_READ(CG_RESET_REG));
}

//////////////////////////////////////
//
//  ViXS -- Resume
//
//////////////////////////////////////
//static int vixs_resume(struct platform_device *pdev)
//{
//	return 0;
//}


//-------------------------------------------------------------------------//
// pci driver glue; this is a "new style" PCI driver module //
static struct platform_driver vixs_pci_driver = {
	.probe   = vixs_probe,
       .shutdown = vixs_shutdown,
	.remove  = vixs_remove,
	.driver  = {
		.name = XC_DRV_NAME,
	},
};

static struct platform_device *xcode_usbd_device = NULL;
static struct resource xcode_usbd_resources[2];


//////////////////////////////////////
//
//  ViXS -- Init module
//
//////////////////////////////////////
static int __init init (void)
{
    int error;
    u32 temp;
    
    temp = readl(USBD_EP0_CTRL + VIXS_MMR_BASE);
    if(((temp & USBD_EP0_CTRL_OP_MODE_MASK)>> USBD_EP0_CTRL_OP_MODE_SHIFT) == VIXS_USBD_OP_MODE)
    {
        vixs_usbd_skip_enum= 1;
    }
   	printk("0 temp:%x vixs_usbd_skip_enum:%x\n",temp,vixs_usbd_skip_enum);


	if(!vixs_usbd_skip_enum)
	{
		PDEBUG("%s : Gadget Driver Init July 23, 2008:(%s)\n", driver_desc, DRIVER_VERSION);

		temp = readl(CG1_CLK_STOP0 + VIXS_MMR_BASE);
		temp &= ~CG1_CLK_STOP0_UCLK_STOP_MASK;
		writel(temp, CG1_CLK_STOP0 + VIXS_MMR_BASE);
		temp = readl(CG1_BLK_CLK_STOP0 + VIXS_MMR_BASE);
		temp &= ~CG1_BLK_CLK_STOP0_USBD_MCLK_STOP_MASK;
		writel(temp, CG1_BLK_CLK_STOP0 + VIXS_MMR_BASE);

	    // Take USBD module Out of reset
	    temp = readl (CG_RESET_REG + VIXS_MMR_BASE);
	    temp &= ~USBD_RESET_MASK;
	    writel(temp, (CG_RESET_REG+VIXS_MMR_BASE));
	    PDEBUG("Gadget Driver Init : CG_RESET_REG=(0x%lx)\n", temp);
	    udelay(1000);

	    // Configurate to Communication class
	    temp = readl (USBD_EP0_CTRL + VIXS_MMR_BASE);
	    temp |= VIXS_USBD_OP_MODE << USBD_EP0_CTRL_OP_MODE_SHIFT;
	    writel(temp, (USBD_EP0_CTRL+VIXS_MMR_BASE));
	    PDEBUG("Gadget Driver Init : USBD_EP0_CTRL=(0x%lx)\n", temp);
	    udelay(1000);

	    // Put device into full speed    
	    //temp = readl (USBD_UDC20_CTRL+ VIXS_MMR_BASE);
	    //temp |= (1<<USBD_UDC20_CTRL_APP_EXP_SPEED_SHIFT);
	    //writel(temp, (USBD_UDC20_CTRL+VIXS_MMR_BASE));
	    //PDEBUG("Gadget Driver Init : USBD_UDC20_CTRL=(0x%lx)\n", temp);
	    //udelay(1000);    

	    // Put VBUS in Reset
	    //temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
	    //temp &= ~(USBD_PHY_CTRL_DETECT_VBUS_0_ENABLE_MASK|USBD_PHY_CTRL_DETECT_VBUS_1_ENABLE_MASK);
	    //writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
	    //PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(VBUS)=(0x%lx)\n", temp);
	    //udelay(1000);

	    // Put the Phy in Suspend state
	    //temp = readl (USBD_UDC20_CTRL + VIXS_MMR_BASE);
	    //temp &= ~(USBD_UDC20_CTRL_PHY_SUSPEND_ENABLE_MASK);
	    //writel(temp, (USBD_UDC20_CTRL+VIXS_MMR_BASE));
	    //PDEBUG("Gadget Driver Init : USBD_UDC20_CTRL(Phys)=(0x%lx)\n", temp);
	    //udelay(1000);

	    // Since PLL2 did not initialize, we have to use external osc. clock
	    writel(0x0, (USBD_PHY_CLK_CTRL+VIXS_MMR_BASE));
	    PDEBUG("Gadget Driver Init : USBD_PHY_CLK_CTRL=(0x%lx)\n", readl (USBD_PHY_CLK_CTRL + VIXS_MMR_BASE));
	    udelay(1000);

	    // Put the Phy in Suspend state
	    temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
	    temp &= ~(PHY_PORT1_SUSPENDM_ENB_MASK);
	    writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
	    PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
	    udelay(1000);
	    
	    temp = readl (USBD_PHY_CTRL + VIXS_MMR_BASE);
	    temp &= ~(PHY_PORT1_VBUSVLDEXT_ENB_MASK);
	    writel(temp, (USBD_PHY_CTRL+VIXS_MMR_BASE));
	    PDEBUG("Gadget Driver Init : USBD_PHY_CTRL(Phys)=(0x%lx)\n", temp);
	    udelay(1000);

	    // Clear all exiting USBD inturrput in MIP4
	    writel (CPU_INTERRUPT_USBD_INT_MASK, CPU_INTERRUPT + VIXS_MMR_BASE);
	    PDEBUG("Gadget Driver Init : CPU_INTERRUPT = 0x%lx\n", readl (CPU_INTERRUPT + VIXS_MMR_BASE));

	    // Clear all exiting USBD inturrput 
	    writel (0xFFFFFFFF, USBD_INT_STATUS + VIXS_MMR_BASE);
	    PDEBUG("Gadget Driver Init : USBD_INT_STATUS = 0x%lx\n", readl (CPU_INTERRUPT + VIXS_MMR_BASE));

	    // Take the USBD out of soft reset
	    writel(0xFFFFFFFF, (USBD_SOFT_RST+VIXS_MMR_BASE));
	    udelay(1000);
	    writel(0, (USBD_SOFT_RST+VIXS_MMR_BASE));
	    udelay(1000);
	    PDEBUG("Gadget Driver Init : USBD_SOFT_RST=(0x%lx)\n", temp);

	}
	else
	{
		PDEBUG("Gadget Driver Init with USB_SKIP_ENUMERATION\n");
		// Clear all exiting USBD inturrput 
		writel (0xFFFFFFFF, USBD_INT_STATUS + VIXS_MMR_BASE);
		PDEBUG("Gadget Driver Init : USBD_INT_STATUS = 0x%lx\n", readl (CPU_INTERRUPT + VIXS_MMR_BASE));
	}
    // allocate platform driver structure
    xcode_usbd_device = platform_device_alloc(XC_DRV_NAME, -1);
    if (!xcode_usbd_device)
    {
        PDEBUG("Gadget Driver Init : platform_device_alloc failure\n");
        error = -ENOMEM;
        return error;
    }
    else
    {
        PDEBUG("Gadget Driver Init : platform_device_alloc success\n");
    }

    //memory resources
    xcode_usbd_resources[0].start = (VIXS_MMR_BASE + 0x2400);
    xcode_usbd_resources[0].end = (VIXS_MMR_BASE + 0x2400 + 0x200 -1);
    xcode_usbd_resources[0].flags = IORESOURCE_MEM;
    //irq resources
    xcode_usbd_resources[1].start = 0x6;
    xcode_usbd_resources[1].end = 0;
    xcode_usbd_resources[1].flags = IORESOURCE_IRQ;
    error = platform_device_add_resources(xcode_usbd_device, xcode_usbd_resources, 2);
    if (error)
    {
        PDEBUG("Gadget Driver Init : platform_device_add_resources failure\n");
        goto err_free_device;
    }
    else
    {
        PDEBUG("Gadget Driver Init : platform_device_add_resources success\n");
    }

    error = platform_device_add(xcode_usbd_device);
    if (error)
    {
        PDEBUG("Gadget Driver Init : platform_device_add failure\n");
        goto err_free_device;
    }
    else
    {
        PDEBUG("Gadget Driver Init : platform_device_add success\n");
    }

    error = platform_driver_register (&vixs_pci_driver);
    if (error < 0)
    {
        PDEBUG("Gadget Driver Init : platform_driver_register failure\n");
        goto err_free_device;
    }
    else
    {
        PDEBUG("Gadget Driver Init : platform_driver_register success\n");
    }

   
    return 0; 

err_free_device:
    platform_device_put(xcode_usbd_device);
    return error;
}
module_init (init);

//////////////////////////////////////
//
//  ViXS -- Clean up module
//
//////////////////////////////////////
static void __exit cleanup (void)
{
    PDEBUG("%s : Gadget Driver cleanup: (%s)\n", driver_desc, DRIVER_VERSION);
    if(xcode_usbd_device)
    {
        platform_device_del(xcode_usbd_device);
        xcode_usbd_device = NULL;
        platform_driver_unregister (&vixs_pci_driver);
    }
}

module_exit (cleanup);

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("DRIVER_AUTHOR");
MODULE_LICENSE ("GPL");

