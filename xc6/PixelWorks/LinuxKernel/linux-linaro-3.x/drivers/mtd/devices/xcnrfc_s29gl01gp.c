#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "xcode_nor.h"
#include "xcnrfc_s29gl01gp.h"
#include "xcnrfc_common.h"

#define EMPTY 0xffff

static int Span29GL_read(FLASHTIMING *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize)
{
    return xcnrfc_generic_read(flash, flashAddr, dstAddr, blkSize);
}
 
static int Span29GL_write(FLASHTIMING *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize)
{
    int ret=0;
    int remain=blksize;

    if(ALIGN((unsigned long)srcAddr, 32)!=(unsigned long)srcAddr)
    {
        printk("XCNRFC: Address not aligned with 32 bytes\n");
        return -EINVAL;
    }

//NEED?    flush_dcache_range((unsigned long)srcAddr, (unsigned long)srcAddr+blksize);
    while(remain>0)
    {
        int burstSize=min(remain, min(MAX_WBURST_SIZE, flash->max_write_buffer_size));
    
        MMR_WRITE(virt_to_bus(srcAddr), NRFC_SRC_ADDR);

        NRFC_SET_WDATA(0, 1);
  
        if(flash->bus_width==8)
        {
            NRFC_SET_WDATA(0x55aa, 0);
            NRFC_SET_WDATA(0x0025 | ((burstSize-1)<<8), 0);
            NRFC_SET_WDATA(0x0029, 0);  
        }
        else
        {
            NRFC_SET_WDATA(0xaa, 0);
            NRFC_SET_WDATA(0x55, 0);
            NRFC_SET_WDATA(0x25, 0);
            NRFC_SET_WDATA((burstSize/2)-1, 0);
            NRFC_SET_WDATA(0x29, 0);  
        }

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait); //aa -> aaa
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait); 
        NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait); //55 -> 555
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); //25 -> flashAddr
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); //wc -> flashAddr
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); //memory -> pbl
        NRFC_WRITE_CYCLE(burstSize, 0, 1, flash->write_wait);

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); //29 -> flashAddr
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

        ret=xcnrfc_waitCmdDone(flash, flashAddr, (unsigned int)(*(unsigned short *)srcAddr), NRFC_PROG);
        if(ret)
            return -EIO;

        debug(".");

        remain-=burstSize;
        srcAddr+=burstSize;
        flashAddr+=burstSize;
    }

    return ret;
}

static int Span29GL_erase(FLASHTIMING *flash, uint32_t flashAddr)
{
    //Block erase

    NRFC_SET_WDATA(0, 1);
    if(flash->bus_width==8)
    {
        NRFC_SET_WDATA(0x55aa, 0);
        NRFC_SET_WDATA(0xaa80, 0);
        NRFC_SET_WDATA(0x3055, 0);
    }
    else
    {  
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x80, 0);
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x30, 0);
    }

    //Command cycle
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);

    debug("Wait for erase cmd done\n");
    xcnrfc_waitCmdDone(flash, flashAddr, EMPTY, NRFC_BLKERASE);

    //Block erase command done
    NRFC_IDLE_CYCLE(1, flash->idle_wait);

    debug("Poll status\n");
    xcnrfc_pollStatus(NRFC_IDLE_DONE, 1, 5000, 1);

    return 0;
}

static int Span29GL_eraseChip(FLASHTIMING *flash)
{
    debug("Erase Chip\n");

    NRFC_SET_WDATA(0, 1);
    if(flash->bus_width==8)
    {
        NRFC_SET_WDATA(0x55aa, 0);
        NRFC_SET_WDATA(0xaa80, 0);
        NRFC_SET_WDATA(0x1055, 0);
    }
    else
    {
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x80, 0);
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x10, 0);
    }

    debug("Write erase cmd\n");
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(0, 0, 0, flash->write_wait);

    debug("Wait for cmd complete\n");
    xcnrfc_waitCmdDone(flash, 0, EMPTY, NRFC_CHIPERASE);

    NRFC_IDLE_CYCLE(1, flash->idle_wait);
    xcnrfc_pollStatus(NRFC_IDLE_DONE, 1, 5000, 1);

#if 0    
    printk("Read back 1st block\n");
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_ADDR_CYCLE(bank, 0, 0, flash->addr_wait);
    NRFC_READ_CYCLE(0x10000, 1, 1, flash->read_wait);
    NRFC_IDLE_CYCLE(0, flash->idle_wait);   
    pollStatus(NRFC_DSTFB_RD_DONE, 1, 5000, 1);
#endif
    xcnrfc_releaseChip();


    return 0;
}

static int Span29GL_readID(FLASHTIMING *flash)
{
    int bytes, i, tmp;

    debug("AutoSelect test\n");

    NRFC_SET_WDATA(0, 1);
    if(flash->bus_width==8)
    {
        NRFC_SET_WDATA(0x55aa, 0);
        NRFC_SET_WDATA(0xaa90, 0);
        NRFC_SET_WDATA(0x9055, 0);
        NRFC_SET_WDATA(0x55aa, 0);
        NRFC_SET_WDATA(0xaa90, 0);
        NRFC_SET_WDATA(0x9055, 0);
        NRFC_SET_WDATA(0x00f0, 0);
    }
    else
    {
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x90, 0);
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x90, 0);
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x90, 0);
        NRFC_SET_WDATA(0xaa, 0);
        NRFC_SET_WDATA(0x55, 0);
        NRFC_SET_WDATA(0x90, 0);
        NRFC_SET_WDATA(0xf0, 0);
    }

    debug("%x\n", MMR_READBIT(NRFC_FIFO_ENTRIES, RFILL_ENTRIES));
    //Autoselect command - manufacturer ID
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

    //Read manufacture ID
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    tmp=MMR_READBIT(NRFC_RDATA_REG, RDATA);
    if(tmp!=flash->manufacturer_id)
        panic("Manufacture ID not correct(should be %08x, now %08x)\n", flash->manufacturer_id, tmp);
    printk("Manufacturer ID = 0x%08x\n", tmp);

    //Autoselect command - Device ID
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0002), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);   
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x001c), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    //Read device ID - 3rd
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x001e), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    bytes=NUM_BYTES_TO_READ();
    debug("%d bytes for Device ID\n", bytes);
    if(bytes!=3)
        printk("No valid device ID can be read\n");
    else
    {
        int shift=0;

        tmp=0;
        for(i=0;i<bytes;i++)
        {
            tmp|=((MMR_READBIT(NRFC_RDATA_REG, RDATA)&0xff)<<shift);
            shift+=8;
        }
        if(tmp!=flash->device_id)
            panic("Device ID not correct %x\n", tmp);
    }
    printk("Device ID = 0x%08x\n", tmp);

    //Autoselect command - Sector Protect verify
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);


    //Read Sector protect verify
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0004), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    //RFIFO level = " NRFC_FIFO_ENTRIES\RFILL_ENTRIES
    bytes=NUM_BYTES_TO_READ();
    printk("Sector protect verify: ");
    for(i=0;i<bytes;i++)
            printk("%08x ", MMR_READBIT(NRFC_RDATA_REG, RDATA));
    printk("\n");

    //Autoselect command - Sector Protect verify
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0555), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0aaa), 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

    //Read secure device verify
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), CMD_ADDR(0x0006), 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    bytes=NUM_BYTES_TO_READ();
    printk("Secure Device verify info: ");
    for(i=0;i<bytes;i++)
        printk("%08x ", MMR_READBIT(NRFC_RDATA_REG, RDATA));
    printk("\n");

    //Reset command to return to read mode"
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_CMD_CYCLE(FLASH_BANK(flash), 0x0000, 1, 0, 0, flash->write_wait);
    NRFC_IDLE_CYCLE(0, 0x1f);

    xcnrfc_releaseChip();

    return 0;
}

FLASHTIMING Span29GL_20MHz={
    "Spansion S29GL01GP",
    MANUFACTURER_ID_SPANSION,
    DEVICE_ID_SPANSION_S29GL01GP,
    3,
    8,
    128*1024*1024,
    128*1024,
    4, 
    64,
    20000000,
    0,          //interval
    0,          //addr_wait
    2,          //read_wait
    0,          //idle_wait
    0,          //write_wait
    3,          //asyn_read
    1,          //page_read
    {   { 4, 320, 128, 320},
        { 25, 409600, 200, 409600},
        { 200, 52428800, 200, 209715200}
    },

    Span29GL_read,
    Span29GL_write,
    Span29GL_erase,
    Span29GL_eraseChip,
    Span29GL_readID,
};

FLASHTIMING Span29GL_30MHz={
    "Spansion S29GL01GP",
    MANUFACTURER_ID_SPANSION,
    DEVICE_ID_SPANSION_S29GL01GP,
    3,
    8,
    128*1024*1024,
    128*1024,
    4,
    64,
    30000000,
    1,          //interval
    1,          //addr_wait
    3,          //read_wait
    0,          //idle_wait
    2,          //write_wait
    3,          //asyn_read
    1,          //page_read
    {
        { 4, 480, 128, 480},
        { 25, 615015, 200, 615015},
        { 200, 78721921, 200, 314887687}
    },

    Span29GL_read,
    Span29GL_write,
    Span29GL_erase,
    Span29GL_eraseChip,
    Span29GL_readID,
};

