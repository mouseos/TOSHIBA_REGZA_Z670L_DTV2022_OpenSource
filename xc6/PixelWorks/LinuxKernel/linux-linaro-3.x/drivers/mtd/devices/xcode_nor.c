#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <plat/xcodeRegDef.h>
#include "../mtdcore.h"
#include "xcode_nor.h"
#include "xcnrfc_s29gl01gp.h"
#include "xcnrfc_j3d.h"
#include "xcnrfc_p30.h"
#include "xcnrfc_p33.h"
#include "xcnrfc_common.h"

FLASHTIMING *flash_array[]={
    &IntelP30_30MHz,
    &IntelP33_30MHz,
    &IntelJ3d_30MHz,
    &Span29GL_30MHz,
};

#define NUM_BANK        ARRAY_SIZE(flash_array)

static int readCFI(void)
{
#if 0
    //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    //%  Spansion CFI Query                                          %
    //%  -- tested rburst size ignored when dst is register          %
    //%  -- address not sequentials (i.e. 0x10 -> 0x12 -> 0x11 ...   %
    //%     Read all CFI address                                     %
    //%  -- tested command cycle                                     %
    //%  -- Tested "reset command" to return read memory mode        %
    //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    WORK_DIR C:\VDS_Tests\Stingray\NRFC
        declare {
            nftest_interval
                nftest_addr_wait
                nftest_read_wait
                nftest_idle_wait
                nftest_write_wait
                nftest_asyn_read
                nftest_page_read
                nftest_rburst_size= 0x11
                nftest_bank_sel   = 0x8 // one-hot
                nftest_golden_data
                nftest_reset_chkaddr = 0x8004
                func_nrfc_param0
                func_nrfc_param1
                func_nrfc_param2
                func_nrfc_param3
                func_nrfc_param4
        }
    nftest_interval   = glb_nrfc_interval
        nftest_addr_wait  = glb_nrfc_addr_wait
        nftest_read_wait  = glb_nrfc_read_wait
        nftest_idle_wait  = glb_nrfc_idle_wait
        nftest_write_wait = glb_nrfc_write_wait
        nftest_asyn_read  = glb_nrfc_asyn_read
        nftest_page_read  = glb_nrfc_page_read

        MMR_WRITEBIT(1, NRFC_CTRL_REG, FLASH_RSTN);

    // Set interrupt mask
    MMR_WRITE(0xff, NRFC_INT_MASK);

    // Set size = 2^4
    NRFC_SET_PAGE_READ_SIZE(4);

    // Set asyn read & page read timing. Used by Burst read
    setIOTiming(flash->asyn_read, flash->page_read); 

    MMR_WRITE(0x02000000, NRFC_DST_ADDR);

    MMR_WRITE(flash->interval, NRFC_MULTIPLIER0);

    //Single data programming from REG. Used to test reset command after CFIread
    MEM[0x0F000000, 0x0F00000F] = RANDOM
        nftest_golden_data = MEM[0x0F000000] & 0x00FF
        LOG "INFO: golden rand data is " nftest_golden_data

        func_nrfc_param0 = nftest_golden_data 
        func_nrfc_param1 = nftest_reset_chkaddr
        func_nrfc_param2 = nftest_bank_sel
        func_nrfc_param3 = nftest_addr_wait
        func_nrfc_param4 = nftest_write_wait
        nrfc_func_Span_RegPg.Run()

        ////////////////////////////////////////
        LOG "INFO: Prepare wdata for CFI Query command"
        glb_nrfc_param0 = 0x0000
        glb_nrfc_param1 = 1
        nrfc_set_wdata_reg.Run()

        glb_nrfc_param0 = 0xF098
        glb_nrfc_param1 = 0
        nrfc_set_wdata_reg.Run()

        LOG "INFO: CFI Query command"
        ////////////////////////////////////////
        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0xff
        nrfc_idle_cycle.Run()

        glb_nrfc_param0 = 0x00AA
        glb_nrfc_param1 = nftest_bank_sel
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 1
        glb_nrfc_param4 = 0
        glb_nrfc_param5 = 0
        glb_nrfc_param6 = nftest_write_wait
        nrfc_command_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0x1f
        nrfc_idle_cycle.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x10 "
        glb_nrfc_param0 = 0x0010 << 1
        glb_nrfc_param1 = nftest_bank_sel
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 0
        glb_nrfc_param4 = nftest_addr_wait
        nrfc_address_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = nftest_rburst_size
        glb_nrfc_param2 = 1
        glb_nrfc_param3 = 1
        glb_nrfc_param4 = nftest_read_wait
        nrfc_read_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 0
        glb_nrfc_param4 = 1
        glb_nrfc_param5 = 0
        glb_nrfc_param6 = 1
        glb_nrfc_param7 = 5000
        glb_nrfc_param8 = 1
        nrfc_poll_opt_status.Run()

        LOG "INFO: Check DST REG Data. CFI read. Check rburst size ignored when dst is register"
        NRFC_RDATA_REG\RDATA == 0x0051

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x12 "
        glb_nrfc_param0 = 0x0012 << 1
        glb_nrfc_param1 = nftest_bank_sel
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 0
        glb_nrfc_param4 = nftest_addr_wait
        nrfc_address_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = nftest_rburst_size
        glb_nrfc_param2 = 1
        glb_nrfc_param3 = 1
        glb_nrfc_param4 = nftest_read_wait
        nrfc_read_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 0
        glb_nrfc_param4 = 1
        glb_nrfc_param5 = 0
        glb_nrfc_param6 = 1
        glb_nrfc_param7 = 5000
        glb_nrfc_param8 = 1
        nrfc_poll_opt_status.Run()

        LOG "INFO: Check DST REG Data. CFI read. Check rburst size ignored when dst is register"
        NRFC_RDATA_REG\RDATA == 0x0059

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x11"
        func_nrfc_param0 = 0x11 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0052
        nrfc_func_flashRd_dstReg.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x14"
        func_nrfc_param0 = 0x14 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: CFI addr 0x13"
        func_nrfc_param0 = 0x13 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        nrfc_func_flashRd_dstReg.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x15"
        func_nrfc_param0 = 0x15 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0040
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: CFI addr 0x16"
        func_nrfc_param0 = 0x16 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x17"
        func_nrfc_param0 = 0x17 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: CFI addr 0x18"
        func_nrfc_param0 = 0x18 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x1A"
        func_nrfc_param0 = 0x1A << 1 
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: CFI addr 0x19"
        func_nrfc_param0 = 0x19 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        nrfc_func_flashRd_dstReg.Run()

        ////////////////////////////////////////
        LOG "INFO: CFI addr 0x1B"
        func_nrfc_param0 = 0x1B << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0027
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: CFI addr 0x1C"
        func_nrfc_param0 = 0x1C << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0036
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x1D << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x1E << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x1F << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0006
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x20 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0006
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x21 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0009
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x22 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0013
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x23 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0003
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x24 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0005
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x25 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0003
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x26 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x27 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x001B
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x28 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x29 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2A << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0006
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2B << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2C << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0001
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2D << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x00FF
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2E << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0003
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x2F << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x30 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x31 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x32 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x33 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x34 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x35 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x36 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x37 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x38 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x39 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x3A << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x3B << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x3C << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x40 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0050
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x41 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0052
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x42 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0049
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x43 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0031
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x44 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0033
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x45 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0014
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x46 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x47 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0001
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x48 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x49 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0008
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4A << 1 
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4B << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0000
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4C << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0002
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4D << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x00B5
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4E << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x00C5
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x4F << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0005
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        func_nrfc_param0 = 0x50 << 1
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = 0x0001
        LOG "INFO: CFI addr " func_nrfc_param0
        nrfc_func_flashRd_dstReg.Run()

        LOG "INFO: Reset command to return to read mode"
        ////////////////////////////////////////
        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0xff
        nrfc_idle_cycle.Run()

        glb_nrfc_param0 = 0x0000
        glb_nrfc_param1 = nftest_bank_sel
        glb_nrfc_param2 = 0
        glb_nrfc_param3 = 1
        glb_nrfc_param4 = 0
        glb_nrfc_param5 = 0
        glb_nrfc_param6 = nftest_write_wait
        nrfc_command_cycle.Run()

        glb_nrfc_param0 = 0
        glb_nrfc_param1 = 0
        glb_nrfc_param2 = 0x1f
        nrfc_idle_cycle.Run()

        LOG "INFO: Read flash data to dst register. Check Flash returned to read mode from CFI read"
        func_nrfc_param0 = nftest_reset_chkaddr
        func_nrfc_param1 = nftest_bank_sel
        func_nrfc_param2 = nftest_addr_wait
        func_nrfc_param3 = nftest_read_wait
        func_nrfc_param4 = nftest_golden_data
        nrfc_func_flashRd_dstReg.Run()

        nrfc_deselect_chip.Run()

        return PASSED
#endif
}

static int xcnrfc_hw_init(void)
{
    printk("NRFC: hw init\n");

    MMR_WRITEBIT(1, ACC_BLK_STOP0, NRFC_BLK_STOP);
    MMR_WRITEBIT(1, ACC_RESET_REG0, NRFC_RESET);
    udelay(100);
    MMR_WRITEBIT(0, ACC_BLK_STOP0, NRFC_BLK_STOP);
    MMR_WRITEBIT(0, ACC_RESET_REG0, NRFC_RESET);
    udelay(100);

    MMR_WRITEBIT(0, NRFC_CTRL_REG, RESETN);
    udelay(100);
    MMR_WRITEBIT(1, NRFC_CTRL_REG, RESETN);
    udelay(100);

//    MMR_WRITE(0xf, NRFC_CEN_SELECTION);
    MMR_WRITEBIT(1, NRFC_NFC_SEL_OVERRIDE, SEL_ENABLE);
    MMR_WRITEBIT(0, NRFC_NFC_SEL_OVERRIDE, NFC_NRFCN_EN_OVERRIDE);
    MMR_WRITEBIT(0, NRFC_NFC_SEL_OVERRIDE, SPI_OVERRIDE);

    MMR_WRITEBIT(0, NRFC_CTRL_REG, FLASH_RSTN);
    mdelay(1000);
    MMR_WRITEBIT(1, NRFC_CTRL_REG, FLASH_RSTN);
    mdelay(1000);

    //*** Bootstrap override - 8-bit IO mode  ***
    MMR_WRITEBIT(NRFC_8BIT, NRFC_BOOTSTRAP_OVRRIDE, BYTE_WORDB_OVERIDE);
    MMR_WRITEBIT(1, NRFC_BOOTSTRAP_OVRRIDE, PAGE_READ_OVERRIDE);
    MMR_WRITEBIT(1, NRFC_BOOTSTRAP_OVRRIDE, NRFC_OVRERIDE_EN);

    return 0;
}

// We could store these in the mtd structure, but we only support 1 device..
static struct mtd_info *mtd_info[4];

static int xcnrfc_erase(struct mtd_info *mtd, struct erase_info *instr)
{
    FLASHTIMING *flash;
    uint64_t t=0;
    int ret=0;

    flash=(FLASHTIMING *)mtd->priv;
    if(!flash || !flash->erase)
        return -EINVAL;

    if (instr->addr + instr->len > mtd->size)
        return -EINVAL;

    debug("E: 0x%08Lx len 0x%08Lx\n", instr->addr, instr->len);

    xcnrfc_resetFlash(flash);
    while(t<instr->len)
    {
        ret=flash->erase(flash, (uint32_t)(instr->addr+t));
        if(ret)
            return -EIO;

        debug(".");

        t+=mtd->erasesize;
    }
    instr->state = MTD_ERASE_DONE;
    mtd_erase_callback(instr);

    return ret;
}

static int xcnrfc_read(struct mtd_info *mtd, loff_t from, size_t len,
        size_t *retlen, u_char *buf)
{
    FLASHTIMING *flash;
    int ret=0;

    flash=(FLASHTIMING *)mtd->priv;
    if(!flash || !flash->read)
        return -EINVAL;

    if (from + len > mtd->size)
        return -EINVAL;

    xcnrfc_resetFlash(flash);
    if(is_vmalloc_addr(buf))
    {
        size_t c=len;
        unsigned long addr=(unsigned long)buf;

        while(c)
        {
            unsigned long physAddr=page_to_phys(vmalloc_to_page((void *)addr));
            unsigned long virtAddr=(unsigned long)phys_to_virt(physAddr);
            size_t rc=min(len, (size_t)PAGE_SIZE);

            debug("Translating vmalloc page 0x%08lx to 0x%08lx\n", addr, virtAddr);
            debug("R: 0x%08Lx len 0x%08x src 0x%08x ", from, rc, (unsigned int)virtAddr);
            ret=flash->read(flash, (uint32_t)from, (u_char *)virtAddr, rc);
            debug("return %d\n", ret);

            if(ret)
                break;

            addr+=rc;
            c-=rc;
            from+=rc;
        }

        if(!ret)
            *retlen = len;
    }
    else
    {
        debug("R: 0x%08Lx len 0x%08x src 0x%08x ", from, len, (unsigned int)buf);
        ret=flash->read(flash, (uint32_t)from, buf, len);
        if(!ret)
            *retlen = len;
        debug("return %d\n", ret);
    }


    return ret;
}

static int xcnrfc_write(struct mtd_info *mtd, loff_t to, size_t len,
        size_t *retlen, const u_char *buf)
{
    FLASHTIMING *flash;
    int ret=0;

    flash=(FLASHTIMING *)mtd->priv;
    if(!flash || !flash->write)
        return -EINVAL;

    if (to + len > mtd->size)
        return -EINVAL;

    xcnrfc_resetFlash(flash);
    if(is_vmalloc_addr(buf))
    {
        size_t c=len;
        unsigned long addr=(unsigned long)buf;

        while(c)
        {
            unsigned long physAddr=page_to_phys(vmalloc_to_page((void *)addr));
            unsigned long virtAddr=(unsigned long)phys_to_virt(physAddr);
            size_t rc=min(len, (size_t)PAGE_SIZE);

            debug("Translating vmalloc page 0x%08lx to 0x%08lx\n", addr, virtAddr);
            debug("W: 0x%08Lx len 0x%08x src 0x%08x ", to, rc, (unsigned int)virtAddr);
            ret=flash->write(flash, (uint32_t)to, (u_char *)virtAddr, rc);
            debug("return %d\n", ret);

            if(ret)
                break;

            addr+=rc;
            c-=rc;
            to+=rc;
        }

        if(!ret)
            *retlen = len;
    }
    else
    {
        debug("W: 0x%08Lx len 0x%08x src 0x%08x ", to, len, (unsigned int)buf);
        ret=flash->write(flash, (uint32_t)to, buf, len);
        if(!ret)
            *retlen = len;

        debug("return %d\n", ret);
    }

    return ret;
}

static int xcnrfc_readID(FLASHTIMING *flash)
{
    if(flash->readID)
    {
        xcnrfc_resetFlash(flash);
        return flash->readID(flash);
    }

    return -EINVAL;
}

static void cleanup_xcnrfc(void)
{
    int i;

    for(i=0;i<NUM_BANK;i++)
        if (mtd_info[i]) {
            del_mtd_device(mtd_info[i]);
            kfree(mtd_info[i]);
        }
}

int xcnrfc_init_device(struct mtd_info *mtd, FLASHTIMING *flash)
{
    printk("Initialising XCNRFC bank %d\n", flash->bank);

    xcnrfc_readID(flash);

    memset(mtd, 0, sizeof(*mtd));

    /* Setup the MTD structure */
    mtd->name = flash->name;
    mtd->type = MTD_NORFLASH;
    mtd->flags = MTD_CAP_NORFLASH;
    mtd->size = flash->size;
    mtd->writesize = 1;
    mtd->erasesize = flash->erase_size;

    mtd->owner = THIS_MODULE;
    mtd->erase = xcnrfc_erase;
    mtd->read = xcnrfc_read;
    mtd->write = xcnrfc_write;

    mtd->priv = (void *)flash;

    if (add_mtd_device(mtd)) {
        return -EIO;
    }

    return 0;
}

static int __init init_xcnrfc(void)
{
    int err, i;

    xcnrfc_hw_init();

    /* Allocate some memory */
    for (i=0;i<NUM_BANK;i++)
    {
        mtd_info[i] = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
        if (!mtd_info[i])
            return -ENOMEM;

        err = xcnrfc_init_device(mtd_info[i], flash_array[i]);
        if (err) {
            kfree(mtd_info[i]);
            mtd_info[i] = NULL;
            cleanup_xcnrfc();
            return err;
        }
    }

    return err;
}

module_init(init_xcnrfc);
module_exit(cleanup_xcnrfc);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("support@vixs.com>");
MODULE_DESCRIPTION("MTD driver for NRFC");
