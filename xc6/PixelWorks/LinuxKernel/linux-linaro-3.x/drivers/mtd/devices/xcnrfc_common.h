#ifndef DRIVERS_MTD_DEVICES_XCNRFC_COMMON_H
#define DRIVERS_MTD_DEVICES_XCNRFC_COMMON_H

#define CMD_ADDR(addr)  ((flash->bus_width==16)?((addr)>>1):(addr))

extern int xcnrfc_pollStatus(unsigned int status, int period, int timeout, int clearStatus);
extern int xcnrfc_releaseChip(void);
extern int xcnrfc_setIOTiming(int asyn_read_delay, int page_read_delay);
extern int xcnrfc_resetFlash(FLASHTIMING *flash);
extern int xcnrfc_waitCmdDone(FLASHTIMING *flash, unsigned long flashAddr, unsigned int expectedData, NRFC_CMD cmd);
extern int xcnrfc_generic_read(FLASHTIMING *flash, uint32_t flashAddr, u_char *dstAddr, size_t blkSize);
extern int xcnrfc_intel_readMode(FLASHTIMING *flash);
extern int xcnrfc_intel_write(FLASHTIMING *flash, uint32_t flashAddr, const u_char *srcAddr, size_t blksize);
extern int xcnrfc_intel_erase(FLASHTIMING *flash, uint32_t flashAddr);
extern int xcnrfc_intel_readID(FLASHTIMING *flash);
extern int xcnrfc_intel_unlock(FLASHTIMING *flash, uint32_t flashAddr);

#endif
