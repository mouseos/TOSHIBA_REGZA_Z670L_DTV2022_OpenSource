#ifndef __ASM_ARCH_XCODE_IRQS_H
#define __ASM_ARCH_XCODE_IRQS_H

#define NR_IRQS			376

#define INTCPS_NR_MIR_REGS	3
#define INTCPS_NR_IRQS		96

#include <mach/hardware.h>

#ifdef CONFIG_FIQ
#define FIQ_START		1024
#endif

#endif
