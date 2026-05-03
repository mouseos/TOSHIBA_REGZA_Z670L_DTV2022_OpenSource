/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *
 * This file is licenced under the GPL.
 */

#include <linux/irq.h>
#include <linux/slab.h>

static void urb_free_priv (struct ohci_hcd *hc, urb_priv_t *urb_priv)
{
	int		last = urb_priv->length - 1;

	if (last >= 0) {
		int		i;
		struct td	*td;

		for (i = 0; i <= last; i++) {
			td = urb_priv->td [i];
			if (td)
				td_free (hc, td);
		}
	}

	list_del (&urb_priv->pending);
	kfree (urb_priv);
}

/*-------------------------------------------------------------------------*/

/*
 * URB goes back to driver, and isn't reissued.
 * It's completely gone from HC data structures.
 * PRECONDITION:  ohci lock held, irqs blocked.
 */
static void
finish_urb(struct ohci_hcd *ohci, struct urb *urb, int status)
__releases(ohci->lock)
__acquires(ohci->lock)
{
	struct usb_host_endpoint *ep = urb->ep;
	struct urb_priv *urb_priv;

	// ASSERT (urb->hcpriv != 0);

 restart:
	urb_free_priv (ohci, urb->hcpriv);
	urb->hcpriv = NULL;
	if (likely(status == -EINPROGRESS))
		status = 0;

	switch (usb_pipetype (urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs--;
		if (ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs == 0) {
			if (quirk_amdiso(ohci))
				usb_amd_quirk_pll_enable();
			if (quirk_amdprefetch(ohci))
				sb800_prefetch(ohci, 0);
		}
		break;
	case PIPE_INTERRUPT:
		ohci_to_hcd(ohci)->self.bandwidth_int_reqs--;
		break;
	}

#ifdef OHCI_VERBOSE_DEBUG
	urb_print(urb, "RET", usb_pipeout (urb->pipe), status);
#endif

	/* urb->complete() can reenter this HCD */
	usb_hcd_unlink_urb_from_ep(ohci_to_hcd(ohci), urb);
	spin_unlock (&ohci->lock);
	usb_hcd_giveback_urb(ohci_to_hcd(ohci), urb, status);
	spin_lock (&ohci->lock);

	/* stop periodic dma if it's not needed */
	if (ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs == 0
			&& ohci_to_hcd(ohci)->self.bandwidth_int_reqs == 0) {
		ohci->hc_control &= ~(OHCI_CTRL_PLE|OHCI_CTRL_IE);
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	}

	/*
	 * An isochronous URB that is sumitted too late won't have any TDs
	 * (marked by the fact that the td_cnt value is larger than the
	 * actual number of TDs).  If the next URB on this endpoint is like
	 * that, give it back now.
	 */
	if (!list_empty(&ep->urb_list)) {
		urb = list_first_entry(&ep->urb_list, struct urb, urb_list);
		urb_priv = urb->hcpriv;
		if (urb_priv->td_cnt > urb_priv->length) {
			status = 0;
			goto restart;
		}
	}
}


/*-------------------------------------------------------------------------*
 * ED handling functions
 *-------------------------------------------------------------------------*/

/* search for the right schedule branch to use for a periodic ed.
 * does some load balancing; returns the branch, or negative errno.
 */
static int balance (struct ohci_hcd *ohci, int interval, int load)
{
	int	i, branch = -ENOSPC;

	/* iso periods can be huge; iso tds specify frame numbers */
	if (interval > NUM_INTS)
		interval = NUM_INTS;

	/* search for the least loaded schedule branch of that period
	 * that has enough bandwidth left unreserved.
	 */
	for (i = 0; i < interval ; i++) {
		if (branch < 0 || ohci->load [branch] > ohci->load [i]) {
			int	j;

			/* usb 1.1 says 90% of one frame */
			for (j = i; j < NUM_INTS; j += interval) {
				if ((ohci->load [j] + load) > 900)
					break;
			}
			if (j < NUM_INTS)
				continue;
			branch = i;
		}
	}
	return branch;
}

/*-------------------------------------------------------------------------*/

/* both iso and interrupt requests have periods; this routine puts them
 * into the schedule tree in the apppropriate place.  most iso devices use
 * 1msec periods, but that's not required.
 */
static void periodic_link (struct ohci_hcd *ohci, struct ed *ed)
{
	unsigned	i;

	ohci_vdbg (ohci, "link %sed %p branch %d [%dus.], interval %d\n",
		(ed->hwINFO & cpu_to_hc32 (ohci, ED_ISO)) ? "iso " : "",
		ed, ed->branch, ed->load, ed->interval);

	for (i = ed->branch; i < NUM_INTS; i += ed->interval) {
		struct ed	**prev = &ohci->periodic [i];
		__hc32		*prev_p = &ohci->hcca->int_table [i];
		struct ed	*here = *prev;

		/* sorting each branch by period (slow before fast)
		 * lets us share the faster parts of the tree.
		 * (plus maybe: put interrupt eds before iso)
		 */
		while (here && ed != here) {
			if (ed->interval > here->interval)
				break;
			prev = &here->ed_next;
#ifndef CONFIG_XC5_VIRTUAL_USB	
	prev_p = &here->hwNextED;
#else
	if(ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		prev_p = &here->hw_seg->hwNextED;
	}else{
		prev_p = &here->hwNextED;
	}
#endif
		
			here = *prev;
		}
		if (ed != here) {
			ed->ed_next = here;
			if (here)
#ifndef CONFIG_XC5_VIRTUAL_USB	
				ed->hwNextED = *prev_p;
#else
			{if(ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
      ohci_mem_writel(ohci_mem_readl(prev_p), &ed->hw_seg->hwNextED);
	}else{
		ed->hwNextED = *prev_p;
	}
			}
#endif

			wmb ();
			*prev = ed;
#ifndef CONFIG_XC5_VIRTUAL_USB	
	*prev_p = cpu_to_hc32(ohci, ed->dma);
#else
	if(ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		  ohci_mem_writel(cpu_to_hc32(ohci, ed->dma), prev_p);
	}else{
		*prev_p = cpu_to_hc32(ohci, ed->dma);
	}
#endif

			wmb();
		}
		ohci->load [i] += ed->load;
	}
	ohci_to_hcd(ohci)->self.bandwidth_allocated += ed->load / ed->interval;
}

/* link an ed into one of the HC chains */
#ifdef CONFIG_XC5_VIRTUAL_USB
void dump_td(struct ohci_hcd* ohci, struct td *td){
     if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
          printk("td->hw_seg:%p hwINFO:%x hwCBP:%x hwNextTD:%x hwBE:%x hwPSW[]:%x %x\n",td->hw_seg,td->hw_seg->hwINFO, td->hw_seg->hwCBP,td->hw_seg->hwNextTD,td->hw_seg->hwBE, td->hw_seg->hwPSW[0],td->hw_seg->hwPSW[1]);
     }else{
          printk("td:%p hwINFO:%x hwCBP:%x hwNextTD:%x hwBE:%x hwPSW[]:%x %x\n",td,td->hwINFO, td->hwCBP,td->hwNextTD,td->hwBE,td->hwPSW[0],td->hwPSW[1]);
     }
}


void dump_td_seg(struct ohci_hcd* ohci, struct ohci_td_hw_seg *hw_seg){
     printk("td hw_seg:%p\n",hw_seg);	
     printk("hw_seg:%p hwINFO:%x hwCBP:%x hwNextTD:%x hwBE:%x hwPSW[]:%x %x\n",hw_seg,hw_seg->hwINFO, hw_seg->hwCBP,hw_seg->hwNextTD,hw_seg->hwBE, hw_seg->hwPSW[0],hw_seg->hwPSW[1]);
}
void dump_td_seg_list(struct ohci_hcd* ohci, struct ed *ed){
	printk("dump_td_seg_list ed:%p ed->hw_seg:%p\n",ed,ed->hw_seg);
    if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      struct ohci_td_hw_seg *temp=(struct ohci_td_hw_seg *)((unsigned int )ed->hw_seg->hwHeadP + 0xa0e00000);
	      do {
		dump_td_seg(ohci,temp);
	
		if((unsigned int )temp->hwNextTD + 0xa0e00000 == (unsigned int)temp)
			break;
		else 
		   	temp=(struct ohci_td_hw_seg *)((unsigned int )temp->hwNextTD + 0xa0e00000);
	      } while( true);

    }else{

    }
}
void dump_ed(struct ohci_hcd* ohci, struct ed *ed){
	printk("dump_ed ed:%p ed->hw_seg:%p\n",ed,ed->hw_seg);
     if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      struct ed *temp=ed;
         	do {  
          printk("hwINFO:%x hwTailP:%x hwHeadP:%x hwNextED:%x\n",temp->hw_seg->hwINFO, temp->hw_seg->hwTailP,temp->hw_seg->hwHeadP,temp->hw_seg->hwNextED);
 	  dump_td_seg_list(ohci,temp);
		
	      if(temp->hw_seg->hwNextED == 0)
		      break;
	      else
		      temp=(struct ed *)temp->hw_seg->hwNextED + 0xa0e00000;
                
		}while(true);
     }else{
          printk("hwINFO:%x hwTailP:%x hwHeadP:%x hwNextED:%x\n",ed->hwINFO, ed->hwTailP,ed->hwHeadP,ed->hwNextED);
     }

}
#endif
static int ed_schedule (struct ohci_hcd *ohci, struct ed *ed)
{
	int	branch;
	ed->state = ED_OPER;
	ed->ed_prev = NULL;
	ed->ed_next = NULL;
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->hwNextED = 0;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	     ohci_mem_writel(0, &ed->hw_seg->hwNextED);
	  }else{
	  	ed->hwNextED = 0;
	  }
#endif	

	if (quirk_zfmicro(ohci)
			&& (ed->type == PIPE_INTERRUPT)
			&& !(ohci->eds_scheduled++))
		mod_timer(&ohci->unlink_watchdog, round_jiffies(jiffies + HZ));
	wmb ();

	/* we care about rm_list when setting CLE/BLE in case the HC was at
	 * work on some TD when CLE/BLE was turned off, and isn't quiesced
	 * yet.  finish_unlinks() restarts as needed, some upcoming INTR_SF.
	 *
	 * control and bulk EDs are doubly linked (ed_next, ed_prev), but
	 * periodic ones are singly linked (ed_next). that's because the
	 * periodic schedule encodes a tree like figure 3-5 in the ohci
	 * spec:  each qh can have several "previous" nodes, and the tree
	 * doesn't have unused/idle descriptors.
	 */
	switch (ed->type) {
	case PIPE_CONTROL:
		if (ohci->ed_controltail == NULL) {
			WARN_ON (ohci->hc_control & OHCI_CTRL_CLE);
			ohci_writel (ohci, ed->dma,
					&ohci->regs->ed_controlhead);
		} else {
			ohci->ed_controltail->ed_next = ed;
#ifndef CONFIG_XC5_VIRTUAL_USB
	ohci->ed_controltail->hwNextED = cpu_to_hc32 (ohci,
						ed->dma);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      ohci_mem_writel(cpu_to_hc32 (ohci,ed->dma), &ohci->ed_controltail->hw_seg->hwNextED);
	  }else{
		ohci->ed_controltail->hwNextED = cpu_to_hc32 (ohci,
					ed->dma);
	  }
#endif

		}
		ed->ed_prev = ohci->ed_controltail;
		if (!ohci->ed_controltail && !ohci->ed_rm_list) {
			wmb();
			ohci->hc_control |= OHCI_CTRL_CLE;
			ohci_writel (ohci, 0, &ohci->regs->ed_controlcurrent);
			ohci_writel (ohci, ohci->hc_control,
					&ohci->regs->control);
		}
		ohci->ed_controltail = ed;
		break;

	case PIPE_BULK:
		if (ohci->ed_bulktail == NULL) {
			WARN_ON (ohci->hc_control & OHCI_CTRL_BLE);
			ohci_writel (ohci, ed->dma, &ohci->regs->ed_bulkhead);
		} else {
			ohci->ed_bulktail->ed_next = ed;
#ifndef CONFIG_XC5_VIRTUAL_USB
					ohci->ed_bulktail->hwNextED = cpu_to_hc32 (ohci,
								ed->dma);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	 ohci_mem_writel(cpu_to_hc32 (ohci,ed->dma), &ohci->ed_bulktail->hw_seg->hwNextED);
	  }else{
		ohci->ed_bulktail->hwNextED = cpu_to_hc32 (ohci,
							ed->dma);
	  }
#endif

		}
		ed->ed_prev = ohci->ed_bulktail;
		if (!ohci->ed_bulktail && !ohci->ed_rm_list) {
			wmb();
			ohci->hc_control |= OHCI_CTRL_BLE;
			ohci_writel (ohci, 0, &ohci->regs->ed_bulkcurrent);
			ohci_writel (ohci, ohci->hc_control,
					&ohci->regs->control);
		}
		ohci->ed_bulktail = ed;
		break;

	// case PIPE_INTERRUPT:
	// case PIPE_ISOCHRONOUS:
	default:
		branch = balance (ohci, ed->interval, ed->load);
		if (branch < 0) {
			ohci_dbg (ohci,
				"ERR %d, interval %d msecs, load %d\n",
				branch, ed->interval, ed->load);
			// FIXME if there are TDs queued, fail them!
			return branch;
		}
		ed->branch = branch;
		periodic_link (ohci, ed);
	}
	/* the HC may not see the schedule updates yet, but if it does
	 * then they'll be properly ordered.
	 */
	return 0;
}

/*-------------------------------------------------------------------------*/

/* scan the periodic table to find and unlink this ED */
static void periodic_unlink (struct ohci_hcd *ohci, struct ed *ed)
{
	int	i;

	for (i = ed->branch; i < NUM_INTS; i += ed->interval) {
		struct ed	*temp;
		struct ed	**prev = &ohci->periodic [i];
		__hc32		*prev_p = &ohci->hcca->int_table [i];

		while (*prev && (temp = *prev) != ed) {
#ifndef CONFIG_XC5_VIRTUAL_USB
		prev_p = &temp->hwNextED;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		prev_p = &temp->hw_seg->hwNextED;
	  }else{
		prev_p = &temp->hwNextED;
	  }
#endif

			prev = &temp->ed_next;
		}
		if (*prev) {
#ifndef CONFIG_XC5_VIRTUAL_USB
	*prev_p = ed->hwNextED;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwNextED), prev_p);
	  }else{
		*prev_p = ed->hwNextED;
	  }

#endif
			*prev = ed->ed_next;
		}
		ohci->load [i] -= ed->load;
	}
	ohci_to_hcd(ohci)->self.bandwidth_allocated -= ed->load / ed->interval;
#ifndef CONFIG_XC5_VIRTUAL_USB
	ohci_vdbg (ohci, "unlink %sed %p branch %d [%dus.], interval %d\n",
		(ed->hwINFO & cpu_to_hc32 (ohci, ED_ISO)) ? "iso " : "",
		ed, ed->branch, ed->load, ed->interval);
#endif
}

/* unlink an ed from one of the HC chains.
 * just the link to the ed is unlinked.
 * the link from the ed still points to another operational ed or 0
 * so the HC can eventually finish the processing of the unlinked ed
 * (assuming it already started that, which needn't be true).
 *
 * ED_UNLINK is a transient state: the HC may still see this ED, but soon
 * it won't.  ED_SKIP means the HC will finish its current transaction,
 * but won't start anything new.  The TD queue may still grow; device
 * drivers don't know about this HCD-internal state.
 *
 * When the HC can't see the ED, something changes ED_UNLINK to one of:
 *
 *  - ED_OPER: when there's any request queued, the ED gets rescheduled
 *    immediately.  HC should be working on them.
 *
 *  - ED_IDLE:  when there's no TD queue. there's no reason for the HC
 *    to care about this ED; safe to disable the endpoint.
 *
 * When finish_unlinks() runs later, after SOF interrupt, it will often
 * complete one or more URB unlinks before making that state change.
 */
static void ed_deschedule (struct ohci_hcd *ohci, struct ed *ed)
{
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->hwINFO |= cpu_to_hc32 (ohci, ED_SKIP);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	    ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwINFO) |cpu_to_hc32 (ohci, ED_SKIP), &ed->hw_seg->hwINFO);
	  }else{
	ed->hwINFO |= cpu_to_hc32 (ohci, ED_SKIP);
	  }
#endif

	wmb ();
	ed->state = ED_UNLINK;

	/* To deschedule something from the control or bulk list, just
	 * clear CLE/BLE and wait.  There's no safe way to scrub out list
	 * head/current registers until later, and "later" isn't very
	 * tightly specified.  Figure 6-5 and Section 6.4.2.2 show how
	 * the HC is reading the ED queues (while we modify them).
	 *
	 * For now, ed_schedule() is "later".  It might be good paranoia
	 * to scrub those registers in finish_unlinks(), in case of bugs
	 * that make the HC try to use them.
	 */
	switch (ed->type) {
	case PIPE_CONTROL:
		/* remove ED from the HC's list: */
		if (ed->ed_prev == NULL) {
			
#ifndef CONFIG_XC5_VIRTUAL_USB
	if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_CLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs CLE with the HC
			} else
				ohci_writel (ohci,
					hc32_to_cpup (ohci, &ed->hwNextED),
					&ohci->regs->ed_controlhead);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	   if(!ohci_mem_readl(&ed->hw_seg->hwNextED)) {
				ohci->hc_control &= ~OHCI_CTRL_CLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs CLE with the HC
			} else
				ohci_writel (ohci,
					hc32_to_cpup (ohci, &ed->hw_seg->hwNextED),
					&ohci->regs->ed_controlhead);
	  }else{
		if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_CLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs CLE with the HC
			} else
				ohci_writel (ohci,
					hc32_to_cpup (ohci, &ed->hwNextED),
					&ohci->regs->ed_controlhead);
	  }

#endif

		} else {
			ed->ed_prev->ed_next = ed->ed_next;
#ifndef CONFIG_XC5_VIRTUAL_USB
		ed->ed_prev->hwNextED = ed->hwNextED;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		      ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwNextED), &ed->ed_prev->hw_seg->hwNextED);
	  }else{
			ed->ed_prev->hwNextED = ed->hwNextED;
	  }

#endif
		}
		/* remove ED from the HCD's list: */
		if (ohci->ed_controltail == ed) {
			ohci->ed_controltail = ed->ed_prev;
			if (ohci->ed_controltail)
				ohci->ed_controltail->ed_next = NULL;
		} else if (ed->ed_next) {
			ed->ed_next->ed_prev = ed->ed_prev;
		}
		break;

	case PIPE_BULK:
		/* remove ED from the HC's list: */
		if (ed->ed_prev == NULL) {
#ifndef CONFIG_XC5_VIRTUAL_USB
				if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_BLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs BLE with the HC
			} else
				ohci_writel (ohci,
					hc32_to_cpup (ohci, &ed->hwNextED),
					&ohci->regs->ed_bulkhead);

#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		  	if (!ohci_mem_readl(&ed->hw_seg->hwNextED)){
				ohci->hc_control &= ~OHCI_CTRL_BLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs BLE with the HC
			} else
			     ohci_writel(ohci,
                                   le32_to_cpu(ohci_mem_readl(&ed->hw_seg->hwNextED)),
					&ohci->regs->ed_bulkhead);
	  }else{
		  	if (!ed->hwNextED) {
				ohci->hc_control &= ~OHCI_CTRL_BLE;
				ohci_writel (ohci, ohci->hc_control,
						&ohci->regs->control);
				// a ohci_readl() later syncs BLE with the HC
			} else
				ohci_writel (ohci,
					hc32_to_cpup (ohci, &ed->hwNextED),
					&ohci->regs->ed_bulkhead);
	  }

#endif
		
		} else {
			ed->ed_prev->ed_next = ed->ed_next;
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->ed_prev->hwNextED = ed->hwNextED;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		    ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwNextED), &ed->ed_prev->hw_seg->hwNextED);
	  }else{
 			ed->ed_prev->hwNextED = ed->hwNextED;
	  }
#endif

		}
		/* remove ED from the HCD's list: */
		if (ohci->ed_bulktail == ed) {
			ohci->ed_bulktail = ed->ed_prev;
			if (ohci->ed_bulktail)
				ohci->ed_bulktail->ed_next = NULL;
		} else if (ed->ed_next) {
			ed->ed_next->ed_prev = ed->ed_prev;
		}
		break;

	// case PIPE_INTERRUPT:
	// case PIPE_ISOCHRONOUS:
	default:
		periodic_unlink (ohci, ed);
		break;
	}
}


/*-------------------------------------------------------------------------*/

/* get and maybe (re)init an endpoint. init _should_ be done only as part
 * of enumeration, usb_set_configuration() or usb_set_interface().
 */
static struct ed *ed_get (
	struct ohci_hcd		*ohci,
	struct usb_host_endpoint *ep,
	struct usb_device	*udev,
	unsigned int		pipe,
	int			interval
) {
	struct ed		*ed;
	unsigned long		flags;
	spin_lock_irqsave (&ohci->lock, flags);

	if (!(ed = ep->hcpriv)) {
		struct td	*td;
		int		is_out;
		u32		info;

		ed = ed_alloc (ohci, GFP_ATOMIC);
		if (!ed) {
			/* out of memory */
			goto done;
		}

		/* dummy td; end of td list for ed */
		td = td_alloc (ohci, GFP_ATOMIC);
		if (!td) {
			/* out of memory */
			ed_free (ohci, ed);
			ed = NULL;
			goto done;
		}
		ed->dummy = td;
#ifndef CONFIG_XC5_VIRTUAL_USB
		ed->hwTailP = cpu_to_hc32 (ohci, td->td_dma);
		ed->hwHeadP = ed->hwTailP;	/* ED_C, ED_H zeroed */
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
  	      ohci_mem_writel( cpu_to_hc32 (ohci, td->td_dma), &ed->hw_seg->hwTailP);
	      ohci_mem_writel( cpu_to_hc32 (ohci, td->td_dma), &ed->hw_seg->hwHeadP);
	  }else{
		ed->hwTailP = cpu_to_hc32 (ohci, td->td_dma);
		ed->hwHeadP = ed->hwTailP;	/* ED_C, ED_H zeroed */
	  }

#endif
		ed->state = ED_IDLE;
	

		is_out = !(ep->desc.bEndpointAddress & USB_DIR_IN);

		/* FIXME usbcore changes dev->devnum before SET_ADDRESS
		 * succeeds ... otherwise we wouldn't need "pipe".
		 */
		info = usb_pipedevice (pipe);
		ed->type = usb_pipetype(pipe);

		info |= (ep->desc.bEndpointAddress & ~USB_DIR_IN) << 7;
		info |= usb_endpoint_maxp(&ep->desc) << 16;
		if (udev->speed == USB_SPEED_LOW)
			info |= ED_LOWSPEED;
		/* only control transfers store pids in tds */
		if (ed->type != PIPE_CONTROL) {
			info |= is_out ? ED_OUT : ED_IN;
			if (ed->type != PIPE_BULK) {
				/* periodic transfers... */
				if (ed->type == PIPE_ISOCHRONOUS)
					info |= ED_ISO;
				else if (interval > 32)	/* iso can be bigger */
					interval = 32;
				ed->interval = interval;
				ed->load = usb_calc_bus_time (
					udev->speed, !is_out,
					ed->type == PIPE_ISOCHRONOUS,
					usb_endpoint_maxp(&ep->desc))
						/ 1000;
			}
		}
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->hwINFO = cpu_to_hc32(ohci, info);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	         ohci_mem_writel(cpu_to_hc32(ohci, info), &ed->hw_seg->hwINFO);
       DBK("ed:%p hw_seg:%p hw_seg->hwINFO:%x\n",ed,ed->hw_seg,ed->hw_seg->hwINFO);
	  }else{
  		ed->hwINFO = cpu_to_hc32(ohci, info);
       DBK("ed:%p ed->hwINFO:%x %x\n",ed,ed->hwINFO);
	  }

#endif
		ep->hcpriv = ed;
	}

done:
	spin_unlock_irqrestore (&ohci->lock, flags);
	return ed;
}

/*-------------------------------------------------------------------------*/

/* request unlinking of an endpoint from an operational HC.
 * put the ep on the rm_list
 * real work is done at the next start frame (SF) hardware interrupt
 * caller guarantees HCD is running, so hardware access is safe,
 * and that ed->state is ED_OPER
 */
static void start_ed_unlink (struct ohci_hcd *ohci, struct ed *ed)
{
#ifndef CONFIG_XC5_VIRTUAL_USB
  	ed->hwINFO |= cpu_to_hc32 (ohci, ED_DEQUEUE);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	       ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwINFO)|cpu_to_hc32(ohci, ED_DEQUEUE), &ed->hw_seg->hwINFO);
	  }else{
	  	ed->hwINFO |= cpu_to_hc32 (ohci, ED_DEQUEUE);
	  }
#endif

	ed_deschedule (ohci, ed);

	/* rm_list is just singly linked, for simplicity */
	ed->ed_next = ohci->ed_rm_list;
	ed->ed_prev = NULL;
	ohci->ed_rm_list = ed;

	/* enable SOF interrupt */
	ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrstatus);
	ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrenable);
	// flush those writes, and get latest HCCA contents
	(void) ohci_readl (ohci, &ohci->regs->control);

	/* SF interrupt might get delayed; record the frame counter value that
	 * indicates when the HC isn't looking at it, so concurrent unlinks
	 * behave.  frame_no wraps every 2^16 msec, and changes right before
	 * SF is triggered.
	 */
	ed->tick = ohci_frame_no(ohci) + 1;

}
/*-------------------------------------------------------------------------*
 * TD handling functions
 *-------------------------------------------------------------------------*/

/* enqueue next TD for this URB (OHCI spec 5.2.8.2) */

static void
td_fill (struct ohci_hcd *ohci, u32 info,
	dma_addr_t data, int len,
	struct urb *urb, int index)
{
	struct td		*td, *td_pt;
	struct urb_priv		*urb_priv = urb->hcpriv;
	int			is_iso = info & TD_ISO;
	int			hash;
	// ASSERT (index < urb_priv->length);

	/* aim for only one interrupt per urb.  mostly applies to control
	 * and iso; other urbs rarely need more than one TD per urb.
	 * this way, only final tds (or ones with an error) cause IRQs.
	 * at least immediately; use DI=6 in case any control request is
	 * tempted to die part way through.  (and to force the hc to flush
	 * its donelist soonish, even on unlink paths.)
	 *
	 * NOTE: could delay interrupts even for the last TD, and get fewer
	 * interrupts ... increasing per-urb latency by sharing interrupts.
	 * Drivers that queue bulk urbs may request that behavior.
	 */
	if (index != (urb_priv->length - 1)
			|| (urb->transfer_flags & URB_NO_INTERRUPT))
		info |= TD_DI_SET (6);

	/* use this td as the next dummy */
	td_pt = urb_priv->td [index];

	/* fill the old dummy TD */
	td = urb_priv->td [index] = urb_priv->ed->dummy;
	urb_priv->ed->dummy = td_pt;

	td->ed = urb_priv->ed;
	td->next_dl_td = NULL;
	td->index = index;
	td->urb = urb;
	td->data_dma = data;
	if (!len)
		data = 0;
#ifndef CONFIG_XC5_VIRTUAL_USB
	td->hwINFO = cpu_to_hc32 (ohci, info);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	   ohci_mem_writel(cpu_to_hc32(ohci, info), &td->hw_seg->hwINFO);
	  }else{
	  	td->hwINFO = cpu_to_hc32 (ohci, info);
	  }
#endif


#ifndef CONFIG_XC5_VIRTUAL_USB
	  	if (is_iso) {
		td->hwCBP = cpu_to_hc32 (ohci, data & 0xFFFFF000);
		*ohci_hwPSWp(ohci, td, 0) = cpu_to_hc16 (ohci,
						(data & 0x0FFF) | 0xE000);
	} else {
		td->hwCBP = cpu_to_hc32 (ohci, data);
	}
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
  	if (is_iso) {
	   ohci_mem_writel(cpu_to_hc32(ohci, data & 0xFFFFF000), &td->hw_seg->hwCBP);
             ohci_mem_writel(cpu_to_hc16(ohci, (data & 0x0FFF) | 0xE000), ohci_hwPSWp(ohci, td, 0));
		td->ed->last_iso = info & 0xffff;
	} else {
		 ohci_mem_writel(cpu_to_hc32(ohci, data), &td->hw_seg->hwCBP);
	}

	  }else{

		if (is_iso) {
		td->hwCBP = cpu_to_hc32 (ohci, data & 0xFFFFF000);
		*ohci_hwPSWp(ohci, td, 0) = cpu_to_hc16 (ohci,
						(data & 0x0FFF) | 0xE000);
		td->ed->last_iso = info & 0xffff;
	} else {
		td->hwCBP = cpu_to_hc32 (ohci, data);
	}
	  }
#endif


#ifndef CONFIG_XC5_VIRTUAL_USB
	if (data)
		td->hwBE = cpu_to_hc32 (ohci, data + len - 1);
	else
		td->hwBE = 0;
	td->hwNextTD = cpu_to_hc32 (ohci, td_pt->td_dma);

#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	  	if (data){
			ohci_mem_writel(cpu_to_hc32 (ohci, data + len - 1), &td->hw_seg->hwBE);
		}
		else{
			ohci_mem_writel(0, &td->hw_seg->hwBE);
		}
	      	ohci_mem_writel(cpu_to_hc32 (ohci, td_pt->td_dma), &td->hw_seg->hwNextTD);
	  }else{
		  	if (data)
		td->hwBE = cpu_to_hc32 (ohci, data + len - 1);
	else
		td->hwBE = 0;
	td->hwNextTD = cpu_to_hc32 (ohci, td_pt->td_dma);
	  }
#endif	  


	/* append to queue */
	list_add_tail (&td->td_list, &td->ed->td_list);

	/* hash it for later reverse mapping */
	hash = TD_HASH_FUNC (td->td_dma);
	td->td_hash = ohci->td_hash [hash];
	ohci->td_hash [hash] = td;

	/* HC might read the TD (or cachelines) right away ... */
	wmb ();
#ifndef CONFIG_XC5_VIRTUAL_USB
	td->ed->hwTailP = td->hwNextTD;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	     ohci_mem_writel(ohci_mem_readl(&td->hw_seg->hwNextTD), &td->ed->hw_seg->hwTailP);
	  }else{
  	td->ed->hwTailP = td->hwNextTD;
	  }
#endif
	  //dump_td(ohci,td);
}

/*-------------------------------------------------------------------------*/

/* Prepare all TDs of a transfer, and queue them onto the ED.
 * Caller guarantees HC is active.
 * Usually the ED is already on the schedule, so TDs might be
 * processed as soon as they're queued.
 */
static void td_submit_urb (
	struct ohci_hcd	*ohci,
	struct urb	*urb
) {
	struct urb_priv	*urb_priv = urb->hcpriv;
	dma_addr_t	data;
	int		data_len = urb->transfer_buffer_length;
	int		cnt = 0;
	u32		info = 0;
	int		is_out = usb_pipeout (urb->pipe);
	int		periodic = 0;

	/* OHCI handles the bulk/interrupt data toggles itself.  We just
	 * use the device toggle bits for resetting, and rely on the fact
	 * that resetting toggle is meaningless if the endpoint is active.
	 */
	if (!usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe), is_out)) {
		usb_settoggle (urb->dev, usb_pipeendpoint (urb->pipe),
			is_out, 1);
#ifndef CONFIG_XC5_VIRTUAL_USB
	urb_priv->ed->hwHeadP &= ~cpu_to_hc32 (ohci, ED_C);		
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		ohci_mem_writel(ohci_mem_readl(&urb_priv->ed->hw_seg->hwHeadP)&(~cpu_to_hc32 (ohci, ED_C)), &urb_priv->ed->hw_seg->hwHeadP);
	  }else{
		urb_priv->ed->hwHeadP &= ~cpu_to_hc32 (ohci, ED_C);		  
	  }
#endif
	}

	list_add (&urb_priv->pending, &ohci->pending);

	if (data_len)
		data = urb->transfer_dma;
	else
		data = 0;

	/* NOTE:  TD_CC is set so we can tell which TDs the HC processed by
	 * using TD_CC_GET, as well as by seeing them on the done list.
	 * (CC = NotAccessed ... 0x0F, or 0x0E in PSWs for ISO.)
	 */
	switch (urb_priv->ed->type) {

	/* Bulk and interrupt are identical except for where in the schedule
	 * their EDs live.
	 */
	case PIPE_INTERRUPT:
		/* ... and periodic urbs have extra accounting */
		periodic = ohci_to_hcd(ohci)->self.bandwidth_int_reqs++ == 0
			&& ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs == 0;
		/* FALLTHROUGH */
	case PIPE_BULK:
		info = is_out
			? TD_T_TOGGLE | TD_CC | TD_DP_OUT
			: TD_T_TOGGLE | TD_CC | TD_DP_IN;
		/* TDs _could_ transfer up to 8K each */
		while (data_len > 4096) {
			td_fill (ohci, info, data, 4096, urb, cnt);
			data += 4096;
			data_len -= 4096;
			cnt++;
		}
		/* maybe avoid ED halt on final TD short read */
		if (!(urb->transfer_flags & URB_SHORT_NOT_OK))
			info |= TD_R;
		td_fill (ohci, info, data, data_len, urb, cnt);

		cnt++;
		if ((urb->transfer_flags & URB_ZERO_PACKET)
				&& cnt < urb_priv->length) {
			td_fill (ohci, info, 0, 0, urb, cnt);
			cnt++;
		}
		/* maybe kickstart bulk list */
		if (urb_priv->ed->type == PIPE_BULK) {
			wmb ();
			//dump_ed(ohci,urb_priv->ed);
			ohci_writel (ohci, OHCI_BLF, &ohci->regs->cmdstatus);
		}
		break;

	/* control manages DATA0/DATA1 toggle per-request; SETUP resets it,
	 * any DATA phase works normally, and the STATUS ack is special.
	 */
	case PIPE_CONTROL:
		info = TD_CC | TD_DP_SETUP | TD_T_DATA0;
		td_fill (ohci, info, urb->setup_dma, 8, urb, cnt++);
		if (data_len > 0) {
			info = TD_CC | TD_R | TD_T_DATA1;
			info |= is_out ? TD_DP_OUT : TD_DP_IN;
			/* NOTE:  mishandles transfers >8K, some >4K */
			td_fill (ohci, info, data, data_len, urb, cnt++);
		}
		info = (is_out || data_len == 0)
			? TD_CC | TD_DP_IN | TD_T_DATA1
			: TD_CC | TD_DP_OUT | TD_T_DATA1;
		td_fill (ohci, info, data, 0, urb, cnt++);
		/* maybe kickstart control list */
		wmb ();
		ohci_writel (ohci, OHCI_CLF, &ohci->regs->cmdstatus);
		break;

	/* ISO has no retransmit, so no toggle; and it uses special TDs.
	 * Each TD could handle multiple consecutive frames (interval 1);
	 * we could often reduce the number of TDs here.
	 */
	case PIPE_ISOCHRONOUS:
		for (cnt = urb_priv->td_cnt; cnt < urb->number_of_packets;
				cnt++) {
			int	frame = urb->start_frame;

			// FIXME scheduling should handle frame counter
			// roll-around ... exotic case (and OHCI has
			// a 2^16 iso range, vs other HCs max of 2^10)
			frame += cnt * urb->interval;
			frame &= 0xffff;
			td_fill (ohci, TD_CC | TD_ISO | frame,
				data + urb->iso_frame_desc [cnt].offset,
				urb->iso_frame_desc [cnt].length, urb, cnt);
		}
		if (ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs == 0) {
			if (quirk_amdiso(ohci))
				usb_amd_quirk_pll_disable();
			if (quirk_amdprefetch(ohci))
				sb800_prefetch(ohci, 1);
		}
		periodic = ohci_to_hcd(ohci)->self.bandwidth_isoc_reqs++ == 0
			&& ohci_to_hcd(ohci)->self.bandwidth_int_reqs == 0;
		break;
	}

	/* start periodic dma if needed */
	if (periodic) {
		wmb ();
		ohci->hc_control |= OHCI_CTRL_PLE|OHCI_CTRL_IE;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	}

	// ASSERT (urb_priv->length == cnt);
}

/*-------------------------------------------------------------------------*
 * Done List handling functions
 *-------------------------------------------------------------------------*/

/* calculate transfer length/status and update the urb */
static int td_done(struct ohci_hcd *ohci, struct urb *urb, struct td *td)
{
#ifndef CONFIG_XC5_VIRTUAL_USB
	u32	tdINFO = hc32_to_cpup (ohci, &td->hwINFO);
#endif
	int	cc = 0;
	int	status = -EINPROGRESS;

#ifdef CONFIG_XC5_VIRTUAL_USB
	u32	tdINFO;
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	tdINFO = hc32_to_cpu(ohci, ohci_mem_readl(&td->hw_seg->hwINFO));
	  }else{
	 tdINFO = hc32_to_cpup (ohci, &td->hwINFO);
	  }
	  DBK("tdINFO:%x\n",tdINFO);
#endif
	list_del (&td->td_list);

	/* ISO ... drivers see per-TD length/status */
	if (tdINFO & TD_ISO) {
		u16	tdPSW = ohci_hwPSW(ohci, td, 0);
		int	dlen = 0;

		/* NOTE:  assumes FC in tdINFO == 0, and that
		 * only the first of 0..MAXPSW psws is used.
		 */

		cc = (tdPSW >> 12) & 0xF;
		if (tdINFO & TD_CC)	/* hc didn't touch? */{
			return status;
		}

		if (usb_pipeout (urb->pipe))
			dlen = urb->iso_frame_desc [td->index].length;
		else {
			/* short reads are always OK for ISO */
			if (cc == TD_DATAUNDERRUN)
				cc = TD_CC_NOERROR;
			dlen = tdPSW & 0x3ff;
		}
		urb->actual_length += dlen;
		urb->iso_frame_desc [td->index].actual_length = dlen;
		urb->iso_frame_desc [td->index].status = cc_to_error [cc];

		if (cc != TD_CC_NOERROR)
			ohci_vdbg (ohci,
				"urb %p iso td %p (%d) len %d cc %d\n",
				urb, td, 1 + td->index, dlen, cc);

	/* BULK, INT, CONTROL ... drivers see aggregate length/status,
	 * except that "setup" bytes aren't counted and "short" transfers
	 * might not be reported as errors.
	 */
	} else {
		int	type = usb_pipetype (urb->pipe);
		u32	tdBE = hc32_to_cpup (ohci, &td->hwBE);
#ifdef CONFIG_XC5_VIRTUAL_USB
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		   tdBE = hc32_to_cpu(ohci, ohci_mem_readl(&td->hw_seg->hwBE));
	  }
#endif
		cc = TD_CC_GET (tdINFO);

		/* update packet status if needed (short is normally ok) */
		if (cc == TD_DATAUNDERRUN
				&& !(urb->transfer_flags & URB_SHORT_NOT_OK))
			cc = TD_CC_NOERROR;
		if (cc != TD_CC_NOERROR && cc < 0x0E)
			status = cc_to_error[cc];

		/* count all non-empty packets except control SETUP packet */
		if ((type != PIPE_CONTROL || td->index != 0) && tdBE != 0) {
#ifndef CONFIG_XC5_VIRTUAL_USB
						if (td->hwCBP == 0)
				urb->actual_length += tdBE - td->data_dma + 1;
			else
				urb->actual_length +=
					  hc32_to_cpup (ohci, &td->hwCBP)
					- td->data_dma;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
  			if (ohci_mem_readl(&td->hw_seg->hwCBP) == 0)
				urb->actual_length += tdBE - td->data_dma + 1;
			else
				urb->actual_length +=
				 hc32_to_cpu(ohci, ohci_mem_readl(&td->hw_seg->hwCBP))
					- td->data_dma;
	  }else{
		  			if (td->hwCBP == 0)
				urb->actual_length += tdBE - td->data_dma + 1;
			else
				urb->actual_length +=
					  hc32_to_cpup (ohci, &td->hwCBP)
					- td->data_dma;
	  }

#endif			

		}

		if (cc != TD_CC_NOERROR && cc < 0x0E){
			ohci_vdbg (ohci,
				"urb %p td %p (%d) cc %d, len=%d/%d\n",
				urb, td, 1 + td->index, cc,
				urb->actual_length,
				urb->transfer_buffer_length);
#ifdef CONFIG_XC5_VIRTUAL_USB			
			DBK("urb %p td->hw_seg %p (%d) cc %d, len=%d/%d\n",
				urb, td->hw_seg, 1 + td->index, cc,
				urb->actual_length,
				urb->transfer_buffer_length);
#endif
		}
	}
	return status;
}

/*-------------------------------------------------------------------------*/

static void ed_halted(struct ohci_hcd *ohci, struct td *td, int cc)
{
	struct urb		*urb = td->urb;
	urb_priv_t		*urb_priv = urb->hcpriv;
	struct ed		*ed = td->ed;
	struct list_head	*tmp = td->td_list.next;
	__hc32			toggle = ed->hwHeadP & cpu_to_hc32 (ohci, ED_C);

#ifdef CONFIG_XC5_VIRTUAL_USB
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		  toggle = cpu_to_hc32(ohci, ED_C) & ohci_mem_readl(&ed->hw_seg->hwHeadP);
	  }
#endif
	/* clear ed halt; this is the td that caused it, but keep it inactive
	 * until its urb->complete() has a chance to clean up.
	 */
#ifndef CONFIG_XC5_VIRTUAL_USB
  	ed->hwINFO |= cpu_to_hc32 (ohci, ED_SKIP);
	wmb ();
	ed->hwHeadP &= ~cpu_to_hc32 (ohci, ED_H);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwINFO)|cpu_to_hc32(ohci, ED_SKIP), &ed->hw_seg->hwINFO);
		wmb ();
	  ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwHeadP) & (~cpu_to_hc32(ohci, ED_H)), &ed->hw_seg->hwHeadP);
	  }else{
	  	ed->hwINFO |= cpu_to_hc32 (ohci, ED_SKIP);
		wmb ();
		ed->hwHeadP &= ~cpu_to_hc32 (ohci, ED_H);
	  }

#endif	  


	/* Get rid of all later tds from this urb.  We don't have
	 * to be careful: no errors and nothing was transferred.
	 * Also patch the ed so it looks as if those tds completed normally.
	 */
	while (tmp != &ed->td_list) {
		struct td	*next;

		next = list_entry (tmp, struct td, td_list);
		tmp = next->td_list.next;

		if (next->urb != urb)
			break;

		/* NOTE: if multi-td control DATA segments get supported,
		 * this urb had one of them, this td wasn't the last td
		 * in that segment (TD_R clear), this ed halted because
		 * of a short read, _and_ URB_SHORT_NOT_OK is clear ...
		 * then we need to leave the control STATUS packet queued
		 * and clear ED_SKIP.
		 */

		list_del(&next->td_list);
		urb_priv->td_cnt++;
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->hwHeadP = next->hwNextTD | toggle;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	 	ohci_mem_writel(next->hw_seg->hwNextTD | toggle, &ed->hw_seg->hwHeadP);
	  }else{
  		ed->hwHeadP = next->hwNextTD | toggle;
	  }
#endif
	}

	/* help for troubleshooting:  report anything that
	 * looks odd ... that doesn't include protocol stalls
	 * (or maybe some other things)
	 */
	switch (cc) {
	case TD_DATAUNDERRUN:
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) == 0)
			break;
		/* fallthrough */
	case TD_CC_STALL:
		if (usb_pipecontrol (urb->pipe))
			break;
		/* fallthrough */
	default:
		ohci_dbg (ohci,
			"urb %p path %s ep%d%s %08x cc %d --> status %d\n",
			urb, urb->dev->devpath,
			usb_pipeendpoint (urb->pipe),
			usb_pipein (urb->pipe) ? "in" : "out",
			hc32_to_cpu (ohci, td->hwINFO),
			cc, cc_to_error [cc]);
	}
}

/* replies to the request have to be on a FIFO basis so
 * we unreverse the hc-reversed done-list
 */
static struct td *dl_reverse_done_list (struct ohci_hcd *ohci)
{
	u32		td_dma;
	struct td	*td_rev = NULL;
	struct td	*td = NULL;
#ifndef CONFIG_XC5_VIRTUAL_USB
	td_dma = hc32_to_cpup (ohci, &ohci->hcca->done_head);
	ohci->hcca->done_head = 0;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      td_dma = hc32_to_cpu( ohci, ohci_mem_readl(&ohci->hcca->done_head));
	       ohci_mem_writel(0, &ohci->hcca->done_head);
	  }else{
		td_dma = hc32_to_cpup (ohci, &ohci->hcca->done_head);		  
		ohci->hcca->done_head = 0;
	  }
#endif


	wmb();

	/* get TD from hc's singly linked list, and
	 * prepend to ours.  ed->td_list changes later.
	 */
	while (td_dma) {
		int		cc;

		td = dma_to_td (ohci, td_dma);
		if (!td) {
			ohci_err (ohci, "bad entry %8x\n", td_dma);
			break;
		}
#ifndef CONFIG_XC5_VIRTUAL_USB
		td->hwINFO |= cpu_to_hc32 (ohci, TD_DONE);
		cc = TD_CC_GET (hc32_to_cpup (ohci, &td->hwINFO));
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
  	      ohci_mem_writel(ohci_mem_readl(&td->hw_seg->hwINFO)|cpu_to_hc32(ohci, TD_DONE), &td->hw_seg->hwINFO);
              cc = TD_CC_GET (hc32_to_cpu(ohci, ohci_mem_readl(&td->hw_seg->hwINFO)));
	  }else{
  		td->hwINFO |= cpu_to_hc32 (ohci, TD_DONE);
		cc = TD_CC_GET (hc32_to_cpup (ohci, &td->hwINFO));
	  }
#endif

		/* Non-iso endpoints can halt on error; un-halt,
		 * and dequeue any other TDs from this urb.
		 * No other TD could have caused the halt.
		 */
#ifndef CONFIG_XC5_VIRTUAL_USB
	  		if (cc != TD_CC_NOERROR
				&& (td->ed->hwHeadP & cpu_to_hc32 (ohci, ED_H)))
			ed_halted(ohci, td, cc);

#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	  		if (cc != TD_CC_NOERROR
				&& ((ohci_mem_readl(&td->ed->hw_seg->hwHeadP) & cpu_to_hc32 (ohci, ED_H))))
			ed_halted(ohci, td, cc);

	  }else{
		  		if (cc != TD_CC_NOERROR
				&& (td->ed->hwHeadP & cpu_to_hc32 (ohci, ED_H)))
			ed_halted(ohci, td, cc);

	  }

#endif

		td->next_dl_td = td_rev;
		td_rev = td;
#ifndef CONFIG_XC5_VIRTUAL_USB
	td_dma = hc32_to_cpup (ohci, &td->hwNextTD);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	  	  td_dma = hc32_to_cpu(ohci, ohci_mem_readl(&td->hw_seg->hwNextTD));
	  }else{
 		td_dma = hc32_to_cpup (ohci, &td->hwNextTD);
	  }
#endif
	}
	return td_rev;
}

/*-------------------------------------------------------------------------*/

/* there are some urbs/eds to unlink; called in_irq(), with HCD locked */
static void
finish_unlinks (struct ohci_hcd *ohci, u16 tick)
{
	struct ed	*ed, **last;
rescan_all:
	for (last = &ohci->ed_rm_list, ed = *last; ed != NULL; ed = *last) {
		struct list_head	*entry, *tmp;
		int			completed, modified;
		__hc32			*prev;

		/* only take off EDs that the HC isn't using, accounting for
		 * frame counter wraps and EDs with partially retired TDs
		 */
		if (likely(ohci->rh_state == OHCI_RH_RUNNING)) {
			if (tick_before (tick, ed->tick)) {
skip_ed:
				last = &ed->ed_next;
				continue;
			}

			if (!list_empty (&ed->td_list)) {
				struct td	*td;
				u32		head;

				td = list_entry (ed->td_list.next, struct td,
							td_list);
#ifndef CONFIG_XC5_VIRTUAL_USB
		head = hc32_to_cpu (ohci, ed->hwHeadP) & TD_MASK;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		head = hc32_to_cpu (ohci, ohci_mem_readl(&ed->hw_seg->hwHeadP)) & TD_MASK;  
	  }else{
  		head = hc32_to_cpu (ohci, ed->hwHeadP) & TD_MASK;
	  }
#endif				

				/* INTR_WDH may need to clean up first */
				if (td->td_dma != head) {
					if (ed == ohci->ed_to_check)
						ohci->ed_to_check = NULL;
					else
						goto skip_ed;
				}
			}
		}

		/* reentrancy:  if we drop the schedule lock, someone might
		 * have modified this list.  normally it's just prepending
		 * entries (which we'd ignore), but paranoia won't hurt.
		 */
		*last = ed->ed_next;
		ed->ed_next = NULL;
		modified = 0;

		/* unlink urbs as requested, but rescan the list after
		 * we call a completion since it might have unlinked
		 * another (earlier) urb
		 *
		 * When we get here, the HC doesn't see this ed.  But it
		 * must not be rescheduled until all completed URBs have
		 * been given back to the driver.
		 */
rescan_this:
		completed = 0;
#ifndef CONFIG_XC5_VIRTUAL_USB
		prev = &ed->hwHeadP;		
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
               prev = &ed->hw_seg->hwHeadP;
	  }else{
  		prev = &ed->hwHeadP;
	  }
#endif		
		list_for_each_safe (entry, tmp, &ed->td_list) {
			struct td	*td;
			struct urb	*urb;
			urb_priv_t	*urb_priv;
			__hc32		savebits;
			u32		tdINFO;

			td = list_entry (entry, struct td, td_list);
			urb = td->urb;
			urb_priv = td->urb->hcpriv;

			if (!urb->unlinked) {
#ifndef CONFIG_XC5_VIRTUAL_USB
  			        prev = &td->hwNextTD;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		  		prev = &td->hw_seg->hwNextTD;
	  }else{
 				prev = &td->hwNextTD;
	  }
#endif
				continue;
			}

			/* patch pointer hc uses */
#ifndef CONFIG_XC5_VIRTUAL_USB
			savebits = *prev & ~cpu_to_hc32 (ohci, TD_MASK);
			*prev = td->hwNextTD | savebits;

#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		        savebits = ohci_mem_readl(prev) & ~cpu_to_hc32 (ohci, TD_MASK);
                    ohci_mem_writel(ohci_mem_readl(&td->hw_seg->hwNextTD) | savebits, prev);
	  }else{
  			savebits = *prev & ~cpu_to_hc32 (ohci, TD_MASK);
			*prev = td->hwNextTD | savebits;

	  }
#endif

			/* If this was unlinked, the TD may not have been
			 * retired ... so manually save the data toggle.
			 * The controller ignores the value we save for
			 * control and ISO endpoints.
			 */
#ifndef CONFIG_XC5_VIRTUAL_USB
			tdINFO = hc32_to_cpup(ohci, &td->hwINFO);
			if ((tdINFO & TD_T) == TD_T_DATA0)
				ed->hwHeadP &= ~cpu_to_hc32(ohci, ED_C);
			else if ((tdINFO & TD_T) == TD_T_DATA1)
				ed->hwHeadP |= cpu_to_hc32(ohci, ED_C);

#else
 if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		tdINFO =  ohci_mem_readl(&ed->hw_seg->hwINFO);
		if ((tdINFO & TD_T) == TD_T_DATA0){
   		  ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwHeadP)& ~cpu_to_hc32(ohci, ED_C), &ed->hw_seg->hwHeadP);
		}
		else if ((tdINFO & TD_T) == TD_T_DATA1){
   		  ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwHeadP)| cpu_to_hc32(ohci, ED_C), &ed->hw_seg->hwHeadP);				}
 }else{
		tdINFO = hc32_to_cpup(ohci, &td->hwINFO);
		if ((tdINFO & TD_T) == TD_T_DATA0)
			ed->hwHeadP &= ~cpu_to_hc32(ohci, ED_C);
		else if ((tdINFO & TD_T) == TD_T_DATA1)
			ed->hwHeadP |= cpu_to_hc32(ohci, ED_C);

 }
#endif

			/* HC may have partly processed this TD */
			td_done (ohci, urb, td);
			urb_priv->td_cnt++;

			/* if URB is done, clean up */
			if (urb_priv->td_cnt >= urb_priv->length) {
				modified = completed = 1;
				finish_urb(ohci, urb, 0);
			}
		}
		if (completed && !list_empty (&ed->td_list))
			goto rescan_this;

		/* ED's now officially unlinked, hc doesn't see */
		ed->state = ED_IDLE;
		if (quirk_zfmicro(ohci) && ed->type == PIPE_INTERRUPT)
			ohci->eds_scheduled--;

#ifndef CONFIG_XC5_VIRTUAL_USB
		ed->hwHeadP &= ~cpu_to_hc32(ohci, ED_H);
		ed->hwNextED = 0;
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
	      ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwHeadP)&(~cpu_to_hc32(ohci, ED_H)), &ed->hw_seg->hwHeadP);
              ohci_mem_writel(0, &ed->hw_seg->hwNextED);
	  }else{
  		ed->hwHeadP &= ~cpu_to_hc32(ohci, ED_H);
		ed->hwNextED = 0;
	  }
#endif
		wmb ();
#ifndef CONFIG_XC5_VIRTUAL_USB
	ed->hwINFO &= ~cpu_to_hc32 (ohci, ED_SKIP | ED_DEQUEUE);
#else
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
  	ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwINFO)&( ~cpu_to_hc32 (ohci, ED_SKIP | ED_DEQUEUE)), &ed->hw_seg->hwINFO);
	  }else{
  		ed->hwINFO &= ~cpu_to_hc32 (ohci, ED_SKIP | ED_DEQUEUE);
	  }
#endif


		/* but if there's work queued, reschedule */
		if (!list_empty (&ed->td_list)) {
			if (ohci->rh_state == OHCI_RH_RUNNING)
				ed_schedule (ohci, ed);
		}

		if (modified)
			goto rescan_all;
	}

	/* maybe reenable control and bulk lists */
	if (ohci->rh_state == OHCI_RH_RUNNING && !ohci->ed_rm_list) {
		u32	command = 0, control = 0;

		if (ohci->ed_controltail) {
			command |= OHCI_CLF;
			if (quirk_zfmicro(ohci))
				mdelay(1);
			if (!(ohci->hc_control & OHCI_CTRL_CLE)) {
				control |= OHCI_CTRL_CLE;
				ohci_writel (ohci, 0,
					&ohci->regs->ed_controlcurrent);
			}
		}
		if (ohci->ed_bulktail) {
			command |= OHCI_BLF;
			if (quirk_zfmicro(ohci))
				mdelay(1);
			if (!(ohci->hc_control & OHCI_CTRL_BLE)) {
				control |= OHCI_CTRL_BLE;
				ohci_writel (ohci, 0,
					&ohci->regs->ed_bulkcurrent);
			}
		}

		/* CLE/BLE to enable, CLF/BLF to (maybe) kickstart */
		if (control) {
			ohci->hc_control |= control;
			if (quirk_zfmicro(ohci))
				mdelay(1);
			ohci_writel (ohci, ohci->hc_control,
					&ohci->regs->control);
		}
		if (command) {
			if (quirk_zfmicro(ohci))
				mdelay(1);
			ohci_writel (ohci, command, &ohci->regs->cmdstatus);
		}
	}
}



/*-------------------------------------------------------------------------*/

/*
 * Used to take back a TD from the host controller. This would normally be
 * called from within dl_done_list, however it may be called directly if the
 * HC no longer sees the TD and it has not appeared on the donelist (after
 * two frames).  This bug has been observed on ZF Micro systems.
 */
static void takeback_td(struct ohci_hcd *ohci, struct td *td)
{
	struct urb	*urb = td->urb;
	urb_priv_t	*urb_priv = urb->hcpriv;
	struct ed	*ed = td->ed;
	int		status;

	/* update URB's length and status from TD */
	status = td_done(ohci, urb, td);
	urb_priv->td_cnt++;
	/* If all this urb's TDs are done, call complete() */
	if (urb_priv->td_cnt >= urb_priv->length)
		finish_urb(ohci, urb, status);

	/* clean schedule:  unlink EDs that are no longer busy */
	if (list_empty(&ed->td_list)) {
		if (ed->state == ED_OPER)
			start_ed_unlink(ohci, ed);

	/* ... reenabling halted EDs only after fault cleanup */
	}
       
#ifndef CONFIG_XC5_VIRTUAL_USB
		else if ((ed->hwINFO & cpu_to_hc32(ohci, ED_SKIP | ED_DEQUEUE))
			== cpu_to_hc32(ohci, ED_SKIP)) {
		td = list_entry(ed->td_list.next, struct td, td_list);
		if (!(td->hwINFO & cpu_to_hc32(ohci, TD_DONE))) {
			ed->hwINFO &= ~cpu_to_hc32(ohci, ED_SKIP);
			/* ... hc may need waking-up */
			switch (ed->type) {
			case PIPE_CONTROL:
				ohci_writel(ohci, OHCI_CLF,
						&ohci->regs->cmdstatus);
				break;
			case PIPE_BULK:
				ohci_writel(ohci, OHCI_BLF,
						&ohci->regs->cmdstatus);
				break;
			}
		}
	}
#else
       	else{
	  if (ohci_to_hcd(ohci) == g_XC_USBH_info.mps_hcd){
		if ((ohci_mem_readl(&ed->hw_seg->hwINFO) & cpu_to_hc32(ohci, ED_SKIP | ED_DEQUEUE))
			== cpu_to_hc32(ohci, ED_SKIP)) {
		td = list_entry(ed->td_list.next, struct td, td_list);
		if (!(ohci_mem_readl(&td->hw_seg->hwINFO) & cpu_to_hc32(ohci, TD_DONE))) {
		 ohci_mem_writel(ohci_mem_readl(&ed->hw_seg->hwINFO)&(~cpu_to_hc32 (ohci, ED_SKIP)), &ed->hw_seg->hwINFO);
			/* ... hc may need waking-up */
			switch (ed->type) {
			case PIPE_CONTROL:
				ohci_writel(ohci, OHCI_CLF,
						&ohci->regs->cmdstatus);
				break;
			case PIPE_BULK:
				ohci_writel(ohci, OHCI_BLF,
						&ohci->regs->cmdstatus);
				break;
			}
		}
		}
	  }else{
		   if ((ed->hwINFO & cpu_to_hc32(ohci, ED_SKIP | ED_DEQUEUE))
			== cpu_to_hc32(ohci, ED_SKIP)) {
		td = list_entry(ed->td_list.next, struct td, td_list);
		if (!(td->hwINFO & cpu_to_hc32(ohci, TD_DONE))) {
			ed->hwINFO &= ~cpu_to_hc32(ohci, ED_SKIP);
			/* ... hc may need waking-up */
			switch (ed->type) {
			case PIPE_CONTROL:
				ohci_writel(ohci, OHCI_CLF,
						&ohci->regs->cmdstatus);
				break;
			case PIPE_BULK:
				ohci_writel(ohci, OHCI_BLF,
						&ohci->regs->cmdstatus);
				break;
			}
		}
	  }
       }
	  }
#endif
}

/*
 * Process normal completions (error or success) and clean the schedules.
 *
 * This is the main path for handing urbs back to drivers.  The only other
 * normal path is finish_unlinks(), which unlinks URBs using ed_rm_list,
 * instead of scanning the (re-reversed) donelist as this does.  There's
 * an abnormal path too, handling a quirk in some Compaq silicon:  URBs
 * with TDs that appear to be orphaned are directly reclaimed.
 */
static void
dl_done_list (struct ohci_hcd *ohci)
{
	struct td	*td = dl_reverse_done_list (ohci);

	while (td) {
		struct td	*td_next = td->next_dl_td;
		struct ed	*ed = td->ed;

		/*
		 * Some OHCI controllers (NVIDIA for sure, maybe others)
		 * occasionally forget to add TDs to the done queue.  Since
		 * TDs for a given endpoint are always processed in order,
		 * if we find a TD on the donelist then all of its
		 * predecessors must be finished as well.
		 */
		for (;;) {
			struct td	*td2;

			td2 = list_first_entry(&ed->td_list, struct td,
					td_list);
			if (td2 == td)
				break;
			takeback_td(ohci, td2);
		}

		takeback_td(ohci, td);
		td = td_next;
	}
}
