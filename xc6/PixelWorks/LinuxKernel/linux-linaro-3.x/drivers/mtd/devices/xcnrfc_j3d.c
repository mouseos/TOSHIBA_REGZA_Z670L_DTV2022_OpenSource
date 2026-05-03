#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "xcode_nor.h"
#include "xcnrfc_j3d.h"
#include "xcnrfc_common.h"

static int IntelJ3d_read(FLASHTIMING *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize)
{
    return xcnrfc_generic_read(flash, flashAddr, dstAddr, blkSize);
}
 
static int IntelJ3d_write(FLASHTIMING *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize)
{
    return xcnrfc_intel_write(flash, flashAddr, srcAddr, blksize);
}

static int IntelJ3d_erase(FLASHTIMING *flash, uint32_t flashAddr)
{
    return xcnrfc_intel_erase(flash, flashAddr);
}

static int IntelJ3d_readID(FLASHTIMING *flash)
{
    return xcnrfc_intel_readID(flash);
}


FLASHTIMING IntelJ3d_30MHz={
    "Intel J3vD",
    MANUFACTURER_ID_INTEL,
    DEVICE_ID_INTEL_J3D,
    2,
    8,
    32*1024*1024,
    128*1024,
    3,
    32,
    30000000,
    0,          //interval
    0,          //addr_wait
    3,          //read_wait
    1,          //idle_wait
    2,          //write_wait
    3,          //asyn_read
    1,          //page_read
    {
        { 8, 481, 128, 481},
        { 48, 615016, 200, 615016},
        { -1, -1, -1, -1}
    },

    IntelJ3d_read,
    IntelJ3d_write,
    IntelJ3d_erase,
    NULL,
    IntelJ3d_readID,
};

