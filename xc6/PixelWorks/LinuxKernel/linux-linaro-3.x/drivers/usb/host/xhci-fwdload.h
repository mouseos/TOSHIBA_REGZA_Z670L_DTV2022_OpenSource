/*
    File Name : xhci-fwdownload.c

    * Copyright (C) 2011 Renesas Electronics Corporation

*/

///////////////////////////////////////////////////////////////////////////////
//
//      History
//      2011-10-28 rev1.0     base create
//      2011-11-17 rev1.2     Add to download FW return from suspend.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef XHCI_FWDOWNLOAD_72020x
#define XHCI_FWDOWNLOAD_72020x

#include "linux/pci.h"


typedef enum {
    XHCI_FWDOWNLOADER_SUCCESS,
    XHCI_FWDOWNLOADER_ERROR,

} XHCI_FWDOWNLOADER;


XHCI_FWDOWNLOADER XHCI_FWDownLoad (
    struct pci_dev  *pDev,      // Target Device handler
    unsigned char   *pFWImage,  // Pointer to the image to download FW
    unsigned int   nFWSize     // Size of the image to download FW
);

XHCI_FWDOWNLOADER XHCI_FWDownLoadCheck(
    struct usb_hcd *hcd     // Target Device handler
);

#ifdef CONFIG_PM
XHCI_FWDOWNLOADER XHCI_FWReLoad(
    struct usb_hcd *hcd     // Target Device handler
);
#endif

XHCI_FWDOWNLOADER XHCI_FWUnLoad(
    struct usb_hcd *hcd     // Target Device handler
);


#define XHCI_FWDOWNLOAD(xcd) if(XHCI_FWDownLoadCheck(xcd) != XHCI_FWDOWNLOADER_SUCCESS) return -ENODEV
#ifdef CONFIG_PM
#define XHCI_FWRELOAD(xcd)   if(XHCI_FWReLoad(xcd) != XHCI_FWDOWNLOADER_SUCCESS) return -ENODEV
#endif
#define XHCI_FWUNLOAD(xcd)   XHCI_FWUnLoad(xcd)

#endif// XHCI_FWDOWNLOAD_72020x

