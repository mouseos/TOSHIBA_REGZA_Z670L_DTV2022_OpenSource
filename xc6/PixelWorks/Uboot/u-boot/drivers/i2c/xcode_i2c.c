/*
 * Basic I2C functions for XCode
 *
 * Copyright (c) 2010 Vixs Systems Inc
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Jerry Wang (jlwang@vixs.com), Vixs Systems Inc
 * Maintained by Emerson Chi (echi@vixs.com), ViXS Systems Inc.
 *
 */

#include <common.h>

//#if defined(CONFIG_DRIVER_XCODE_I2C)&&(CONFIG_CMD_I2C)
#include <asm/arch-xc6/xcodeRegDef.h>
#define readb(addr) (*(volatile unsigned char *) (addr + XC_SOC_PROC_MMREG_BASE))
#define readw(addr) (*(volatile unsigned short *) (addr + XC_SOC_PROC_MMREG_BASE))
#define readl(addr) (*(volatile unsigned int *) (addr+ XC_SOC_PROC_MMREG_BASE))
#define writeb(b,addr) (*(volatile unsigned char *) (addr+ XC_SOC_PROC_MMREG_BASE) = (b))
#define writew(b,addr) (*(volatile unsigned short *) (addr+XC_SOC_PROC_MMREG_BASE) = (b))
#define writel(b,addr) (*(volatile unsigned int *) (addr+XC_SOC_PROC_MMREG_BASE) = (b))

/* I2C clk range from 39.59KHz - 5MHz */
#define MASTER_I2C_CLOCK                    (162000000)
#define I2C_BUS_CLOCK                       (100000)
#define MASTER_I2C_ATOMIC_PERIOD            (MASTER_I2C_CLOCK / I2C_BUS_CLOCK / 4)
#define I2CCONFIGTIMEOUT					(0xFFFF)

#define I2CCONFIGADDRESSMODE7BIT            0
#define I2CCONFIGADDRESSMODE10BIT           1

/* current i2c bus number */
static i2c_bus_number = CONFIG_SYS_I2C_BUS;

//speed: khZ
//slaveadd: own i2c Addr (if available)
void i2c_init (int speed, int slave)
{
	volatile u32 temp = 0;
	u16 atomic;

	temp = readl(PPC_SFT_RSTN);
	writel(temp & ~1, PPC_SFT_RSTN);
	udelay(100);
	writel(temp | 1, PPC_SFT_RSTN);

	if (speed == 0)
		atomic = MASTER_I2C_ATOMIC_PERIOD;
	else
		atomic = (MASTER_I2C_CLOCK / speed) / 4;

	if(i2c_bus_number) {
		temp = readl(GPIO_M_CTRL);
		temp &= ~(1 << 30);
		writel(temp, GPIO_M_CTRL);
	} else {
		temp = readl(GPIO_M_CTRL);
		temp &= ~(1 << 31);
		writel(temp, GPIO_M_CTRL);
	}

	temp |= (I2CCONFIGTIMEOUT << I2C0_CONFIG_TIMEOUT_SHIFT);
	temp |= (I2CCONFIGADDRESSMODE7BIT << I2C0_CONFIG_ADR_MODE_SHIFT);
	temp |= (atomic << I2C0_CONFIG_ATOMIC_PERIOD_SHIFT);
	writel(temp, I2C0_CONFIG);
	temp = readl(I2C0_CONFIG);
	debug ("--I2C-0 CONFIG REG:%x\n", (unsigned)temp);

	temp |= (I2CCONFIGTIMEOUT << I2C1_CONFIG_TIMEOUT_SHIFT);
	temp |= (I2CCONFIGADDRESSMODE7BIT << I2C1_CONFIG_ADR_MODE_SHIFT);
	temp |= (atomic << I2C1_CONFIG_ATOMIC_PERIOD_SHIFT);
	writel(temp, I2C1_CONFIG);
	temp = readl(I2C1_CONFIG);
	debug ("--I2C-1 CONFIG REG:%x\n", (unsigned)temp);

	if (slave != 0x15)
		writel(slave, I2C_SLAVE_ADDR_OVERRIDE);
}

//wop: 0: read 1: write
//addr_mode: 0 7 bit address: 1 10 bit address
static int xcode_i2c_read_write(u8 devaddr, u8 * value, u32 size, u32 wop, u32 start_en, u32 stop_en)
{
    int i2c_error = 0, i;
    u16 status;
    u32 *p = (u32*)value;
    u32 temp, temp1;
	u32 syspre=0, syscur=0;
	
    if(size > 64)
    {
        debug("I2C transfer size 0x%x exceeds the maximum 64\n", size);
        return 1;
    }	

    if(wop)//write
    {
        debug("--I2C0 Load DATA size %x\n", size);
        for(i = 0; i < size; i += 4)
        {
            temp = *p;
			debug ("--I2C: DATA LOAD REG[0x%x] = %x\n", I2C0_DATA_LOAD + i2c_bus_number * 0x10, (unsigned)temp);
            writel(temp, I2C0_DATA_LOAD + i2c_bus_number * 0x10);
            p++;
			debug("--I2C: FIFO[%d] = 0x%08x\n", i, readl(I2C0_DATA_READ + i2c_bus_number * 0x10));
        }
    }


    temp = (wop<<I2C0_COMMAND_WOP_SHIFT)
			|(start_en <<I2C0_COMMAND_START_EN_SHIFT)
			|(stop_en << I2C0_COMMAND_STOP_EN_SHIFT)
			|((devaddr)<<I2C0_COMMAND_SLAVE_ADR_SHIFT)
			| size;
	debug ("--I2C COMMAND REG[0x%x] = %x\n", I2C0_COMMAND + i2c_bus_number * 0x10, (unsigned)temp);
    writel(temp, I2C0_COMMAND + i2c_bus_number * 0x10);	
    
    //wait until finish
    syspre = readl(CG_SYSTEM_CNT);
    while (1) {
		temp = readl(PPC_INT_STATUS);
		syscur = readl(CG_SYSTEM_CNT);	
		if ((syscur - syspre) > 0x100000) {
			debug("Error, I2C0 time out\n");
			i2c_error = 1;
			goto i2c_exit;
		}
		if (i2c_bus_number) {
			if (temp & (I2C1_ARB_FAIL_MASK | I2C1_BUSY_ERROR_MASK)) {
				debug("Error, I2C1 HW error\n");
				i2c_error = 1;
				goto i2c_exit;
			}		
			if (temp & I2C1_XFERDONE_MASK) {
				if (readl(I2C1_COMMAND) & I2C0_COMMAND_ERROR_MASK) {
					i2c_error = 1;
					debug("Error, I2C1 Command error\n");
					goto i2c_exit;
				} else {				
					if (!wop) {
						p = (u32*)value;
						for (i=0; i<size;i+=4) {
							*p = readl(I2C1_DATA_READ);
							debug ("-- I2C1 Read data %x\n", *p);
							p++;
						}				
					}
					goto i2c_exit;
				}
			}
		} else {
			if (temp&(I2C0_ARB_FAIL_MASK|I2C0_BUSY_ERROR_MASK)) {
				debug("Error, I2C0 HW error\n");
				i2c_error = 1;
				goto i2c_exit;
			}		
			if (temp & I2C0_XFERDONE_MASK) {
				if (readl(I2C0_COMMAND)&I2C0_COMMAND_ERROR_MASK) {
					i2c_error = 1;
					debug("Error, I2C0 Command error\n");
					goto i2c_exit;
				} else {				
					if (!wop) {
						p = (u32*)value;
						for (i=0; i<size;i+=4) {
							*p = readl(I2C0_DATA_READ);
							debug ("-- I2C0 Read data %x\n", *p);
							p++;
						}				
					}
					goto i2c_exit;
				}
			}
		}
    }

i2c_exit:
    writel(temp, PPC_INT_STATUS);
    return i2c_error;
}

//Try to detect if device (i2C address) is available or not in system
int i2c_probe (uchar chip)
{
	int res = 1;

	return res;
}

int i2c_read (uchar chip, uint addr, int alen, uchar * buffer, int len)
{
	int i;
        int err = 0;

        debug("Enter I2C read chip %x addr %x alen %x len %x\n", chip, addr, alen, len);

        //write address
        err = xcode_i2c_read_write(chip,( u8 *) &addr, alen, 1, 1, 0);
        
        if(err)
        {
            debug("Write Address failed\n");
            return 1;
        }
		
        err = xcode_i2c_read_write(chip, (u8*) buffer, len, 0, 1, 1);

        if(err)
        {
            debug("Read data filed\n");
            return 1;
        }
        debug("Exit I2c read\n");
	return err;
}

int i2c_write (uchar chip, uint addr, int alen, uchar * buffer, int len)
{
	int i;
    int err = 0;

    debug("Enter I2C write chip %x addr %x alen %x len %x\n", chip, addr, alen, len);

    err = xcode_i2c_read_write(chip, (u8*)&addr, alen, 1, 1, 0);
    if(err) {
		debug("I2C Write address fail\n");
        return 1;
    }
	
    err = xcode_i2c_read_write(chip, (u8*)buffer, len, 1, 0, 1);
    if(err) {
		debug("I2C Write data fail\n");
        return 1;
    }
		
    debug("Exit I2C write\n");
	return err;
}

int i2c_set_bus_num(int idx)
{
	volatile u32 temp;
	i2c_bus_number = idx & 0x1;
	if (idx) {
		temp = readl(GPIO_M_CTRL);
		temp &= ~(1 << 30);
		writel(temp, GPIO_M_CTRL);
	} else  {
		temp = readl(GPIO_M_CTRL);
		temp &= ~(1 << 31);
		writel(temp, GPIO_M_CTRL);
	}

	return 0;
}

int i2c_get_bus_num(void)
{
	return i2c_bus_number;
}

int i2c_set_bus_speed(int speed)
{
	volatile u32 temp;
	u16 atomic = (MASTER_I2C_CLOCK / speed) / 4;
	debug("speed %d atomic 0x%04x\n", speed, atomic);
	temp = readl(I2C0_CONFIG + i2c_bus_number * 0x10);
	temp &= ~I2C0_CONFIG_ATOMIC_PERIOD_MASK;
	temp |= atomic << I2C0_CONFIG_ATOMIC_PERIOD_SHIFT;
	writel(temp, I2C0_CONFIG + i2c_bus_number * 0x10);
	return 0;	
}

int i2c_get_bus_speed(void)
{
	volatile u32 temp;
	temp = readl(I2C0_CONFIG + i2c_bus_number * 0x10);
	temp &= I2C0_CONFIG_ATOMIC_PERIOD_MASK;
	temp  >>= I2C0_CONFIG_ATOMIC_PERIOD_SHIFT;
	return ((MASTER_I2C_CLOCK / temp) / 4);
}
//#endif /* CONFIG_DRIVER_XCODE_I2C */
