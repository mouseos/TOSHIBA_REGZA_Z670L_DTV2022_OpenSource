/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#include <linux/mm.h>

#ifdef __KERNEL__

/*
 * This file essentially defines the interface between board
 * specific PCI code and MIPS common PCI code.  Should potentially put
 * into include/asm/pci.h file.
 */

#include <linux/ioport.h>

/*
 * Each pci channel is a top-level PCI bus seem by CPU.  A machine  with
 * multiple PCI channels may have multiple PCI host controllers or a
 * single controller supporting multiple channels.
 */
struct pci_controller {
	struct pci_controller *next;
	struct pci_bus *bus;

	struct pci_ops *pci_ops;
	struct resource *mem_resource;
	unsigned long mem_offset;
	struct resource *io_resource;
	unsigned long io_offset;
	unsigned long io_map_base;

	unsigned int index;
	/* For compatibility with current (as of July 2003) pciutils
	   and XFree86. Eventually will be removed. */
	unsigned int need_domain_info;

	int iommu;

	/* Optional access methods for reading/writing the bus number
	   of the PCI controller */
	int (*get_busno)(void);
	void (*set_busno)(int busno);
};

/*
 * Used by boards to register their PCI busses before the actual scanning.
 */
extern struct pci_controller * alloc_pci_controller(void);
extern void register_pci_controller(struct pci_controller *hose);

/*
 * board supplied pci irq fixup routine
 */
extern int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);


#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern void pcibios_set_master(struct pci_dev *dev);

#define HAVE_PCI_MMAP

extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
	enum pci_mmap_state mmap_state, int write_combine);

/*
 * Dynamic DMA mapping stuff.
 * MIPS has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

struct pci_dev;

extern void pcibios_resource_to_bus(struct pci_dev *dev,
	struct pci_bus_region *region, struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
				    struct pci_bus_region *region);

#define pci_domain_nr(bus) ((struct pci_controller *)(bus)->sysdata)->index
#endif /* __KERNEL__ */

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/* Do platform specific device initialization at pci_enable_device() time */
extern int pcibios_plat_dev_init(struct pci_dev *dev);

#ifdef CONFIG_CPU_CAVIUM_OCTEON
/* MSI arch hook for OCTEON */
#define arch_setup_msi_irqs arch_setup_msi_irqs
#endif

extern int pci_probe_only;

extern char * (*pcibios_plat_setup)(char *str);

#define XCODE_PCIE_PORT 0

// to enable PCIH cmd debug
//#define XCODE_DEBUG_PCIH        

#ifdef XCODE_DEBUG_PCIH
#define DBG(fmt,x...)      printk("[%s:%d]"fmt,__func__,__LINE__,##x)
#else
#define DBG(fmt,x...)
#endif
#define DBG_ERR(fmt,x...)      printk("[%s:%d]"fmt,__func__,__LINE__,##x)

#endif /* _ASM_PCI_H */
