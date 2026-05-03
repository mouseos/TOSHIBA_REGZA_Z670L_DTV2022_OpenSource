/*
 *  XCODE SoC pci ops.
 *
 *  Copyright 2009 ViXS Systems Inc
 *    Philip Yang <pyang@vixs.com>
 
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
 */
 
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include <mach/pci.h>
#include <plat/xcodeRegDef.h> 

#define PCIE_CMD_CONFIG_READ_TYPE0    0x0000F040 // type 1: 0x0000F050 // type 0: 0x0000F040     
#define PCIE_CMD_CONFIG_READ_TYPE1    0x0000F050 // type 1: 0x0000F050 // type 0: 0x0000F040     
#define PCIE_CMD_CONFIG_WRITE_TYPE0   0x0000F042 // type 1: 0x0000F052 // type 0: 0x0000F042     
#define PCIE_CMD_CONFIG_WRITE_TYPE1   0x0000F052 // type 1: 0x0000F052 // type 0: 0x0000F042     

#define CHECKTIMEOUT_VALUE  (1000*1000*1024)        // about 2ms, as ARM is at 1000MHz
static int pcie_read_config_dword(unsigned int busno, unsigned int devfn, int where, u32 *value)
{
	unsigned long flags;
    unsigned long reg_data;
    unsigned long cmd, status;
    unsigned long checktimeout;

    int port=busno;
   // return -1;
    DBG("busno: 0x%08x  devfn:%x where:%x addr 0x%08x, data: 0x%08x\n", busno,devfn,where, ((devfn&0xff)<<16) + (where & ~3), value);
    local_irq_save(flags);

    // do config read
    reg_data = (busno<<24) + ((devfn&0xff)<<16) + (where & ~3);  // one device has 256 bytes configuration space, offset to dword alignment
        cmd = PCIE_CMD_CONFIG_READ_TYPE0;

    mmr_write(reg_data, PCIE_GENIO_ADDR+port*0xF800);
    mmr_write(cmd, PCIE_GENIO_CMD+port*0xF800);

    // wait until cmd finish or timeout
    checktimeout = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + PROC5_WATCHDOG_CNT));
    while ((mmr_read(PCIE_GENIO_STATUS+port*0xF800) & PCIE_GENIO_STATUS_PCIE_GENIO_BUSY_MASK) == PCIE_GENIO_STATUS_PCIE_GENIO_BUSY_MASK)
    {
        if (readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + PROC5_WATCHDOG_CNT)) - checktimeout > CHECKTIMEOUT_VALUE){
		DBG_ERR("%s polling IO timed out , PCIE_GENIO_RDAT:%x\n", __func__, mmr_read(PCIE_GENIO_RDAT+port*0xF800));
		break;
		}
    }

    *value = mmr_read(PCIE_GENIO_RDAT+port*0xF800);

	local_irq_restore(flags);

    // check read completion status
    status = mmr_read(PCIE_GENIO_STATUS+port*0xF800);
    status = (status & PCIE_GENIO_STATUS_PCIE_GENIO_CPL_STATUS_MASK) >> PCIE_GENIO_STATUS_PCIE_GENIO_CPL_STATUS_SHIFT; 
    if (status == 0x001 || status == 0x100) // unsupported request or competer abort
    {   
        DBG_ERR("unsupported request or competer abort\n");
        *value = 0;
    }
    if (status == 0x010) // retry status
    {   
        DBG("retry status\n");
        *value = 0;
    }

    // clear status
    mmr_write(mmr_read(PCIE_HOST_STATUS+port*0xF800)| PCIE_HOST_STATUS_PCIE_HOST_POISONED_MASK, PCIE_HOST_STATUS+port*0xF800);
        
    DBG("busno: 0x%x addr: 0x%08x, value:0x%08x\n", busno, ((devfn&0xff)<<16) + (where & ~3), *value);

	return 0;
}

static int pcie_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	unsigned int v;

    if (bus->number == 0 && PCI_SLOT(devfn) >= 1)   // only scan first  2nd device 
    {
	DBG("bus->number: 0x%x  PCI_SLOT(devfn)%08x\n", bus->number,  PCI_SLOT(devfn));
        return 0;
    }
    if (bus->number == 1 && PCI_SLOT(devfn) >= 1)   // only scan first device 
    {
	DBG("bus->number: 0x%x  PCI_SLOT(devfn)%08x\n", bus->number,  PCI_SLOT(devfn));
        return 0;
    }
    if (bus->number >= 2 && PCI_SLOT(devfn) >= 1)   // only scan first device 
    {
	DBG("bus->number: 0x%x  PCI_SLOT(devfn)%08x\n", bus->number,  PCI_SLOT(devfn));
        return 0;
    }
    if (bus->number >= 5){
	DBG("bus->number: 0x%x  PCI_SLOT(devfn)%08x\n", bus->number,  PCI_SLOT(devfn));
        return 0;
    }
        
	switch (size) {
	case 1:
            pcie_read_config_dword(bus->number, devfn, where&~3, &v);
            *val = 0xff & (v >> (8*(where & 3)));
            return 0;
	case 2:
            if (where&1) return -EINVAL;
            pcie_read_config_dword(bus->number, devfn, where&~3, &v);
            *val = 0xffff & (v >> (8*(where & 3)));
            return 0;
	case 4:
            if (where&3) return -EINVAL;
            pcie_read_config_dword(bus->number, devfn, where&~3, val);
            return 0;
	}
	return -EINVAL;
}

static int pcie_write_config_dword(unsigned int busno, unsigned int devfn, int where, u32 value)
{
	unsigned long flags;
    unsigned long reg_data;
    unsigned long cmd;
    unsigned long checktimeout;
   int port=busno;
//return -1;
    DBG("busno: 0x%08x  devfn:%x where:%x addr 0x%08x, data: 0x%08x\n", busno,devfn,where, ((devfn&0xff)<<16) + (where & ~3), value);
 	local_irq_save(flags);

    // do config write
    reg_data = (busno<<24) + ((devfn&0xff)<<16) + (where & ~3);  // one device has 256 bytes configuration space, offset to dword alignment
        cmd = PCIE_CMD_CONFIG_WRITE_TYPE0;
	mmr_write(reg_data, PCIE_GENIO_ADDR+port*0xF800);
    mmr_write(value, PCIE_GENIO_WDAT+port*0xF800);
    mmr_write(cmd, PCIE_GENIO_CMD+port*0xF800);

    // wait until cmd finish or timeout
    checktimeout = readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + PROC5_WATCHDOG_CNT));
    while ((mmr_read(PCIE_GENIO_STATUS+port*0xF800) & PCIE_GENIO_STATUS_PCIE_GENIO_BUSY_MASK) == PCIE_GENIO_STATUS_PCIE_GENIO_BUSY_MASK) // IVAN IVAN CHECK THIS
    {
        if (readl((unsigned long *)(XC_SOC_PROC_MMREG_BASE + PROC5_WATCHDOG_CNT)) - checktimeout > CHECKTIMEOUT_VALUE){
			DBG_ERR("[%s:%d] writing IO timed out\n", __func__,__LINE__);
            break;
		}		
    }
    
	local_irq_restore(flags);

	return 0;
}

static int pcie_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	unsigned int v;

    if (PCI_SLOT(devfn) >= 21) {
   	DBG("bus->number: 0x%x  PCI_SLOT(devfn)%08x\n", bus->number,  PCI_SLOT(devfn));
        return 0;
    }

	switch (size) {
	case 1:
		pcie_read_config_dword(bus->number, devfn, where&~3, &v);
		v = (v & ~(0xff << (8*(where&3)))) |
		    ((0xff&val) << (8*(where&3)));
		return pcie_write_config_dword(bus->number, devfn, where&~3, v);
	case 2:
		if (where&1) 
		    return -EINVAL;
		pcie_read_config_dword(bus->number, devfn, where&~3, &v);
		v = (v & ~(0xffff << (8*(where&3)))) |
		    ((0xffff&val) << (8*(where&3)));
		return pcie_write_config_dword(bus->number, devfn, where&~3, v);
	case 4:
		if (where&3) 
		    return -EINVAL;
		return pcie_write_config_dword(bus->number, devfn, where, val);
	}
	return -EINVAL;
}


struct pci_ops xcode_pci_ops = {
	pcie_read_config,
	pcie_write_config
};

