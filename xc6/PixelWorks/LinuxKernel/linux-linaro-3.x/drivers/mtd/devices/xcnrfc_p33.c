#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "xcode_nor.h"
#include "xcnrfc_p33.h"
#include "xcnrfc_common.h"

FLASHTIMING IntelP33_30MHz={
    "Numonyx P33",
    MANUFACTURER_ID_INTEL,
    DEVICE_ID_INTEL_P33,
    1,
    16,
    32*1024*1024,
    128*1024,
    3,
    64,
    30000000,
    0,          //interval
    0,          //addr_wait
    3,          //read_wait
    1,          //idle_wait
    2,          //write_wait
    3,          //asyn_read
    1,          //page_read
    {
        { 28, 481, 56, 481},
        { 42, 615016, 196, 615016},
        { -1, -1, -1, -1}
    },
};

