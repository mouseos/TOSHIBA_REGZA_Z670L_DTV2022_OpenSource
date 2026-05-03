/*
 * SDUH EHCI port support
 *
 * Copyright(C) 2015 Vixs Systems Limited
 * http://www.Vixs.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SDUH_EHCI_H_
#define _SDUH_EHCI_H_

//********************************************************
//SDUH register definition
//********************************************************
#define SDUH_REG_BASE   (XC_SOC_PROC_MMREG_BASE + 0x2000)
#define SDUH_REG_SIZE   0x40
   
#define SDUH_IAS_REG    0
#define SDUH_EOC_REG    4
#define SDUH_OOC_REG    0xc
#define SDUH_WDATA_REG  0x10
#define SDUH_RDATA_REG      0x14
#define SDUH_OTC_REG    0x18
#define SDUH_STATUS_REG 0x1c
#define SDUH_CONTROL_REG    0x20
#define SDUH_PHY_CONTROL_REG    0x30
#define SDUH_HOST_INT_MASK_REG  0x34
#define SDUH_MIPS_INT_MASK_REG  0x38

typedef enum {
    EHCI_REG = 0,
    OHCI_REG = 1,
} SDUH_HC_reg_type;

extern unsigned int SDUH_writel(unsigned int ehci_reg_addr, unsigned int value, SDUH_HC_reg_type type);
extern unsigned int SDUH_readl(unsigned int ehci_reg_addr, SDUH_HC_reg_type type);
#endif /* _SDUH_EHCI_H_ */
