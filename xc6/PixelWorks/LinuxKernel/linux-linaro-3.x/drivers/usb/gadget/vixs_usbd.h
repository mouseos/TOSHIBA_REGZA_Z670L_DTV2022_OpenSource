/*
 * Copyright (C) 2007 ViXS System Inc. (http://www.vixs.com)
 * Copyright (C) 2007 Alan Tong
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

/*-------------------------------------------------------------------------*/
#include <linux/time.h>
#include <plat/xcodeRegDef.h>

//#define IC_DEBUG
#ifdef  IC_DEBUG
#ifdef __KERNEL__

static inline void printk_time(void);
#define DBK(fmt, args...) printk("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBK_LOC	printk("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBK_LOC_T printk_time();printk("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBKT(fmt, args...) printk_time();printk("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
static inline void printk_time(void)
{
	struct timespec tv;
	getnstimeofday(&tv);
	printk("%03ds %03dms %03dus",(int) tv.tv_sec % 1000,(int )tv.tv_nsec / 1000000, (int) (tv.tv_nsec %  1000000)/1000) ;
}

static inline void dump_packet(unsigned char* buf, int size)
{
    int i,j;
    int row = size/16;
    int col = size%16;
    unsigned char* cur=buf;
	// Reza modified
	int c, index = 0;
	printk("%s, size:%d\n", __FUNCTION__, size);
    for (i=0;i<row;i++)
    {
        printk("%02x|",i<<4);
        for(j=0;j<16;j++)
        {
             printk("%02x ", cur[index]);
             index++;
        }
		index -= 16;
		printk(" ");
		for (j = 0; j < 16; j++) {
        	c = cur[index];
	        if (c < 32 || c > 127)
    	        c = '.';
        	printk("%c", c);
			index++;
    	}
        printk("\n");
    }
	if(col) {
		printk("%02x|", row << 4);
		for(i = 0; i < col; i++) {
			printk("%02x ", cur[index]);
			index++;
		}
		index -= col;
		for(i = col; i < 16; i++) {
			printk("   ");
		}
		printk(" ");
		for(i = 0; i < col; i++) {
			c = cur[index];
	        if (c < 32 || c > 127)
    	        c = '.';
        	printk("%c", c);
			index++;
		}
		printk("\n");
	}
}

#define ALL_MSG "XCode, "
#define DEBUG_MSG_DISPLAY_LEVEL KERN_INFO

// Reza removed inline
static void PDEBUG(char *fmt, ...)
{
    va_list ap;
    char s[256];

    va_start(ap, fmt);

    vsnprintf(s, sizeof(s), fmt, ap);
    printk(DEBUG_MSG_DISPLAY_LEVEL ALL_MSG "%s", s);

    va_end(ap);
}


#else
#include <stdio.h>
#include <sys/time.h>
static inline void printf_time(void);
#define DBF(fmt, args...) printf("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBF_LOC	printf("[%s:%d]\n",__FUNCTION__,__LINE__)
#define DBFT(fmt, args...) printf_time();printf("[%s:%d]"fmt,__FUNCTION__,__LINE__,## args)
#define DBF_LOC_T	printf_time();printf("[%s:%d]\n",__FUNCTION__,__LINE__)

static inline void printf_time(void)
{
	struct timeval start;
	gettimeofday(&start, NULL);
	printf("%03ds %03dms %03dus",(int) start.tv_sec % 1000,(int) start.tv_usec/1000,(int)start.tv_usec);
}
#endif
#else
#define DBK
#define DBK_LOC
#define DBF
#define DBF_LOC

// Reza modified to remove warning
//#define PDEBUG
static void PDEBUG(char *fmt, ...){}

#define dump_packet

#endif

/* ep0 transfer state */
#define WAIT_FOR_SETUP          0
#define DATA_STATE_XMIT         1
#define DATA_STATE_NEED_ZLP     2
#define WAIT_FOR_OUT_STATUS     3
#define DATA_STATE_RECV         4

#define EP_DIR_IN	1
#define EP_DIR_OUT	0

// USB Speed
#define VIXS_HIGH_SPEED 0
#define VIXS_FULL_SPEED 1
#define VIXS_LOW_SPEED  2

#define NUM_OF_EP 11
#define MAX_TRANS_DESC 4

#define VIXS_USBD_OP_MODE	3


// Endpoint zero descriptor
#define USB_MAX_EP_FIFO             64
#define USB_MAX_CTRL_PAYLOAD        64
#define USB_MAX_BULK_PAYLOAD        512
#define USB_MAX_BULK_PAYLOAD_FULL   64


#define MAX_TRANSFER_BYTES          ((64*1024)-1)
//#define VIXS_MMR_BASE               0xF0000000
#define VIXS_MMR_BASE               XC_SOC_PROC_MMREG_BASE

#ifndef uint8
typedef unsigned char uint8;
#endif

#ifndef uint_ptr
typedef unsigned int * uint_ptr;
#endif

/* ep a-f highspeed and fullspeed maxpacket, addresses
 * computed from ep->num
 */
#define REG_EP_MAXPKT(dev,num) (((num) + 1) * 0x10 + (((dev)->gadget.speed == USB_SPEED_HIGH) ? 0 : 1))


/* DRIVER DATA STRUCTURES and UTILITIES */

struct vixs_ep {
    struct usb_ep                           ep;
    struct list_head                        queue;
    struct vixs                             *dev;
    struct usb_gadget                       *gadget;
    const struct usb_endpoint_descriptor    *desc;

    unsigned int                num_of_desc_submitted;

    unsigned int                pending_request;

    char                                    name[14];
    unsigned    num:8,
                fifo_size:12,
                stopped:1,
                is_in:1,
                responded:1;
};

struct vixs_request {
	struct usb_request		req;
	struct list_head		queue;
	struct vixs_ep          *ep;
    unsigned                insert;
	unsigned                mapped : 1,
                            valid : 1;
};

struct vixs {
    struct usb_gadget           gadget;
    struct usb_gadget_driver    *driver;
    struct vixs_ep              ep[NUM_OF_EP];
    struct vixs_request         *status_req;
    struct device 		      *dev;

    unsigned                    enabled : 1,
                                protocol_stall : 1,
                                softconnect : 1,
                                got_irq : 1,
                                region : 1;

    unsigned int                max_ep;
    unsigned int                Pci_Interrupt_Line;
    unsigned int                Pci_HW_Interrupt_Line;

    unsigned int*               setup_packet; 
//    unsigned int                inter_out_buffer[MAX_TRANS_DESC];
//    unsigned int                inter_in_buffer[MAX_TRANS_DESC];
//    unsigned int                recv_buffer_index;
//    unsigned int                out_buffer_index;
//    unsigned int                in_buffer_index;

    spinlock_t                  lock;
	spinlock_t                  queue_lock;

    unsigned                    vbus_active:1;
    unsigned                    stopped:1;
    unsigned                    remote_wakeup:1;

    volatile uint8 *            pmmr;
    volatile uint8 *            pmmfb;

    uint_ptr                    physfb;
    uint                        fbsize;
    uint_ptr                    physmmr;
    uint                        mmrsize;

    // statistics...
    u32 max_pipes;      /* Device max pipes */
    u32 max_use_endpts;	/* Max endpointes to be used */
    u32 bus_reset;      /* Device is bus reseting */
    u32 resume_state;   /* USB state to resume */
    u32 usb_state;      /* USB current state */
    u32 usb_next_state; /* USB next state */
    u32 ep0_state;      /* Endpoint zero state */
    u32 ep0_dir;        /* Endpoint zero direction: can be  USB_DIR_IN or USB_DIR_OUT */
    u32 usb_sof_count;  /* SOF count */
    u32 errors;         /* USB ERRORs count */
    u8 device_address;  /* Device USB address */

	u32	mCurConfiguration;	/* Current Configuration Number */
	u32	mCurInterface;		/* Current Interface Number */
	u32	mCurAltSetting;		/* Current Alter Setting */
	
    struct tasklet_struct  KdpcBulkOut[1];
    struct tasklet_struct  KdpcBulkIn[1];
    struct completion *done;    /* to make sure release() is done */
};


#define ep_index(EP)                ((EP)->desc->bEndpointAddress&0xF)
#define ep_maxpacket(EP)            ((EP)->ep.maxpacket)
#define ep_is_in(EP)                ((ep_index(EP) == 0) ? (EP->dev->ep0_dir == USB_DIR_IN ):((EP)->desc->bEndpointAddress & USB_DIR_IN)==USB_DIR_IN)

#define get_ep_by_pipe(udc, pipe)   ((pipe == 1)? &dev->ep[0]: &dev->ep[pipe])
#define get_pipe_by_windex(windex)  ((windex & USB_ENDPOINT_NUMBER_MASK) * 2 + ((windex & USB_DIR_IN) ? 1 : 0))
#define get_pipe_by_ep(EP)          (ep_index(EP) * 2 + ep_is_in(EP))

/*-------------------------------------------------------------------------*/

#define xprintk(dev,level,fmt,args...) printk(level "%s %s: " fmt , driver_name , pci_name(dev->pdev) , ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DEBUG(dev,fmt,args...) do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDEBUG DEBUG
#else
#define VDEBUG(dev,fmt,args...) do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev,fmt,args...) xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

