/*
 * Copyright (c) 2001 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

/*-------------------------------------------------------------------------*/

/*
 * There's basically three types of memory:
 *	- data used only by the HCD ... kmalloc is fine
 *	- async and periodic schedules, shared by HC and HCD ... these
 *	  need to use dma_pool or dma_alloc_coherent
 *	- driver buffers, read/written by HC ... single shot DMA mapped
 *
 * There's also "register" data (e.g. PCI or SOC), which is memory mapped.
 * No memory seen by this driver is pageable.
 */

/*-------------------------------------------------------------------------*/

/* Allocate the key transfer structures from the previously allocated pool */

static inline void ehci_qtd_init(struct ehci_hcd *ehci, struct ehci_qtd *qtd,
				  dma_addr_t dma)
{
	memset (qtd, 0, sizeof *qtd);
	qtd->qtd_dma = dma;
	qtd->hw_token = cpu_to_hc32(ehci, QTD_STS_HALT);
	qtd->hw_next = EHCI_LIST_END(ehci);
	qtd->hw_alt_next = EHCI_LIST_END(ehci);
	INIT_LIST_HEAD (&qtd->qtd_list);
}

static struct ehci_qtd *ehci_qtd_alloc (struct ehci_hcd *ehci, gfp_t flags)
{
	struct ehci_qtd		*qtd;
	dma_addr_t		dma;
#ifdef CONFIG_XC5_VIRTUAL_USB 
	 struct usb_hcd *hcd = ehci_to_hcd(ehci);
#endif


#ifndef CONFIG_XC5_VIRTUAL_USB 
	qtd = dma_pool_alloc (ehci->qtd_pool, flags, &dma);
#else
          if (hcd == g_XC_USBH_info.mps_ehci_hcd){
		qtd = kmalloc(sizeof(struct ehci_qtd),GFP_ATOMIC);
		//qtd = dma_pool_alloc (ehci->qtd_pool, flags, &dma);
	  }else{
		qtd = dma_pool_alloc (ehci->qtd_pool, flags, &dma);
	  }
#endif
	if (qtd != NULL) {
#ifndef CONFIG_XC5_VIRTUAL_USB 
		ehci_qtd_init(ehci, qtd, dma);
#else
          if (hcd == g_XC_USBH_info.mps_ehci_hcd){
		  int i;

	memset (qtd, 0, sizeof *qtd);
        qtd->hw = (struct ehci_qtd_hw*)vusb_qtd_hw_seg_allocate(&qtd->qtd_dma);
        if(qtd->hw == NULL){
          kfree(qtd);
          return NULL;
         }

         for(i  = 0; i < sizeof(struct ehci_qtd_hw)/4; i++)
         {
           ehci_mem_writel(0, ( __hc32 __iomem *)(((u32)qtd->hw)+i*4));        
        }

       ehci_mem_writel( cpu_to_hc32(ehci, QTD_STS_HALT), &qtd->hw->hw_token);
       ehci_mem_writel(EHCI_LIST_END(ehci), &qtd->hw->hw_next);
       ehci_mem_writel(EHCI_LIST_END(ehci), &qtd->hw->hw_alt_next);
	qtd->qtd_dma_hack =(dma_addr_t) NULL;
	INIT_LIST_HEAD (&qtd->qtd_list);
	  }else{
         	ehci_qtd_init(ehci, qtd, dma);
	  }
	DBK("ehci:%p qtd:%p dma:%p\n",ehci,qtd,(void*)qtd->qtd_dma);
#endif
	}else{
	BUG();
	}
	return qtd;
}

static inline void ehci_qtd_free (struct ehci_hcd *ehci, struct ehci_qtd *qtd)
{
#ifdef CONFIG_XC5_VIRTUAL_USB 
 struct usb_hcd *hcd = ehci_to_hcd(ehci);
  if (hcd == g_XC_USBH_info.mps_ehci_hcd){
       vusb_qtd_hw_seg_free( (u32)qtd->hw);        
         kfree(qtd);
    }else{
        dma_pool_free (ehci->qtd_pool, qtd, qtd->qtd_dma);
    }
#else
    dma_pool_free (ehci->qtd_pool, qtd, qtd->qtd_dma);
#endif
	

}


static void qh_destroy(struct ehci_hcd *ehci, struct ehci_qh *qh)
{
#ifdef CONFIG_XC5_VIRTUAL_USB 
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
#endif
	/* clean qtds first, and know this is not linked */
	if (!list_empty (&qh->qtd_list) || qh->qh_next.ptr) {
		ehci_dbg (ehci, "unused qh not empty!\n");
		BUG ();
	}
	if (qh->dummy)
		ehci_qtd_free (ehci, qh->dummy);
#ifdef CONFIG_XC5_VIRTUAL_USB 
      if (hcd == g_XC_USBH_info.mps_ehci_hcd){
        vusb_qh_hw_seg_free( (u32)qh->hw);         
	dma_pool_free (ehci->qh_pool, qh->hw_hack, qh->qh_dma_hack);
      }else{
	dma_pool_free(ehci->qh_pool, qh->hw, qh->qh_dma);
      }
#else
	dma_pool_free(ehci->qh_pool, qh->hw, qh->qh_dma);
#endif
	kfree(qh);
}

static struct ehci_qh *ehci_qh_alloc (struct ehci_hcd *ehci, gfp_t flags)
{
	struct ehci_qh		*qh;
	dma_addr_t		dma;
	#ifdef CONFIG_XC5_VIRTUAL_USB 
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
#endif
	qh = kzalloc(sizeof *qh, GFP_ATOMIC);
	if (!qh)
		goto done;
	qh->hw = (struct ehci_qh_hw *)
		dma_pool_alloc(ehci->qh_pool, flags, &dma);
	if (!qh->hw)
		goto fail;
	memset(qh->hw, 0, sizeof *qh->hw);
#ifdef CONFIG_XC5_VIRTUAL_USB
       //allocate qh_hw_seg here
        if (hcd == g_XC_USBH_info.mps_ehci_hcd){
 	int i ;
	qh->hw_hack = qh->hw;
        qh->hw = (struct ehci_qh_hw*)vusb_qh_hw_seg_allocate( &qh->qh_dma);
        for(i  = 0; i < sizeof(struct ehci_qh_hw)/4; i++)
        {
           ehci_mem_writel(0, ( __hc32 __iomem *)(((u32)qh->hw)+i*4));        
        }
	qh->qh_dma_hack = dma;
	}else{
	qh->qh_dma = dma;
	}
#else
	qh->qh_dma = dma;
#endif
	// INIT_LIST_HEAD (&qh->qh_list);
	INIT_LIST_HEAD (&qh->qtd_list);
	/* dummy td enables safe urb queuing */
	qh->dummy = ehci_qtd_alloc (ehci, flags);
	if (qh->dummy == NULL) {
		ehci_dbg (ehci, "no dummy td\n");
		goto fail1;
	}
done:
	return qh;
fail1:
	dma_pool_free(ehci->qh_pool, qh->hw, qh->qh_dma);
fail:
	kfree(qh);
	return NULL;
}

/*-------------------------------------------------------------------------*/

/* The queue heads and transfer descriptors are managed from pools tied
 * to each of the "per device" structures.
 * This is the initialisation and cleanup code.
 */

static void ehci_mem_cleanup (struct ehci_hcd *ehci)
{
#ifdef CONFIG_XC5_VIRTUAL_USB 
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
#endif
	if (ehci->async)
		qh_destroy(ehci, ehci->async);
	ehci->async = NULL;

	if (ehci->dummy)
		qh_destroy(ehci, ehci->dummy);
	ehci->dummy = NULL;

	/* DMA consistent memory and pools */
	if (ehci->qtd_pool)
		dma_pool_destroy (ehci->qtd_pool);
	ehci->qtd_pool = NULL;

	if (ehci->qh_pool) {
		dma_pool_destroy (ehci->qh_pool);
		ehci->qh_pool = NULL;
	}

	if (ehci->itd_pool)
		dma_pool_destroy (ehci->itd_pool);
	ehci->itd_pool = NULL;

	if (ehci->sitd_pool)
		dma_pool_destroy (ehci->sitd_pool);
	ehci->sitd_pool = NULL;

#ifndef CONFIG_XC5_VIRTUAL_USB 
	if (ehci->periodic)
		dma_free_coherent (ehci_to_hcd(ehci)->self.controller,
			ehci->periodic_size * sizeof (u32),
			ehci->periodic, ehci->periodic_dma);
#else
	 if (hcd != g_XC_USBH_info.mps_ehci_hcd){
                if (ehci->periodic)
		dma_free_coherent (ehci_to_hcd(ehci)->self.controller,
			ehci->periodic_size * sizeof (u32),
			ehci->periodic, ehci->periodic_dma);
	 }
#endif
	ehci->periodic = NULL;

	/* shadow periodic table */
	kfree(ehci->pshadow);
	ehci->pshadow = NULL;
}

/* remember to add cleanup code (above) if you add anything here */
static int ehci_mem_init (struct ehci_hcd *ehci, gfp_t flags)
{
#ifdef CONFIG_XC5_VIRTUAL_USB 
	struct usb_hcd *hcd = ehci_to_hcd(ehci);
#endif
	int i;

	/* QTDs for control/bulk/intr transfers */
	ehci->qtd_pool = dma_pool_create ("ehci_qtd",
			ehci_to_hcd(ehci)->self.controller,
			sizeof (struct ehci_qtd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->qtd_pool) {
		goto fail;
	}

	/* QHs for control/bulk/intr transfers */
	ehci->qh_pool = dma_pool_create ("ehci_qh",
			ehci_to_hcd(ehci)->self.controller,
			sizeof(struct ehci_qh_hw),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->qh_pool) {
		goto fail;
	}
	ehci->async = ehci_qh_alloc (ehci, flags);
	if (!ehci->async) {
		goto fail;
	}

	/* ITD for high speed ISO transfers */
	ehci->itd_pool = dma_pool_create ("ehci_itd",
			ehci_to_hcd(ehci)->self.controller,
			sizeof (struct ehci_itd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->itd_pool) {
		goto fail;
	}

	/* SITD for full/low speed split ISO transfers */
	ehci->sitd_pool = dma_pool_create ("ehci_sitd",
			ehci_to_hcd(ehci)->self.controller,
			sizeof (struct ehci_sitd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->sitd_pool) {
		goto fail;
	}
#ifndef CONFIG_XC5_VIRTUAL_USB
	/* Hardware periodic table */
	ehci->periodic = (__le32 *)
		dma_alloc_coherent (ehci_to_hcd(ehci)->self.controller,
			ehci->periodic_size * sizeof(__le32),
			&ehci->periodic_dma, 0);
	if (ehci->periodic == NULL) {
		goto fail;
	}

	for (i = 0; i < ehci->periodic_size; i++)
			ehci->periodic[i] = EHCI_LIST_END(ehci);
#else
        if (hcd == g_XC_USBH_info.mps_ehci_hcd){
            ehci->periodic = (__le32 *)(g_XC_USBH_info.m_ehci_periodic_mem_vaddr);
            ehci->periodic_dma = g_XC_USBH_info.m_ehci_periodic_mem_paddr;
	}else {
        	ehci->periodic = (__le32 *)
		dma_alloc_coherent (ehci_to_hcd(ehci)->self.controller,
			ehci->periodic_size * sizeof(__le32),
			&ehci->periodic_dma, 0);
	}

	if (ehci->periodic == NULL) {
		goto fail;
	}
        if (hcd == g_XC_USBH_info.mps_ehci_hcd){
	for (i = 0; i < ehci->periodic_size; i++)
       {   
              ehci_mem_writel(EHCI_LIST_END(ehci), &ehci->periodic [i]);
       }}else{
	for (i = 0; i < ehci->periodic_size; i++)
			ehci->periodic[i] = EHCI_LIST_END(ehci);
       }

#endif
		if (ehci->use_dummy_qh) {
		struct ehci_qh_hw	*hw;
		ehci->dummy = ehci_qh_alloc(ehci, flags);
		if (!ehci->dummy)
			goto fail;
		
		hw = ehci->dummy->hw;
#ifndef CONFIG_XC5_VIRTUAL_USB
		hw->hw_next = EHCI_LIST_END(ehci);
		hw->hw_qtd_next = EHCI_LIST_END(ehci);
		hw->hw_alt_next = EHCI_LIST_END(ehci);
		hw->hw_token &= ~QTD_STS_ACTIVE;
		
		ehci->dummy->hw = hw;

		for (i = 0; i < ehci->periodic_size; i++)
			ehci->periodic[i] = ehci->dummy->qh_dma;
#else
		 if (hcd == g_XC_USBH_info.mps_ehci_hcd){
                     ehci_mem_writel(EHCI_LIST_END(ehci), &hw->hw_next);
                     ehci_mem_writel(EHCI_LIST_END(ehci),  &hw->hw_qtd_next);
                     ehci_mem_writel(EHCI_LIST_END(ehci), &hw->hw_alt_next);
                     ehci_mem_writel(ehci_mem_readl(&hw->hw_token)& (~QTD_STS_ACTIVE), &hw->hw_token);

		 }else{
		hw->hw_next = EHCI_LIST_END(ehci);
		hw->hw_qtd_next = EHCI_LIST_END(ehci);
		hw->hw_alt_next = EHCI_LIST_END(ehci);
		hw->hw_token &= ~QTD_STS_ACTIVE;
		 }
		ehci->dummy->hw = hw;

		 if (hcd == g_XC_USBH_info.mps_ehci_hcd){
          		for (i = 0; i < ehci->periodic_size; i++)
			  ehci_mem_writel(ehci->dummy->qh_dma, &ehci->periodic [i]);
		 }else {
		for (i = 0; i < ehci->periodic_size; i++)
				ehci->periodic[i] = ehci->dummy->qh_dma;
		 }
#endif


	} else {
#ifndef CONFIG_XC5_VIRTUAL_USB
		for (i = 0; i < ehci->periodic_size; i++)
			ehci->periodic[i] = EHCI_LIST_END(ehci);
#else
		 if (hcd == g_XC_USBH_info.mps_ehci_hcd){
		for (i = 0; i < ehci->periodic_size; i++)
		   ehci_mem_writel(EHCI_LIST_END(ehci), &ehci->periodic [i]);
		 }else{
		for (i = 0; i < ehci->periodic_size; i++)
			ehci->periodic[i] = EHCI_LIST_END(ehci);
		 }
#endif
	}
	/* software shadow of hardware table */
	ehci->pshadow = kcalloc(ehci->periodic_size, sizeof(void *), flags);
	if (ehci->pshadow != NULL)
		return 0;

fail:
	ehci_dbg (ehci, "couldn't init memory\n");
	ehci_mem_cleanup (ehci);
	return -ENOMEM;
}
