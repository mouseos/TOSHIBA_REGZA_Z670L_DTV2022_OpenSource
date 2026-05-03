#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "xcode_nor.h"
#include "xcnrfc_p30.h"
#include "xcnrfc_common.h"

static int IntelP30_read(FLASHTIMING *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize)
{
    return xcnrfc_generic_read(flash, flashAddr, dstAddr, blkSize);
}
 
static int IntelP30_write(FLASHTIMING *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize)
{
    //Unlock block
    if(xcnrfc_intel_unlock(flash, flashAddr))
        return -EIO;

    return xcnrfc_intel_write(flash, flashAddr, srcAddr, blksize);
}

static int IntelP30_erase(FLASHTIMING *flash, uint32_t flashAddr)
{
    //Unlock block
    if(xcnrfc_intel_unlock(flash, flashAddr))
        return -EIO;
 
    return xcnrfc_intel_erase(flash, flashAddr);
}

static int IntelP30_readID(FLASHTIMING *flash)
{
    return xcnrfc_intel_readID(flash);
}

FLASHTIMING IntelP30_30MHz={
    "Numonyx P30",
    MANUFACTURER_ID_INTEL,
    DEVICE_ID_INTEL_P30,
    0,
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
        { 28, 481, 42, 481},
        { 42, 615016, 196, 615016},
        { -1, -1, -1, -1}
    },

    IntelP30_read,
    IntelP30_write,
    IntelP30_erase,
    NULL,
    IntelP30_readID,
};
