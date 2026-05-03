#ifndef __ASM_ARCH_XCODE_HARDWARE_H
#define __ASM_ARCH_XCODE_HARDWARE_H

#include <asm/sizes.h>
#ifndef __ASSEMBLER__
#include <asm/types.h>
#endif

/*
 * ---------------------------------------------------------------------------
 * Processor specific defines
 * ---------------------------------------------------------------------------
 */

#include <plat/xcode6.h>

//#define pcibios_assign_all_busses() 1
//Now defined in pci init as variables
//#define PCIBIOS_MIN_IO		0x80000000
//#define PCIBIOS_MIN_MEM		0x00000000

#endif	/* __ASM_ARCH_XCODE_HARDWARE_H */
