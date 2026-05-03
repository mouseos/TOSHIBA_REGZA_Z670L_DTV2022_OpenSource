#ifndef DRIVERS_MTD_DEVICES_XCODE_NOR_H
#define DRIVERS_MTD_DEVICES_XCODE_NOR_H

#include <asm/io.h>
#include <plat/xcodeRegDef.h>

#undef debug
#ifdef NRFC_DEBUG
#define debug(fmt, arg...) printk(fmt, ##arg)
#else
#define debug(fmt, arg...)
#endif

#define _MMREG(off)((unsigned long) (XC_SOC_PROC_MMREG_BASE + (off)))
#define MMR_READ(reg) readl(_MMREG(reg))
#define MMR_WRITE(data, reg)        writel( (data), (_MMREG(reg)))
#define MMR_WRITEBIT(val, reg, bit) \
    MMR_WRITE((MMR_READ(reg)&~reg##_##bit##_MASK) | (((val)<<reg##_##bit##_SHIFT)&reg##_##bit##_MASK), reg)
#define MMR_READBIT(reg, bit) \
    ((MMR_READ(reg)&reg##_##bit##_MASK)>>reg##_##bit##_SHIFT)

#define MMR_NRFC_FUNC_REG0(func, enableIRQ, multiplier, wfifo, rfifo, cycle) \
    ({ \
     unsigned int temp; \
     temp=((func)<<NRFC_FUNC_REG0_BASE_FUNC_SHIFT)&NRFC_FUNC_REG0_BASE_FUNC_MASK; \
     temp|=(((enableIRQ)<<NRFC_FUNC_REG0_INTEN_SHIFT)&NRFC_FUNC_REG0_INTEN_MASK); \
     temp|=(((multiplier)<<NRFC_FUNC_REG0_MULTIPLIER_SEL_SHIFT)&NRFC_FUNC_REG0_MULTIPLIER_SEL_MASK); \
     temp|=(((wfifo)<<NRFC_FUNC_REG0_WFIFO_SRC_SHIFT)&NRFC_FUNC_REG0_WFIFO_SRC_MASK); \
     temp|=(((rfifo)<<NRFC_FUNC_REG0_RFIFO_DST_SHIFT)&NRFC_FUNC_REG0_RFIFO_DST_MASK); \
     temp|=(((cycle)<<NRFC_FUNC_REG0_NUM_CYCLE_SHIFT)&NRFC_FUNC_REG0_NUM_CYCLE_MASK); \
     MMR_WRITE(temp, NRFC_FUNC_REG0); \
     })

#define WAIT_COND(period, timeout, condition) \
    ({ \
     int t=timeout; \
     \
     while(t>0) \
     { \
     if(condition) \
     break; \
     udelay(period); \
     t-=(period);   \
     } \
     })

#define NRFC_SET_WDATA(data, resetFIFO) \
    ({ \
     unsigned int temp; \
     temp=((data)<<NRFC_WDATA_REG_WDATA_SHIFT)&NRFC_WDATA_REG_WDATA_MASK; \
     temp|=((resetFIFO)<<NRFC_WDATA_REG_WFIFO_REG_RST_SHIFT)&NRFC_WDATA_REG_WFIFO_REG_RST_MASK; \
     MMR_WRITE(temp, NRFC_WDATA_REG); \
     })

#define NRFC_SET_PAGE_READ_SIZE(size) \
    MMR_WRITEBIT((size), NRFC_CTRL_REG, PAGE_SIZE); // Set size = 2^page_size

#define NRFC_IDLE_CYCLE(enableIRQ, cycle) \
    ({ \
     MMR_NRFC_FUNC_REG0(0, enableIRQ, 0, 0, 0, cycle); \
     })

#define NRFC_ADDR_CYCLE(bank, addr, enableIRQ, cycle) \
    ({ \
     MMR_WRITEBIT(addr, NRFC_FLASH_ADDR, FLASH_ADDR); \
     MMR_WRITEBIT(bank, NRFC_FLASH_ADDR, BANK_SEL); \
     MMR_NRFC_FUNC_REG0(1, enableIRQ, 0, 0, 0, cycle); \
     })

#define NRFC_ADDR_CYCLE_SEL(bank, addr, enableIRQ, cycle, multiplier) \
    ({ \
     MMR_WRITEBIT(addr, NRFC_FLASH_ADDR, FLASH_ADDR); \
     MMR_WRITEBIT(bank, NRFC_FLASH_ADDR, BANK_SEL); \
     MMR_NRFC_FUNC_REG0(1, enableIRQ, multiplier, 0, 0, cycle); \
     })

#define NRFC_WRITE_CYCLE(burstSize, enableIRQ, wfifo, cycle) \
    ({ \
     MMR_WRITEBIT(burstSize, NRFC_FUNC_REG1, WBURST_SIZE); \
     MMR_NRFC_FUNC_REG0(3, enableIRQ, 0, wfifo, 0, cycle); \
     })

#define NRFC_READ_CYCLE(burstSize, enableIRQ, rfifo, cycle) \
    ({ \
     MMR_WRITEBIT(burstSize, NRFC_FUNC_REG1, RBURST_SIZE); \
     MMR_NRFC_FUNC_REG0(2, enableIRQ, 0, 0, rfifo, cycle); \
     })

#define NRFC_CMD_CYCLE(bank, addr, burstSize, enableIRQ, wfifo, cycle) \
    ({ \
     MMR_WRITEBIT(addr, NRFC_FLASH_ADDR, FLASH_ADDR); \
     MMR_WRITEBIT(bank, NRFC_FLASH_ADDR, BANK_SEL); \
     MMR_WRITEBIT(burstSize, NRFC_FUNC_REG1, WBURST_SIZE); \
     MMR_NRFC_FUNC_REG0(4, enableIRQ, 0, wfifo, 0, cycle); \
     }) 

#define NUM_BYTES_TO_READ() \
       (MMR_READBIT(NRFC_FIFO_ENTRIES, RFILL_ENTRIES)+ \
        (MMR_READBIT(NRFC_FIFO_ENTRIES, NEW_RDATA)?1:0))

#define MAX_WBURST_SIZE 64
#define MAX_RBURST_SIZE (64*1024)

#define FLASH_BANK(flash)  (1<<(flash->bank))

typedef enum {
    NRFC_PROG=0,
    NRFC_BLKERASE,
    NRFC_CHIPERASE,
} NRFC_CMD;

enum {
    NRFC_8BIT=0,
    NRFC_16BIT, 
};

struct cmdTiming {
    unsigned int cycle;
    unsigned int base;
    unsigned int max_cycle;
    unsigned int max_base;
};

typedef struct _flashtiming {
    char name[64];
    int manufacturer_id;
    int device_id;
    int bank;
    int bus_width;
    int size;
    int erase_size;
    int page_buffer_size_shift;
    int max_write_buffer_size;
    int clock;
    unsigned int interval;
    unsigned int addr_wait;
    unsigned int read_wait;
    unsigned int idle_wait;
    unsigned int write_wait;
    unsigned int asyn_read;
    unsigned int page_read;

    struct cmdTiming cmd[3];
 
    int (*read)(struct _flashtiming *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize);
    int (*write)(struct _flashtiming *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize);
    int (*erase)(struct _flashtiming *flash, uint32_t flashAddr);

    int (*eraseChip)(struct _flashtiming *flash);
    int (*readID)(struct _flashtiming *flash);
    
} FLASHTIMING;


#define NRFC_IDLE_DONE      NRFC_STATUS_IDLE_OPT_DONE_INT_MASK
#define NRFC_ADDR_DONE      NRFC_STATUS_ADDR_OPT_DONE_INT_MASK
#define NRFC_WRITE_DONE     NRFC_STATUS_WRITE_OPT_DONE_INT_MASK
#define NRFC_CMD_DONE       NRFC_STATUS_CMD_OPT_DONE_INT_MASK
#define NRFC_DSTREG_RD_DONE NRFC_STATUS_DSTREG_RD_DONE_INT_MASK
#define NRFC_DSTFB_RD_DONE  NRFC_STATUS_DSTFB_RD_DONE_INT_MASK

#endif
