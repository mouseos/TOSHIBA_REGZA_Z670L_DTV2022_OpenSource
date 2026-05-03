#ifndef XCODEREGDEF_H
#define XCODEREGDEF_H

#ifdef CONFIG_PLAT_XCODE64xx
#include <mach/tesla.h>
#else
#ifdef CONFIG_PLAT_XCODE68xx
#include <mach/capri.h>
#else
#error "xcode type not defined"
#endif
#endif

#endif
