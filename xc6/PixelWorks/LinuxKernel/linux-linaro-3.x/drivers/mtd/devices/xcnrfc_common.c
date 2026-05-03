#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "xcode_nor.h"
#include "xcnrfc_s29gl01gp.h"
#include "xcnrfc_common.h"

int xcnrfc_pollStatus(unsigned int status, int period, int timeout, int clearStatus)
{
    debug("Poll for status %08x\n", status);

    WAIT_COND(period, timeout, ((MMR_READ(NRFC_STATUS)&status)==status));

    debug("Clearing status\n");
    if(clearStatus)
        MMR_WRITE(status, NRFC_STATUS);

    return 0;
}

int xcnrfc_releaseChip(void)
{
    NRFC_IDLE_CYCLE(1, 0x5);
    xcnrfc_pollStatus(NRFC_IDLE_DONE, 1000, 5000000, 1);

    return 0; 
}

int xcnrfc_setIOTiming(int asyn_read_delay, int page_read_delay)
{
    // Set asyn read & page read timing. Used by Burst read
    MMR_WRITEBIT(asyn_read_delay, NRFC_IO_TIMING, ASY_RD_DELAY);
    MMR_WRITEBIT(page_read_delay, NRFC_IO_TIMING, PAGE_RD_DELAY);

    return 0;
}

int xcnrfc_resetFlash(FLASHTIMING *flash)
{
    // Flash reset release
    MMR_WRITEBIT(1, NRFC_CTRL_REG, FLASH_RSTN);

    //*** Bootstrap override - 8-bit IO mode  ***
    MMR_WRITEBIT(flash->bus_width==8?NRFC_8BIT:NRFC_16BIT, NRFC_BOOTSTRAP_OVRRIDE, BYTE_WORDB_OVERIDE);
    MMR_WRITEBIT(1, NRFC_BOOTSTRAP_OVRRIDE, PAGE_READ_OVERRIDE);
    MMR_WRITEBIT(1, NRFC_BOOTSTRAP_OVRRIDE, NRFC_OVRERIDE_EN);

    // Set interrupt mask
    MMR_WRITE(0xff, NRFC_INT_MASK);

    NRFC_SET_PAGE_READ_SIZE(flash->page_buffer_size_shift-((flash->bus_width==8)?0:1));

    xcnrfc_setIOTiming(flash->asyn_read, flash->page_read); 

    MMR_WRITE(flash->interval, NRFC_MULTIPLIER0);

    NRFC_IDLE_CYCLE(0, 0xff);

    return 0;
}

int xcnrfc_waitCmdDone(FLASHTIMING *flash, unsigned long flashAddr, unsigned int expectedData, NRFC_CMD cmd)
{
    int multiplier, ret=0;
    int wait, match, retry=0;
    int poll, done=0;
    unsigned int data;

    if(flash->bus_width==8)
        expectedData=expectedData&0xff; 

    multiplier=flash->cmd[cmd].base; 
    wait=flash->cmd[cmd].cycle;
    if(cmd==NRFC_CHIPERASE)
        poll=500000000;
    else
        poll=5000000;

    MMR_WRITE(multiplier, NRFC_MULTIPLIER1);
    while(done==0)
    {
        NRFC_ADDR_CYCLE_SEL(FLASH_BANK(flash), flashAddr, 0, wait, 1);
        NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
        xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1000, poll, 1); 

        data=MMR_READBIT(NRFC_RDATA_REG, RDATA);
        debug("Data read %x, data expected %x multiplier %d wait %d poll %d\n", data, expectedData, multiplier, wait, poll);
        if(data==expectedData)
        {
            match=1;
        }
        else
            match=0;

        if(match)
            done=1;
        else
        {
            retry++;
            multiplier=flash->cmd[cmd].max_base;
            wait=flash->cmd[cmd].max_cycle;
            MMR_WRITE(multiplier, NRFC_MULTIPLIER1);
        }    

        if(((retry>=2) && (cmd==NRFC_BLKERASE)) || 
                ((retry>=6) && (cmd==NRFC_CHIPERASE)) || 
                ((retry>=10) && (cmd==NRFC_PROG)))
        {
            ret=1;
            printk("Cmd %d Timeout!\n", cmd);
            break;   
        }
    }   
    return ret;
}

int xcnrfc_generic_read(FLASHTIMING *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize)
{
    if(ALIGN((unsigned long)dstAddr, 32)!=(unsigned long)dstAddr)
    {
        printk("XCNRFC: Address not aligned with 32 bytes\n");
        return -EINVAL;
    }

//NEED?    inv_dcache_range((unsigned long)dstAddr, (unsigned long)dstAddr+blkSize);
    while(blkSize>0)
    {
        int burstSize=min(blkSize, (size_t)MAX_RBURST_SIZE);

        MMR_WRITE(virt_to_bus((void *)dstAddr), NRFC_DST_ADDR);

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
        NRFC_READ_CYCLE(burstSize, 1, 1, flash->read_wait);
        xcnrfc_pollStatus(NRFC_DSTFB_RD_DONE, 100, 5000000, 1);

        debug(".");

        blkSize-=burstSize;
        dstAddr+=burstSize;
        flashAddr+=burstSize;
    }

    return 0;
}

int xcnrfc_intel_readMode(FLASHTIMING *flash)
{
    NRFC_SET_WDATA(0, 1);
    NRFC_SET_WDATA(0x00ff, 0);
    
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), 0, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 1, 0, flash->write_wait);

    xcnrfc_pollStatus(NRFC_WRITE_DONE, 1, 5000, 1);

    return 0;
}

int xcnrfc_intel_write(FLASHTIMING *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize)
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
            NRFC_SET_WDATA(0x00e8 | ((burstSize-1)<<8), 0);
            NRFC_SET_WDATA(0x00d0, 0);  
        }
        else
        {
            NRFC_SET_WDATA(0xe8, 0);
            NRFC_SET_WDATA(burstSize/2-1, 0);
            NRFC_SET_WDATA(0xd0, 0);  
        }
    
        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); 
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait); 
        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
        NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    
        xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1000, 50000, 1);

        //Ready for WSM for buffered programming?
        if(MMR_READBIT(NRFC_RDATA_REG, RDATA)!=0x80)
        {
            printk("Flash chip not accept bufferred programming?!\n");
            return -EIO;
        }

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); 
        NRFC_WRITE_CYCLE(burstSize, 0, 1, flash->write_wait);

        NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait); 
        NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

        ret=xcnrfc_waitCmdDone(flash, flashAddr, 0x80, NRFC_PROG);
        if(ret)
            return -EIO;

        debug(".");

        remain-=burstSize;
        srcAddr+=burstSize;
        flashAddr+=burstSize;
    }

    xcnrfc_intel_readMode(flash);

    return ret;
}

int xcnrfc_intel_erase(FLASHTIMING *flash, uint32_t flashAddr)
{
    //Block erase

    NRFC_SET_WDATA(0, 1);
    if(flash->bus_width==8)
        NRFC_SET_WDATA(0xd020, 0);
    else
    {
        NRFC_SET_WDATA(0x20, 0);
        NRFC_SET_WDATA(0xd0, 0);
    }

    //Command cycle
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

    debug("Wait for erase cmd done\n");
    xcnrfc_waitCmdDone(flash, flashAddr, 0x80, NRFC_BLKERASE);

    xcnrfc_intel_readMode(flash);

    return 0;
}

int xcnrfc_intel_readID(FLASHTIMING *flash)
{
    int bytes, i, tmp;

    debug("AutoSelect test\n");

    NRFC_SET_WDATA(0, 1);
    NRFC_SET_WDATA(0x90, 0);

    //Autoselect command - manufacturer ID
    NRFC_IDLE_CYCLE(0, 0xff);
    NRFC_CMD_CYCLE(FLASH_BANK(flash), 0, 1, 0, 0, flash->addr_wait);
    NRFC_IDLE_CYCLE(0, 0x1f);

    //Read manufacture ID
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), 0, 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    tmp=MMR_READBIT(NRFC_RDATA_REG, RDATA);
    if(tmp!=flash->manufacturer_id)
        panic("Manufacture ID not correct(should be %08x, now %08x)\n", flash->manufacturer_id, tmp);
    printk("Manufacturer ID = 0x%08x\n", tmp);

    //Autoselect command - Device ID
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), 0x0002, 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);   
    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);

    tmp=MMR_READBIT(NRFC_RDATA_REG, RDATA);
    if(tmp!=flash->device_id)
        panic("Device ID not correct(should be %08x, now %08x)\n", flash->device_id, tmp);
    printk("Device ID = 0x%08x\n", tmp);

    xcnrfc_intel_readMode(flash);

    xcnrfc_releaseChip();

    return 0;
}

int xcnrfc_intel_unlock(FLASHTIMING *flash, uint32_t flashAddr)
{
    NRFC_SET_WDATA(0, 1);
    if(flash->bus_width==8)
    {
        NRFC_SET_WDATA(0xd060, 0);
        NRFC_SET_WDATA(0x0090, 0);
    }
    else
    {  
        NRFC_SET_WDATA(0x60, 0);
        NRFC_SET_WDATA(0xd0, 0);
        NRFC_SET_WDATA(0x90, 0);
    }
 
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);

    xcnrfc_waitCmdDone(flash, flashAddr, 0x80, NRFC_BLKERASE);

    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr, 0, flash->addr_wait);
    NRFC_WRITE_CYCLE(1, 0, 0, flash->write_wait);
    NRFC_ADDR_CYCLE(FLASH_BANK(flash), flashAddr+4, 0, flash->addr_wait);
    NRFC_READ_CYCLE(1, 1, 0, flash->read_wait);

    xcnrfc_pollStatus(NRFC_DSTREG_RD_DONE, 1, 5000, 1);
    
    if(MMR_READ(NRFC_RDATA_REG)&1)
    {
        printk("Unlock flash block failed\n");
        return -EIO;
    }

    return 0;
}
