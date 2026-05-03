/** \file
 * This file defines the synopsys GMAC device dependent functions.
 * Most of the operations on the GMAC device are available in this file.
 * Functions for initiliasing and accessing MAC/DMA/PHY registers and the DMA descriptors
 * are encapsulated in this file. The functions are platform/host/OS independent.
 * These functions in turn use the low level device dependent (HAL) functions to 
 * access the register space.
 * \internal
 * ------------------------REVISION HISTORY---------------------------------
 * Synopsys                 01/Aug/2007                              Created
 */
#include <linux/delay.h>
#include "synopGMAC_Dev.h"

//#define SKB_OVERWRITTEN_DEBUG

/* If it is MAC to MAC connection, no PHY init and link status check, the link is always on */
u32 synopGMAC_is_MAC_to_MAC(void)
{
#ifdef CONFIG_PLAT_XCODE64xx    
	u32 board_id = mmr_read(CG_DUMMY_REG1);
	switch(board_id) {
	case 0x1900:
		return SPEED_1000;
	default:
		return 0;
	}
#else
    return 0;
#endif
}

/**
  * Function to set the MDC clock for mdio transactiona
  *
  * @param[in] pointer to device structure.
  * @param[in] clk divider value.
  * \return Reuturns 0 on success else return the error value.
  */
s32 synopGMAC_set_mdc_clk_div(synopGMACdevice *gmacdev,u32 clk_div_val)
{
	u32 orig_data;
	orig_data = synopGMACReadReg((u32 *)gmacdev->MacBase,GmacGmiiAddr); //set the mdc clock to the user defined value
	orig_data &= (~ GmiiCsrClkMask);	   
	orig_data |= clk_div_val;
	synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacGmiiAddr ,orig_data);
	return 0;
}

/**
  * Returns the current MDC divider value programmed in the ip.
  *
  * @param[in] pointer to device structure.
  * @param[in] clk divider value.
  * \return Returns the MDC divider value read.
  */
u32 synopGMAC_get_mdc_clk_div(synopGMACdevice *gmacdev)
{
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->MacBase,GmacGmiiAddr);
	data &= GmiiCsrClkMask;
	return data;
}



/**
  * Function to read the Phy register. The access to phy register
  * is a slow process as the data is moved accross MDI/MDO interface
  * @param[in] pointer to Register Base (It is the mac base in our case) .
  * @param[in] PhyBase register is the index of one of supported 32 PHY devices.
  * @param[in] Register offset is the index of one of the 32 phy register.
  * @param[out] u16 data read from the respective phy register (only valid iff return value is 0).
  * \return Returns 0 on success else return the error status.
  */
s32 synopGMAC_read_phy_reg(synopGMACdevice *gmacdev,u32 *RegBase,u32 PhyBase, u32 RegOffset, u16 * data)
{
    u32 addr;
    u32 loop_variable;

    addr = ((PhyBase << GmiiDevShift) & GmiiDevMask) | ((RegOffset << GmiiRegShift) & GmiiRegMask);
    addr = addr | GmiiBusy ; //Gmii busy bit
    addr |= GmiiCsrClk5;

    synopGMACWriteReg(RegBase,GmacGmiiAddr,addr); //write the address from where the data to be read in GmiiGmiiAddr register of synopGMAC ip

    for(loop_variable = 0; loop_variable < DEFAULT_LOOP_VARIABLE; loop_variable++){ //Wait till the busy bit gets cleared with in a certain amount of time
        if (!(synopGMACReadReg(RegBase,GmacGmiiAddr) & GmiiBusy)){
            break;
        }
        plat_delay(DEFAULT_DELAY_VARIABLE);
    }

    if(loop_variable < DEFAULT_LOOP_VARIABLE)
        * data = (u16)(synopGMACReadReg(RegBase,GmacGmiiData) & 0xFFFF);
    else{
        TR0("Error::: PHY not responding Busy bit didnot get cleared !!!!!!\n");
	    return -ESYNOPGMACPHYERR;
    }

    return -ESYNOPGMACNOERR;
}

/**
  * Function to write to the Phy register. The access to phy register
  * is a slow process as the data is moved accross MDI/MDO interface
  * @param[in] pointer to Register Base (It is the mac base in our case) .
  * @param[in] PhyBase register is the index of one of supported 32 PHY devices.
  * @param[in] Register offset is the index of one of the 32 phy register.
  * @param[in] data to be written to the respective phy register.
  * \return Returns 0 on success else return the error status.
  */
//s32 synopGMAC_write_phy_reg(u32 *RegBase, u32 PhyBase, u32 RegOffset, u16 data)
s32 synopGMAC_write_phy_reg(synopGMACdevice * gmacdev,u32 *RegBase, u32 PhyBase, u32 RegOffset, u16 data)
{
    u32 addr;
    u32 loop_variable;

    synopGMACWriteReg(RegBase,GmacGmiiData,data); // write the data in to GmacGmiiData register of synopGMAC ip

    addr = ((PhyBase << GmiiDevShift) & GmiiDevMask) | ((RegOffset << GmiiRegShift) & GmiiRegMask) | GmiiWrite;
    addr |= GmiiBusy ; //set Gmii clk to 20-35 Mhz and Gmii busy bit
    //addr |= GmiiCsrClk1; 
 
    synopGMACWriteReg(RegBase,GmacGmiiAddr,addr);

    for(loop_variable = 0; loop_variable < DEFAULT_LOOP_VARIABLE; loop_variable++){
        if (!(synopGMACReadReg(RegBase,GmacGmiiAddr) & GmiiBusy)){
            break;
        }
        plat_delay(DEFAULT_DELAY_VARIABLE);
    }

    if(loop_variable < DEFAULT_LOOP_VARIABLE){
	    return -ESYNOPGMACNOERR;
	}
    else{
        TR("Error::: PHY not responding Busy bit didnot get cleared !!!!!!\n");
	    return -ESYNOPGMACPHYERR;
    }
}

/**
  * Function to configure the phy in loopback mode. 
  *
  * @param[in] pointer to synopGMACdevice.
  * @param[in] enable or disable the loopback.
  * \return 0 on success else return the error status.
  * \note Don't get confused with mac loop-back synopGMAC_loopback_on(synopGMACdevice *) 
  * and synopGMAC_loopback_off(synopGMACdevice *) functions.
  */
s32 synopGMAC_phy_loopback(synopGMACdevice *gmacdev, bool loopback)
{
s32 status = -ESYNOPGMACNOERR;
if(loopback)
	//status = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, Mii_Loopback);
	status = synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, Mii_Loopback);

else
	//status = synopGMAC_write_phy_reg((u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, Mii_NoLoopback);
	status = synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase, gmacdev->PhyBase, PHY_CONTROL_REG, Mii_NoLoopback);

return status;
}



/**
  * Function to read the GMAC IP Version and populates the same in device data structure.
  * @param[in] pointer to synopGMACdevice.
  * \return Always return 0.
  */

s32 synopGMAC_read_version (synopGMACdevice * gmacdev) 
{	
	u32 data = 0;
	data = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacVersion );
	gmacdev->Version = data;
	TR("The data read from %08x is %08x\n",(gmacdev->MacBase+GmacVersion),data);
	return 0;
}


/**
  * Function to reset the GMAC core. 
  * This reests the DMA and GMAC core. After reset all the registers holds their respective reset value
  * @param[in] pointer to synopGMACdevice.
  * \return 0 on success else return the error status.
  */
s32 synopGMAC_reset (synopGMACdevice * gmacdev ) 
{	
	u32 data = 0;

	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaBusMode ,DmaResetOn);
    plat_delay(DEFAULT_LOOP_VARIABLE);
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaBusMode);
	TR("DATA after Reset = %08x\n",data);
	
	return 0;	
}


/**
  * Function to program DMA bus mode register. 
  * 
  * The Bus Mode register is programmed with the value given. The bits to be set are
  * bit wise or'ed and sent as the second argument to this function.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] the data to be programmed.
  * \return 0 on success else return the error status.
  */
s32 synopGMAC_dma_bus_mode_init(synopGMACdevice * gmacdev, u32 init_value )
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaBusMode ,init_value);
	return 0;

}

/**
  * Function to program DMA Control register. 
  * 
  * The Dma Control register is programmed with the value given. The bits to be set are
  * bit wise or'ed and sent as the second argument to this function.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] the data to be programmed.
  * \return 0 on success else return the error status.
  */
s32 synopGMAC_dma_control_init(synopGMACdevice * gmacdev, u32 init_value)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl, init_value);
	return 0;
}


/*Gmac configuration functions*/

/**
  * Enable the watchdog timer on the receiver. 
  * When enabled, Gmac enables Watchdog timer, and GMAC allows no more than
  * 2048 bytes of data (10,240 if Jumbo frame enabled).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_wd_enable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacWatchdog);
	return;
}
/**
  * Disable the watchdog timer on the receiver. 
  * When disabled, Gmac disabled watchdog timer, and can receive frames up to
  * 16,384 bytes.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_wd_disable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacWatchdog);
	return;
}

/**
  * Enables the Jabber frame support. 
  * When enabled, GMAC disabled the jabber timer, and can transfer 16,384 byte frames.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_jab_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacJabber);
	return;
}
/**
  * Disables the Jabber frame support. 
  * When disabled, GMAC enables jabber timer. It cuts of transmitter if application 
  * sends more than 2048 bytes of data (10240 if Jumbo frame enabled).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_jab_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacJabber);
	return;
}

/**
  * Enables Frame bursting (Only in Half Duplex Mode). 
  * When enabled, GMAC allows frame bursting in GMII Half Duplex mode.
  * Reserved in 10/100 and Full-Duplex configurations.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_frame_burst_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacFrameBurst);
	return;
}
/**
  * Disables Frame bursting. 
  * When Disabled, frame bursting is not supported.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_frame_burst_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacFrameBurst);
	return;
}

/**
  * Enable Jumbo frame support. 
  * When Enabled GMAC supports jumbo frames of 9018/9022(VLAN tagged).
  * Giant frame error is not reported in receive frame status.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_jumbo_frame_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacJumboFrame);
	return;
}
/**
  * Disable Jumbo frame support. 
  * When Disabled GMAC does not supports jumbo frames.
  * Giant frame error is reported in receive frame status.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_jumbo_frame_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacJumboFrame);
	return;
}

/**
  * Disable Carrier sense. 
  * When Disabled GMAC ignores CRS signal during frame transmission
  * in half duplex mode.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */

void synopGMAC_disable_crs(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacDisableCrs);
	return;
}



/**
  * Selects the GMII port. 
  * When called GMII (1000Mbps) port is selected (programmable only in 10/100/1000 Mbps configuration).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_select_gmii(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacMiiGmii);
	return;
}
/**
  * Selects the MII port. 
  * When called MII (10/100Mbps) port is selected (programmable only in 10/100/1000 Mbps configuration).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_select_mii(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacMiiGmii);
	return;
}

/**
  * Enables Receive Own bit (Only in Half Duplex Mode). 
  * When enaled GMAC receives all the packets given by phy while transmitting.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_rx_own_enable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRxOwn);
	return;
}
/**
  * Disables Receive Own bit (Only in Half Duplex Mode). 
  * When enaled GMAC disables the reception of frames when gmii_txen_o is asserted.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_rx_own_disable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRxOwn);
	return;
}

/**
  * Sets the GMAC in loopback mode. 
  * When on GMAC operates in loop-back mode at GMII/MII.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  * \note (G)MII Receive clock is required for loopback to work properly, as transmit clock is
  * not looped back internally.
  */
void synopGMAC_loopback_on(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacLoopback);
	return;
}
/**
  * Sets the GMAC in Normal mode. 
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_loopback_off(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacLoopback);
	return;
}

/**
  * Sets the GMAC core in Full-Duplex mode. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_set_full_duplex(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacDuplex);
	return;
}
/**
  * Sets the GMAC core in Half-Duplex mode. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_set_half_duplex(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacDuplex);
	return;
}

/**
  * GMAC tries retransmission (Only in Half Duplex mode).
  * If collision occurs on the GMII/MII, GMAC attempt retries based on the 
  * back off limit configured. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  * \note This function is tightly coupled with synopGMAC_back_off_limit(synopGMACdev *, u32).
  */
void synopGMAC_retry_enable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRetry);
	return;
}
/**
  * GMAC tries only one transmission (Only in Half Duplex mode).
  * If collision occurs on the GMII/MII, GMAC will ignore the current frami
  * transmission and report a frame abort with excessive collision in tranmit frame status. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_retry_disable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRetry);
	return;
}

/**
  * GMAC strips the Pad/FCS field of incoming frames.
  * This is true only if the length field value is less than or equal to
  * 1500 bytes. All received frames with length field greater than or equal to
  * 1501 bytes are passed to the application without stripping the Pad/FCS field. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_pad_crc_strip_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacPadCrcStrip);
	return;
}
/**
  * GMAC doesnot strips the Pad/FCS field of incoming frames.
  * GMAC will pass all the incoming frames to Host unmodified. 
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_pad_crc_strip_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacPadCrcStrip);
	return;
}
/**
  * GMAC programmed with the back off limit value.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  * \note This function is tightly coupled with synopGMAC_retry_enable(synopGMACdevice * gmacdev)
  */
void synopGMAC_back_off_limit(synopGMACdevice * gmacdev, u32 value)
{
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacConfig);
	data &= (~GmacBackoffLimit);
	data |= value;
	synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacConfig,data);
	return;
}

/**
  * Enables the Deferral check in GMAC (Only in Half Duplex mode)
  * GMAC issues a Frame Abort Status, along with the excessive deferral error bit set in the 
  * transmit frame status when transmit state machine is deferred for more than
  * 	- 24,288 bit times in 10/100Mbps mode
  * 	- 155,680 bit times in 1000Mbps mode or Jumbo frame mode in 10/100Mbps operation. 
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  * \note Deferral begins when transmitter is ready to transmit, but is prevented because  of
  * an active CRS (carrier sense) 
  */
void synopGMAC_deferral_check_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacDeferralCheck);
	return;
}
/**
  * Disables the Deferral check in GMAC (Only in Half Duplex mode).
  * GMAC defers until the CRS signal goes inactive.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_deferral_check_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacDeferralCheck);
	return;
}
/**
  * Enable the reception of frames on GMII/MII.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_rx_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRx);
	return;
}
/**
  * Disable the reception of frames on GMII/MII.
  * GMAC receive state machine is disabled after completion of reception of current frame.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_rx_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacRx);
	return;
}
/**
  * Enable the transmission of frames on GMII/MII.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_tx_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacConfig, GmacTx);
	return;
}
/**
  * Disable the transmission of frames on GMII/MII.
  * GMAC transmit state machine is disabled after completion of transmission of current frame.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_tx_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacConfig, GmacTx);
	return;
}


/*Receive frame filter configuration functions*/

/**
  * Enables reception of all the frames to application.
  * GMAC passes all the frames received to application irrespective of whether they
  * pass SA/DA address filtering or not.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_frame_filter_enable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacFilter);
	return;
}
/**
  * Disables reception of all the frames to application.
  * GMAC passes only those received frames to application which 
  * pass SA/DA address filtering.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_frame_filter_disable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacFilter);
	return;
}

/**
  * Populates the Hash High register with the data supplied.
  * This function is called when the Hash filtering is to be enabled.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] data to be written to hash table high register.
  * \return void. 
  */
void synopGMAC_write_hash_table_high(synopGMACdevice * gmacdev, u32 data)
{
	synopGMACWriteReg((u32 *)gmacdev->MacBase,GmacHashHigh,data);
	return;
}

/**
  * Populates the Hash Low register with the data supplied.
  * This function is called when the Hash filtering is to be enabled.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] data to be written to hash table low register.
  * \return void. 
  */
void synopGMAC_write_hash_table_low(synopGMACdevice * gmacdev, u32 data)
{
	synopGMACWriteReg((u32 *)gmacdev->MacBase,GmacHashLow,data);
	return;
}

/**
  * Enables Hash or Perfect filter (only if Hash filter is enabled in H/W).
  * Only frames matching either perfect filtering or Hash Filtering as per HMC and HUC 
  * configuration are sent to application.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_hash_perfect_filter_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacHashPerfectFilter);
	return;
}

/**
  * Enables only Hash(only if Hash filter is enabled in H/W).
  * Only frames matching Hash Filtering as per HMC and HUC 
  * configuration are sent to application.
  * @param[in] pointer to synopGMACdevice.
  * \return void. 
  */
void synopGMAC_Hash_filter_only_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacHashPerfectFilter);
	return;
}

/**
  * Enables Source address filtering.
  * When enabled source address filtering is performed. Only frames matching SA filtering are passed  to application with 
  * SAMatch bit of RxStatus is set. GMAC drops failed frames. 
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  * \note This function is overriden by synopGMAC_frame_filter_disable(synopGMACdevice *) 
  */
void synopGMAC_src_addr_filter_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacSrcAddrFilter);
	return;
}
/**
  * Disables Source address filtering.
  * When disabled GMAC forwards the received frames with updated SAMatch bit in RxStatus. 
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_src_addr_filter_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacSrcAddrFilter);
	return;
}
/**
  * Enables Inverse Destination address filtering.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_dst_addr_filter_inverse(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacDestAddrFilterNor);
	return;
}
/**
  * Enables the normal Destination address filtering.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_dst_addr_filter_normal(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacDestAddrFilterNor);
	return;
}

/**
  * Enables forwarding of control frames.
  * When set forwards all the control frames (incl. unicast and multicast PAUSE frames).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  * \note Depends on RFE of FlowControlRegister[2]
  */
void synopGMAC_set_pass_control(synopGMACdevice * gmacdev,u32 passcontrol)
{	
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacFrameFilter);
	data &= (~GmacPassControl);
	data |= passcontrol;
	synopGMACWriteReg((u32 *)gmacdev->MacBase,GmacFrameFilter,data);
	return;
}

/**
  * Enables Broadcast frames.
  * When enabled Address filtering module passes all incoming broadcast frames.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_broadcast_enable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacBroadcast);
	return;
}
/**
  * Disable Broadcast frames.
  * When disabled Address filtering module filters all incoming broadcast frames.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_broadcast_disable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacBroadcast);
	return;
}

/**
  * Enables Multicast frames.
  * When enabled all multicast frames are passed.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_multicast_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacMulticastFilter);
	return;
}
/**
  * Disable Multicast frames.
  * When disabled multicast frame filtering depends on HMC bit.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_multicast_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacMulticastFilter);
	return;
}

/**
  * Enables multicast hash filtering.
  * When enabled GMAC performs teh destination address filtering according to the hash table.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_multicast_hash_filter_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacMcastHashFilter);
	return;
}
/**
  * Disables multicast hash filtering.
  * When disabled GMAC performs perfect destination address filtering for multicast frames, it compares 
  * DA field with the value programmed in DA register.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_multicast_hash_filter_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacMcastHashFilter);
	return;
}

/**
  * Enables promiscous mode.
  * When enabled Address filter modules pass all incoming frames regardless of their Destination
  * and source addresses.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_promisc_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacPromiscuousMode);
	return;
}
/**
  * Clears promiscous mode.
  * When called the GMAC falls back to normal operation from promiscous mode.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_promisc_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacPromiscuousMode);
	return;
}


/**
  * Enables unicast hash filtering.
  * When enabled GMAC performs the destination address filtering of unicast frames according to the hash table.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_unicast_hash_filter_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacUcastHashFilter);
	return;
}
/**
  * Disables multicast hash filtering.
  * When disabled GMAC performs perfect destination address filtering for unicast frames, it compares 
  * DA field with the value programmed in DA register.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_unicast_hash_filter_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFrameFilter, GmacUcastHashFilter);
	return;
}
	
/*Flow control configuration functions*/

/**
  * Enables detection of pause frames with stations unicast address.
  * When enabled GMAC detects the pause frames with stations unicast address in addition to the
  * detection of pause frames with unique multicast address.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_unicast_pause_frame_detect_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacUnicastPauseFrame);
	return;
}
/**
  * Disables detection of pause frames with stations unicast address.
  * When disabled GMAC only detects with the unique multicast address (802.3x).
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_unicast_pause_frame_detect_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacUnicastPauseFrame);
	return;
}
/**
  * Rx flow control enable.
  * When Enabled GMAC will decode the rx pause frame and disable the tx for a specified time.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_rx_flow_control_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacRxFlowControl);
	return;
}
/**
  * Rx flow control disable.
  * When disabled GMAC will not decode pause frame.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_rx_flow_control_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacRxFlowControl);
	return;
}
/**
  * Tx flow control enable.
  * When Enabled 
  * 	- In full duplex GMAC enables flow control operation to transmit pause frames.
  *	- In Half duplex GMAC enables the back pressure operation
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_tx_flow_control_enable(synopGMACdevice * gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacTxFlowControl);
	return;
}

/**
  * Tx flow control disable.
  * When Disabled 
  * 	- In full duplex GMAC will not transmit any pause frames.
  *	- In Half duplex GMAC disables the back pressure feature.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_tx_flow_control_disable(synopGMACdevice * gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacTxFlowControl);
	return;
}

/**
  * Initiate Flowcontrol operation.
  * When Set
  * 	- In full duplex GMAC initiates pause control frame.
  *	- In Half duplex GMAC initiates back pressure function.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_tx_activate_flow_control(synopGMACdevice * gmacdev)
{
	//In case of full duplex check for this bit to b'0. if it is read as b'1 indicates that
        //control frame transmission is in progress.
	if(gmacdev->Speed == DUPLEX_FULL){
		if(!synopGMACCheckBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacFlowControlBackPressure))
			synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacFlowControlBackPressure);
	}
	else{ //if half duplex mode
		
		synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacFlowControlBackPressure);
	}

	return;
}

/**
  * stops Flowcontrol operation.
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_tx_deactivate_flow_control(synopGMACdevice * gmacdev)
{
	//In full duplex this bit is automatically cleared after transmitting a pause control frame.
	if(gmacdev->Speed == DUPLEX_HALF){
	synopGMACSetBits((u32 *)gmacdev->MacBase, GmacFlowControl, GmacFlowControlBackPressure);
	}
	return;
}

/**
  * Added on 3.60a
  * This enables the pause frame generation after programming the appropriate registers.
  * presently activation is set at 3k and deactivation set at 4k. These may have to tweaked
  * if found any issues
  * @param[in] pointer to synopGMACdevice.
  * \return void.
  */
void synopGMAC_pause_control(synopGMACdevice *gmacdev)
{
	u32 omr_reg;
	u32 mac_flow_control_reg;
	omr_reg = synopGMACReadReg((u32 *)gmacdev->DmaBase,DmaControl);
	omr_reg |= DmaRxFlowCtrlAct1K | DmaRxFlowCtrlDeact3K |DmaEnHwFlowCtrl;
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl, omr_reg);

	mac_flow_control_reg = synopGMACReadReg((u32 *)gmacdev->MacBase,GmacFlowControl);
	mac_flow_control_reg |= GmacRxFlowControl | GmacTxFlowControl | 0xFFFF0000;
	synopGMACWriteReg((u32 *)gmacdev->MacBase,GmacFlowControl,mac_flow_control_reg);

	return;

}


/**
  * Example mac initialization sequence.
  * This function calls the initialization routines to initialize the GMAC register.
  * One can change the functions invoked here to have different configuration as per the requirement
  * @param[in] pointer to synopGMACdevice.
  * \return Returns 0 on success.
  */
s32 synopGMAC_mac_init(synopGMACdevice *gmacdev)
{
	//u32 PHYreg;

	if(gmacdev->DuplexMode == DUPLEX_FULL){
		synopGMAC_wd_enable(gmacdev);

        // by Stanley	
		//synopGMAC_jab_enable(gmacdev);
		//synopGMAC_frame_burst_enable(gmacdev);

		synopGMAC_jumbo_frame_disable(gmacdev);
		
		//by Stanley
		//synopGMAC_rx_own_enable(gmacdev);
		synopGMAC_rx_own_disable(gmacdev);
		
		synopGMAC_loopback_off(gmacdev);
		synopGMAC_set_full_duplex(gmacdev);
		synopGMAC_retry_enable(gmacdev);
		
		synopGMAC_pad_crc_strip_disable(gmacdev);
		synopGMAC_back_off_limit(gmacdev,GmacBackoffLimit0);
		synopGMAC_deferral_check_disable(gmacdev);
		synopGMAC_tx_enable(gmacdev);	
		synopGMAC_rx_enable(gmacdev);

		if(gmacdev->Speed == SPEED_1000)
		{
			synopGMAC_select_gmii(gmacdev);
		}
		else
		{
		    synopGMAC_select_mii(gmacdev);
        }
        
		/*Frame Filter Configuration*/
	 	synopGMAC_frame_filter_enable(gmacdev);
		synopGMAC_set_pass_control(gmacdev,GmacPassControl0);
		synopGMAC_broadcast_enable(gmacdev);
		synopGMAC_src_addr_filter_disable(gmacdev);
		synopGMAC_multicast_disable(gmacdev);
		synopGMAC_dst_addr_filter_normal(gmacdev);
		synopGMAC_multicast_hash_filter_disable(gmacdev);
		//synopGMAC_promisc_disable(gmacdev);
		synopGMAC_promisc_enable(gmacdev);
		synopGMAC_unicast_hash_filter_disable(gmacdev);

		/*Flow Control Configuration*/
		//by Stanley
		//synopGMAC_unicast_pause_frame_detect_disable(gmacdev);
		synopGMAC_unicast_pause_frame_detect_enable(gmacdev);
		
		synopGMAC_rx_flow_control_enable(gmacdev);
		synopGMAC_tx_flow_control_enable(gmacdev);
	}
	else
	{
	    //for Half Duplex configuration
		synopGMAC_wd_enable(gmacdev);

		//by Stanley
		//synopGMAC_jab_enable(gmacdev);
		//synopGMAC_frame_burst_enable(gmacdev);
		synopGMAC_unicast_pause_frame_detect_enable(gmacdev);	

		synopGMAC_jumbo_frame_disable(gmacdev);
		
		//by Stanley
		//synopGMAC_rx_own_enable(gmacdev);
		synopGMAC_rx_own_disable(gmacdev);


		synopGMAC_loopback_off(gmacdev);
		synopGMAC_set_half_duplex(gmacdev);
		synopGMAC_retry_enable(gmacdev);
		synopGMAC_pad_crc_strip_disable(gmacdev);
		synopGMAC_back_off_limit(gmacdev,GmacBackoffLimit0);
		synopGMAC_deferral_check_disable(gmacdev);
		synopGMAC_tx_enable(gmacdev);	
		synopGMAC_rx_enable(gmacdev);

		if(gmacdev->Speed == SPEED_1000)
			synopGMAC_select_gmii(gmacdev);
		else
			synopGMAC_select_mii(gmacdev);

		/*Frame Filter Configuration*/
	 	synopGMAC_frame_filter_enable(gmacdev);
		synopGMAC_set_pass_control(gmacdev,GmacPassControl0);
		synopGMAC_broadcast_enable(gmacdev);
		synopGMAC_src_addr_filter_disable(gmacdev);
		synopGMAC_multicast_disable(gmacdev);
		synopGMAC_dst_addr_filter_normal(gmacdev);
		synopGMAC_multicast_hash_filter_disable(gmacdev);
		synopGMAC_promisc_disable(gmacdev);
		synopGMAC_unicast_hash_filter_disable(gmacdev);
		
		/*Flow Control Configuration*/
		//by Stanley
		//synopGMAC_unicast_pause_frame_detect_disable(gmacdev);
		synopGMAC_unicast_pause_frame_detect_enable(gmacdev);

		synopGMAC_rx_flow_control_disable(gmacdev);
		synopGMAC_tx_flow_control_disable(gmacdev);

		/*To set PHY register to enable CRS on Transmit*/
		//synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacGmiiAddr, GmiiBusy | 0x00000408);
		//PHYreg = synopGMACReadReg((u32 *)gmacdev->MacBase,GmacGmiiData);
		//synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacGmiiData, PHYreg   | 0x00000800);
		//synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacGmiiAddr, GmiiBusy | 0x0000040a);
        {
            u16 regval;
            synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_SPECIFIC_CTRL_REG, &regval);        
            synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_SPECIFIC_CTRL_REG, regval|0x0800);        
        }
	}

	//by Stanley
	synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacConfig, synopGMACReadReg((u32 *)gmacdev->MacBase, GmacConfig) | GmacInterFrameGap2);
	
	return 0;
}


/**
  * Checks and initialze phy.
  * This function checks whether the phy initialization is complete. 
  * @param[in] pointer to synopGMACdevice.
  * \return 0 if success else returns the error number.
  */

void vixs_eth_fix_clk_source(synopGMACdevice *gmacdev)
{
	TR("Change clock source to %d\n", gmacdev->Speed);
	switch(gmacdev->Speed)
	{

#ifdef CONFIG_PLAT_XCODE64xx
        case SPEED_1000:
		{
			u32 i;
	
			i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			if(gmacdev->reg==0)
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR0_TXCLK_SRC_SEL_MASK;
			}
			else
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR1_TXCLK_SRC_SEL_MASK;
			}
			writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			break;
		}	
		case SPEED_100:
		{
			u32 i;
	
			i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			if(gmacdev->reg==0)
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR0_TXCLK_SRC_SEL_MASK;
				i|=(1<<CG1_CLK_SRC_SEL8_ETHR0_TXCLK_SRC_SEL_SHIFT);
			}
			else
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR1_TXCLK_SRC_SEL_MASK;
				i|=(1<<CG1_CLK_SRC_SEL8_ETHR1_TXCLK_SRC_SEL_SHIFT);
			}
			writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			break;
		}	
		case SPEED_10:
		{
			u32 i;
	
			i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			if(gmacdev->reg==0)
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR0_TXCLK_SRC_SEL_MASK;
				i|=(2<<CG1_CLK_SRC_SEL8_ETHR0_TXCLK_SRC_SEL_SHIFT);
			}
			else
			{
				i&=~CG1_CLK_SRC_SEL8_ETHR1_TXCLK_SRC_SEL_MASK;
				i|=(2<<CG1_CLK_SRC_SEL8_ETHR1_TXCLK_SRC_SEL_SHIFT);
			}
			writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL8));
			break;
		}
#else
        case SPEED_1000:
        {
            u32 i;

            i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            if(gmacdev->reg==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
            }
            writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            break;
        }   
        case SPEED_100:
        {
            u32 i;

            i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            if(gmacdev->reg==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
                i|=(1<<CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_SHIFT);
            }
            writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            break;
        }   
        case SPEED_10:
        {
            u32 i;

            i=readl((unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            if(gmacdev->reg==0)
            {
                i&=~CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_MASK;
                i|=(2<<CG1_CLK_SRC_SEL4_ETHR0_TXCLK_SRC_SEL_SHIFT);
            }
            writel(i, (unsigned long *)(gmacdev->baseAddr + CG1_CLK_SRC_SEL4));
            break;
        }

#endif
		default:
			printk("Unknown speed\n");
			break;
	}
}

s32 synopGMAC_check_phy_init (synopGMACdevice * gmacdev) 
{	
	//u32 addr;  
	u16 data;
	s32 status = -ESYNOPGMACNOERR;		
	s32 loop_count;	
    struct ethtool_cmd ecmd;

	/* Skip PHY init for MAC to MAC */
	if (synopGMAC_is_MAC_to_MAC()) {
		gmacdev->ecmd.speed = synopGMAC_is_MAC_to_MAC();
		gmacdev->ecmd.duplex = DUPLEX_FULL;
		gmacdev->LinkState = LINKUP;
		gmacdev->Speed = gmacdev->ecmd.speed;
		gmacdev->DuplexMode = DUPLEX_FULL;
		vixs_eth_fix_clk_source(gmacdev);
		return -ESYNOPGMACNOERR;
	} else 

#ifdef VIPER_STACK_FPGA
#define SYNOPSYS_FORCE_10M
#endif

#ifdef SYNOPSYS_FORCE_10M
	TR0("Force to 10Mbps\n");
	synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_AN_ADV_REG, &data);
	data = 0x41;   // 10M full deplex
	TR0("PHY_AN_ADV_REG=0x%08x\n", data);
	synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_AN_ADV_REG, data);
    data = 0x1200;	// restart auto-negotiation
	synopGMAC_write_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_CONTROL_REG, data);	
#else
	if((!gmacdev->mii.force_media) &&(!gmacdev->autoneg_done))
	{
		mii_ethtool_sset(&gmacdev->mii,&gmacdev->ecmd);	
	}
#endif

	#define MAX_AUTO_NEGOTIATION_LOOP   50
	#define AUTO_NEGOTIATION_INTERVAL   100

	if((!gmacdev->mii.force_media) &&(!gmacdev->autoneg_done))
	{
		loop_count = MAX_AUTO_NEGOTIATION_LOOP; // total loop 5 seconds. DEFAULT_LOOP_VARIABLE;
		while(loop_count-- > 0)
		{

			status = synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_STATUS_REG, &data);
			if(status)	
				return status;

			if((data & Mii_AutoNegCmplt) != 0)
			{
				TR0("Autonegotiation Complete, PHY_STATUS_REG=0x%08x\n", data);
				synopGMAC_read_phy_reg(gmacdev,(u32 *)gmacdev->MacBase,gmacdev->PhyBase,PHY_SPECIFIC_STATUS_REG, &data);
				TR0("Autonegotiation Complete, PHY_SPECIFIC_STATUS_REG=0x%08x\n", data);
				gmacdev->autoneg_done = 1;
				break;
			}

			msleep(AUTO_NEGOTIATION_INTERVAL);  // sleep 100ms
		}

	   TR0("take %d ms to auto-negotiation\n", (MAX_AUTO_NEGOTIATION_LOOP-loop_count-1) * AUTO_NEGOTIATION_INTERVAL);
 
	}
    
	if(!mii_link_ok(&gmacdev->mii))
	{
		TR0("Link down\n");
		gmacdev->LinkState = LINKDOWN; 
		gmacdev->autoneg_done = 0;
		return -ESYNOPGMACPHYERR;
	}
	else
	{
		TR0("Link up\n");
		gmacdev->LinkState = LINKUP; 
	}

	mii_ethtool_gset(&gmacdev->mii,&ecmd);
	TR0("Speed is %d %d\n", ecmd.speed, ecmd.duplex);
	gmacdev->Speed = ecmd.speed;	
	vixs_eth_fix_clk_source(gmacdev);
	gmacdev->DuplexMode = ecmd.duplex;

	return -ESYNOPGMACNOERR;
}

/**
  * Sets the Mac address in to GMAC register.
  * This function sets the MAC address to the MAC register in question.
  * @param[in] pointer to synopGMACdevice to populate mac dma and phy addresses.
  * @param[in] Register offset for Mac address high
  * @param[in] Register offset for Mac address low
  * @param[in] buffer containing mac address to be programmed.
  * \return 0 upon success. Error code upon failure.
  */
s32 synopGMAC_set_mac_addr(synopGMACdevice *gmacdev, u32 MacHigh, u32 MacLow, u8 *MacAddr)
{
	u32 data;
	
	data = (MacAddr[5] << 8) | MacAddr[4];
	synopGMACWriteReg((u32 *)gmacdev->MacBase,MacHigh,data);
	data = (MacAddr[3] << 24) | (MacAddr[2] << 16) | (MacAddr[1] << 8) | MacAddr[0] ;
	synopGMACWriteReg((u32 *)gmacdev->MacBase,MacLow,data);
	return 0;
}


/**
  * Get the Mac address in to the address specified.
  * The mac register contents are read and written to buffer passed.
  * @param[in] pointer to synopGMACdevice to populate mac dma and phy addresses.
  * @param[in] Register offset for Mac address high
  * @param[in] Register offset for Mac address low
  * @param[out] buffer containing the device mac address.
  * \return 0 upon success. Error code upon failure.
  */
s32 synopGMAC_get_mac_addr(synopGMACdevice *gmacdev, u32 MacHigh, u32 MacLow, u8 *MacAddr)
{
	u32 data;
		
	data = synopGMACReadReg((u32 *)gmacdev->MacBase,MacHigh);
	MacAddr[5] = (data >> 8) & 0xff;
	MacAddr[4] = (data)        & 0xff;

	data = synopGMACReadReg((u32 *)gmacdev->MacBase,MacLow);
	MacAddr[3] = (data >> 24) & 0xff;
	MacAddr[2] = (data >> 16) & 0xff;
	MacAddr[1] = (data >> 8 ) & 0xff;
	MacAddr[0] = (data )      & 0xff;

	return 0;
}


/**
  * Attaches the synopGMAC device structure to the hardware.
  * Device structure is populated with MAC/DMA and PHY base addresses.
  * @param[in] pointer to synopGMACdevice to populate mac dma and phy addresses.
  * @param[in] GMAC IP mac base address.
  * @param[in] GMAC IP dma base address.
  * @param[in] GMAC IP phy base address.
  * \return 0 upon success. Error code upon failure.
  * \note This is important function. No kernel api provided by Synopsys 
  */

s32 synopGMAC_attach (synopGMACdevice * gmacdev, u32 macBase, u32 dmaBase, u32 phyBase,u32 devid) 
{
	//u8 mac_addr0[6] = DEFAULT_MAC_ADDRESS;
	/*Make sure the Device data strucure is cleared before we proceed further*/
	memset((void *) gmacdev,0,sizeof(synopGMACdevice));
	/*Populate the mac and dma base addresses*/
	gmacdev->MacBase = macBase;
	gmacdev->DmaBase = dmaBase;
	gmacdev->PhyBase = phyBase;
	
	/*added by Stanley on Aug 12,2008*/
	gmacdev->reg = devid;   //eth0:0 eth1:1
    gmacdev->baseAddr =  XC_SOC_PROC_MMREG_BASE; 

	/* Program/flash in the station/IP's Mac address */
	//synopGMAC_set_mac_addr(gmacdev,GmacAddr0High,GmacAddr0Low, mac_addr0); 

	return 0;	
}




/**
  * Initialize the rx descriptors for ring or chain mode operation.
  * 	- Status field is initialized to 0.
  *	- EndOfRing set for the last descriptor.
  *	- buffer1 and buffer2 set to 0 for ring mode of operation. (note)
  *	- data1 and data2 set to 0. (note)
  * @param[in] pointer to DmaDesc structure.
  * @param[in] whether end of ring
  * \return 0 upon success. Error code upon failure.
  * \note Initialization of the buffer1, buffer2, data1,data2 and status are not done here. This only initializes whether one wants to use this descriptor
  * in chain mode or ring mode. For chain mode of operation the buffer2 and data2 are programmed before calling this function.
  */
void synopGMAC_rx_desc_init_ring(DmaDesc *desc, bool last_ring_desc)
{
	//desc->status = 0;
	//desc->length = last_ring_desc ? RxDescEndOfRing : 0;
	//desc->buffer1 = 0;
	//desc->buffer2 = 0;
	//desc->data1 = 0;
	//desc->data2 = 0;

	arc_write_uncached_32(&(desc->status),0);
	arc_write_uncached_32(&(desc->length),last_ring_desc ? RxDescEndOfRing : 0);
	arc_write_uncached_32(&(desc->buffer1),0);
	arc_write_uncached_32(&(desc->buffer2),0);
	arc_write_uncached_32(&desc->data1,0);
	arc_write_uncached_32(&desc->data2,0);
	


	return;
}
/**
  * Initialize the tx descriptors for ring or chain mode operation.
  * 	- Status field is initialized to 0.
  *	- EndOfRing set for the last descriptor.
  *	- buffer1 and buffer2 set to 0 for ring mode of operation. (note)
  *	- data1 and data2 set to 0. (note)
  * @param[in] pointer to DmaDesc structure.
  * @param[in] whether end of ring
  * \return 0 upon success. Error code upon failure.
  * \note Initialization of the buffer1, buffer2, data1,data2 and status are not done here. This only initializes whether one wants to use this descriptor
  * in chain mode or ring mode. For chain mode of operation the buffer2 and data2 are programmed before calling this function.
  */
void synopGMAC_tx_desc_init_ring(DmaDesc *desc, bool last_ring_desc)
{
#if 0
	#ifdef ENH_DESC
	desc->status = last_ring_desc? TxDescEndOfRing : 0;
	desc->length = 0; 
	#else
	desc->length = last_ring_desc? TxDescEndOfRing : 0;
	#endif
	desc->buffer1 = 0;
	desc->buffer2 = 0;
	desc->data1 = 0;
	desc->data2 = 0;
#endif
	
	#ifdef ENH_DESC
	arc_write_uncached_32(&(desc->status), last_ring_desc? TxDescEndOfRing : 0);
	arc_write_uncached_32(&(desc->length),  0);
	#else
	arc_write_uncached_32(&(desc->length), last_ring_desc? TxDescEndOfRing : 0);
	#endif
	arc_write_uncached_32(&(desc->buffer1),0);
	arc_write_uncached_32(&(desc->buffer2),0);
	
	//desc->data1 = 0;
	//desc->data2 = 0;
	arc_write_uncached_32(&desc->data1,0);
	arc_write_uncached_32(&desc->data2,0);
	
	return;
}



/**
  * Initialize the rx descriptors for chain mode of operation.
  * 	- Status field is initialized to 0.
  *	- EndOfRing set for the last descriptor.
  *	- buffer1 and buffer2 set to 0.
  *	- data1 and data2 set to 0.
  * @param[in] pointer to DmaDesc structure.
  * @param[in] whether end of ring
  * \return 0 upon success. Error code upon failure.
  */

void synopGMAC_rx_desc_init_chain(DmaDesc * desc)
{
	desc->status = 0;
	desc->length = RxDescChain;
	desc->buffer1 = 0;
	desc->data1 = 0;
	return;
}
/**
  * Initialize the rx descriptors for chain mode of operation.
  * 	- Status field is initialized to 0.
  *	- EndOfRing set for the last descriptor.
  *	- buffer1 and buffer2 set to 0.
  *	- data1 and data2 set to 0.
  * @param[in] pointer to DmaDesc structure.
  * @param[in] whether end of ring
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_tx_desc_init_chain(DmaDesc * desc)
{
	#ifdef ENH_DESC
		desc->status = TxDescChain;
		desc->length = 0;
	#else
		desc->length = TxDescChain;
	#endif

	desc->buffer1 = 0;
	desc->data1 = 0;
	return;
}


s32 synopGMAC_init_tx_rx_desc_queue(synopGMACdevice *gmacdev)
{
	s32 i;
	for(i =0; i < gmacdev -> TxDescCount; i++){
	synopGMAC_tx_desc_init_ring(gmacdev->TxDesc + i, i == gmacdev->TxDescCount-1);
	}
	TR("At line %d\n",__LINE__);
	for(i =0; i < gmacdev -> RxDescCount; i++){
	synopGMAC_rx_desc_init_ring(gmacdev->RxDesc + i, i == gmacdev->RxDescCount-1);
	}
	
	gmacdev->TxNext = 0;
	gmacdev->TxBusy = 0;
	gmacdev->RxNext = 0;
	gmacdev->RxBusy = 0;
	
	return -ESYNOPGMACNOERR;
}
/**
  * Programs the DmaRxBaseAddress with the Rx descriptor base address.
  * Rx Descriptor's base address is available in the gmacdev structure. This function progrms the 
  * Dma Rx Base address with the starting address of the descriptor ring or chain.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_init_rx_desc_base(synopGMACdevice *gmacdev)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase,DmaRxBaseAddr,(u32)gmacdev->RxDescDma);
	return;
}

/**
  * Programs the DmaTxBaseAddress with the Tx descriptor base address.
  * Tx Descriptor's base address is available in the gmacdev structure. This function progrms the 
  * Dma Tx Base address with the starting address of the descriptor ring or chain.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_init_tx_desc_base(synopGMACdevice *gmacdev)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase,DmaTxBaseAddr,(u32)gmacdev->TxDescDma);
	return;
}


/** 
  * Makes the Dma as owner for this descriptor.
  * This function sets the own bit of status field of the DMA descriptor,
  * indicating the DMA is the owner for this descriptor. 
  * @param[in] pointer to DmaDesc structure.
  * \return returns void.
  */
void synopGMAC_set_owner_dma(DmaDesc *desc)
{
//desc->status |=  DescOwnByDma;
arc_write_uncached_32(&(desc->status),arc_read_uncached_32(&(desc->status))|DescOwnByDma);

}

/** 
  * set tx descriptor to indicate SOF.
  * This Descriptor contains the start of ethernet frame.
  * @param[in] pointer to DmaDesc structure.
  * \return returns void.
  */
void synopGMAC_set_desc_sof(DmaDesc *desc)
{
#ifdef ENH_DESC
//desc->status |= DescTxFirst;//ENH_DESC
arc_write_uncached_32(&(desc->status),arc_read_uncached_32(&(desc->status)) | DescTxFirst);
#else
//desc->length |= DescTxFirst;
arc_write_uncached_32(&(desc->length),arc_read_uncached_32(&(desc->length)) | DescTxFirst);
#endif

}

/** 
  * set tx descriptor to indicate EOF.
  * This descriptor contains the End of ethernet frame.
  * @param[in] pointer to DmaDesc structure.
  * \return returns void.
  */
void synopGMAC_set_desc_eof(DmaDesc *desc)
{
#ifdef ENH_DESC
//desc->status |= DescTxLast;//ENH_DESC
arc_write_uncached_32(&(desc->status),arc_read_uncached_32(&(desc->status)) | DescTxLast);
#else
//desc->length |= DescTxLast;
arc_write_uncached_32(&(desc->length),arc_read_uncached_32(&(desc->length)) | DescTxLast);

#endif
}


/** 
  * checks whether this descriptor contains start of frame.
  * This function is to check whether the descriptor's data buffer 
  * contains a fresh ethernet frame?
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if SOF in current descriptor, else returns fail.
  */
bool synopGMAC_is_sof_in_rx_desc(DmaDesc *desc)
{
//return ((desc->status & DescRxFirst) == DescRxFirst);                      
return ((arc_read_uncached_32(&(desc->status)) & DescRxFirst) == DescRxFirst);
}

/** 
  * checks whether this descriptor contains end of frame.
  * This function is to check whether the descriptor's data buffer 
  * contains end of ethernet frame?
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if SOF in current descriptor, else returns fail.
  */
bool synopGMAC_is_eof_in_rx_desc(DmaDesc *desc)
{
//return ((desc->status & DescRxLast) == DescRxLast);                      
return ((arc_read_uncached_32(&(desc->status)) & DescRxLast) == DescRxLast);
}

/** 
  * checks whether destination address filter failed in the rx frame.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if Failed, false if not.
  */
bool synopGMAC_is_da_filter_failed(DmaDesc *desc)
{
//return ((desc->status & DescDAFilterFail) == DescDAFilterFail);                      
return ((arc_read_uncached_32(&(desc->status)) & DescDAFilterFail) == DescDAFilterFail);

}

/** 
  * checks whether source address filter failed in the rx frame.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if Failed, false if not.
  */
bool synopGMAC_is_sa_filter_failed(DmaDesc *desc)
{
//return ((desc->status & DescSAFilterFail) == DescSAFilterFail);                      
return ((arc_read_uncached_32(&(desc->status)) & DescSAFilterFail) == DescSAFilterFail);
}

/** 
  * Checks whether the descriptor is owned by DMA.
  * If descriptor is owned by DMA then the OWN bit is set to 1. This API is same for both ring and chain mode.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if Dma owns descriptor and false if not.
  */
bool synopGMAC_is_desc_owned_by_dma(DmaDesc *desc)
{
//return ((desc->status & DescOwnByDma) == DescOwnByDma );

u32 status = arc_read_uncached_32(&(desc->status));
return ((status & DescOwnByDma) == DescOwnByDma );

}

/**
  * returns the byte length of received frame including CRC.
  * This returns the no of bytes received in the received ethernet frame including CRC(FCS).
  * @param[in] pointer to DmaDesc structure.
  * \return returns the length of received frame lengths in bytes.
  */
u32 synopGMAC_get_rx_desc_frame_length(u32 status)
{
	return ((status & DescFrameLengthMask) >> DescFrameLengthShift);
}

/**
  * Checks whether the descriptor is valid
  * if no errors such as CRC/Receive Error/Watchdog Timeout/Late collision/Giant Frame/Overflow/Descriptor
  * error the descritpor is said to be a valid descriptor.
  * @param[in] pointer to DmaDesc structure.
  * \return True if desc valid. false if error.
  */
bool synopGMAC_is_desc_valid(u32 status)
{
	return ((status & DescError) == 0);
}

/**
  * Checks whether the descriptor is empty.
  * If the buffer1 and buffer2 lengths are zero in ring mode descriptor is empty.
  * In chain mode buffer2 length is 0 but buffer2 itself contains the next descriptor address.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if descriptor is empty, false if not empty.
  */
bool synopGMAC_is_desc_empty(DmaDesc *desc)
{
	//if both the buffer1 length and buffer2 length are zero desc is empty
	//return(((desc->length  & DescSize1Mask) == 0) && ((desc->length  & DescSize2Mask) == 0) );
	
	//u32 length = arc_read_uncached_32(&(desc->length));
	//return(((length  & DescSize1Mask) == 0) && ((length  & DescSize2Mask) == 0) );
	
	#define DescSizeMask (DescSize1Mask | DescSize2Mask)
	return ((arc_read_uncached_32(&desc->length) & DescSizeMask ) == 0);
		
}


/**
  * Checks whether the rx descriptor is valid.
  * if rx descripor is not in error and complete frame is available in the same descriptor
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if no error and first and last desc bits are set, otherwise it returns false.
  */
bool synopGMAC_is_rx_desc_valid(u32 status)
{
	return ((status & DescError) == 0) && ((status & DescRxFirst) == DescRxFirst) && ((status & DescRxLast) == DescRxLast);
}

/**
  * Checks whether the tx is aborted due to collisions.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if collisions, else returns false.
  */
bool synopGMAC_is_tx_aborted(u32 status)
{
	return (((status & DescTxLateCollision) == DescTxLateCollision) | ((status & DescTxExcCollisions) == DescTxExcCollisions));

}

/**
  * Checks whether the tx carrier error.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if carrier error occured, else returns falser.
  */
bool synopGMAC_is_tx_carrier_error(u32 status)
{
	return (((status & DescTxLostCarrier) == DescTxLostCarrier)  | ((status & DescTxNoCarrier) == DescTxNoCarrier));
}


/**
  * Gives the transmission collision count.
  * returns the transmission collision count indicating number of collisions occured before the frame was transmitted.
  * Make sure to check excessive collision didnot happen to ensure the count is valid.
  * @param[in] pointer to DmaDesc structure.
  * \return returns the count value of collision.
  */
u32 synopGMAC_get_tx_collision_count(u32 status)
{
	return ((status & DescTxCollMask) >> DescTxCollShift);
}
u32 synopGMAC_is_exc_tx_collisions(u32 status)
{
	return ((status & DescTxExcCollisions) == DescTxExcCollisions);
}


/**
  * Check for damaged frame due to overflow or collision.
  * Retruns true if rx frame was damaged due to buffer overflow in MTL or late collision in half duplex mode.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if error else returns false.
  */
bool synopGMAC_is_rx_frame_damaged(u32 status)
{
//bool synopGMAC_dma_rx_collisions(u32 status)
	return (((status & DescRxDamaged) == DescRxDamaged) | ((status & DescRxCollision) == DescRxCollision));
}

/**
  * Check for damaged frame due to collision.
  * Retruns true if rx frame was damaged due to late collision in half duplex mode.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if error else returns false.
  */
bool synopGMAC_is_rx_frame_collision(u32 status)
{
//bool synopGMAC_dma_rx_collisions(u32 status)
	return ((status & DescRxCollision) == DescRxCollision);
}

/**
  * Check for receive CRC error.
  * Retruns true if rx frame CRC error occured.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if error else returns false.
  */
bool synopGMAC_is_rx_crc(u32 status)
{
//u32 synopGMAC_dma_rx_crc(u32 status)
	return ((status & DescRxCrc) == DescRxCrc);
}

/**
  * Indicates rx frame has non integer multiple of bytes. (odd nibbles).
  * Retruns true if dribbling error in rx frame.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if error else returns false.
  */
bool synopGMAC_is_frame_dribbling_errors(u32 status)
{
//u32 synopGMAC_dma_rx_frame_errors(u32 status)
	return ((status & DescRxDribbling) == DescRxDribbling);
}

/**
  * Indicates error in rx frame length.
  * Retruns true if received frame length doesnot match with the length field
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if error else returns false.
  */
bool synopGMAC_is_rx_frame_length_errors(u32 status)
{
//u32 synopGMAC_dma_rx_length_errors(u32 status)
	return((status & DescRxLengthError) == DescRxLengthError);
}

/**
  * Checks whether this rx descriptor is last rx descriptor.
  * This returns true if it is last descriptor either in ring mode or in chain mode.
  * @param[in] pointer to devic structure.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if it is last descriptor, false if not.
  * \note This function should not be called before initializing the descriptor using synopGMAC_desc_init().
  */
bool synopGMAC_is_last_rx_desc(synopGMACdevice * gmacdev,DmaDesc *desc)
{
//bool synopGMAC_is_last_desc(DmaDesc *desc)
	//return (((desc->length & RxDescEndOfRing) == RxDescEndOfRing) || ((u32)gmacdev->RxDesc == desc->data2));
	return (((arc_read_uncached_32(&(desc->length)) & RxDescEndOfRing) == RxDescEndOfRing) 
			|| ((u32)gmacdev->RxDesc == arc_read_uncached_32(&desc->data2)));

}

/**
  * Checks whether this tx descriptor is last tx descriptor.
  * This returns true if it is last descriptor either in ring mode or in chain mode.
  * @param[in] pointer to devic structure.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if it is last descriptor, false if not.
  * \note This function should not be called before initializing the descriptor using synopGMAC_desc_init().
  */
bool synopGMAC_is_last_tx_desc(synopGMACdevice * gmacdev,DmaDesc *desc)
{
//bool synopGMAC_is_last_desc(DmaDesc *desc)
#ifdef ENH_DESC
	//return (((desc->status & TxDescEndOfRing) == TxDescEndOfRing) || ((u32)gmacdev->TxDesc == desc->data2));
	return (((arc_read_uncached_32(&(desc->status)) & TxDescEndOfRing) == TxDescEndOfRing) 
				|| ((u32)gmacdev->TxDesc == arc_read_uncached_32(&desc->data2)));

#else
	//return (((desc->length & TxDescEndOfRing) == TxDescEndOfRing) || ((u32)gmacdev->TxDesc == desc->data2));
	return (((arc_read_uncached_32(&(desc->length)) & TxDescEndOfRing) == TxDescEndOfRing) 
			|| ((u32)gmacdev->TxDesc == arc_read_uncached_32(&desc->data2)));

#endif
}

/**
  * Checks whether this rx descriptor is in chain mode.
  * This returns true if it is this descriptor is in chain mode.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if chain mode is set, false if not.
  */
bool synopGMAC_is_rx_desc_chained(DmaDesc * desc)
{
	//return((desc->length & RxDescChain) == RxDescChain);
	return (((arc_read_uncached_32(&(desc->length)) & RxDescChain) == RxDescChain));
             
}

/**
  * Checks whether this tx descriptor is in chain mode.
  * This returns true if it is this descriptor is in chain mode.
  * @param[in] pointer to DmaDesc structure.
  * \return returns true if chain mode is set, false if not.
  */
bool synopGMAC_is_tx_desc_chained(DmaDesc * desc)
{
#ifdef ENH_DESC
	//return((desc->status & TxDescChain) == TxDescChain);             
	return((arc_read_uncached_32(&(desc->status)) & TxDescChain) == TxDescChain);             

#else
	//return((desc->length & TxDescChain) == TxDescChain);             
	return((arc_read_uncached_32(&(desc->length)) & TxDescChain) == TxDescChain);             
#endif
}

/**
  * Driver Api to get the descriptor field information.
  * This returns the status, dma-able address of buffer1, the length of buffer1, virtual address of buffer1
  * dma-able address of buffer2, length of buffer2, virtural adddress of buffer2.
  * @param[in]  pointer to DmaDesc structure.
  * @param[out] pointer to status field fo descriptor.
  * @param[out] dma-able address of buffer1.
  * @param[out] length of buffer1.
  * @param[out] virtual address of buffer1.
  * @param[out] dma-able address of buffer2.
  * @param[out] length of buffer2.
  * @param[out] virtual address of buffer2.
  * \return returns void.
  */
void synopGMAC_get_desc_data(DmaDesc * desc, u32 * Status, u32 * Buffer1, u32 * Length1, u32 * Data1, u32 * Buffer2, u32 * Length2, u32 * Data2)
{

	if(Status != 0)   
		//*Status = desc->status;
		*Status = arc_read_uncached_32(&(desc->status));

	if(Buffer1 != 0)
		//*Buffer1 = desc->buffer1;
		*Buffer1 = arc_read_uncached_32(&(desc->buffer1));
	if(Length1 != 0)
		//*Length1 = (desc->length & DescSize1Mask) >> DescSize1Shift;
		*Length1 = (arc_read_uncached_32(&(desc->length))& DescSize1Mask) >> DescSize1Shift;
	if(Data1 != 0)
		//*Data1 = desc->data1;
		*Data1 = arc_read_uncached_32(&desc->data1);

	if(Buffer2 != 0)
		//*Buffer2 = desc->buffer2;
		*Buffer2 = arc_read_uncached_32(&(desc->buffer2));
	if(Length2 != 0)
		//*Length2 = (desc->length & DescSize2Mask) >> DescSize2Shift;
		*Length2 = (arc_read_uncached_32(&(desc->length))& DescSize2Mask) >> DescSize2Shift;
	if(Data1 != 0)
		//*Data2 = desc->data2;
		*Data2 = arc_read_uncached_32(&desc->data2);
	return;

}





/**
  * Clears all the pending interrupts.
  * If the Dma status register is read then all the interrupts gets cleared
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_clear_interrupt(synopGMACdevice *gmacdev)
{
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaStatus);
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaStatus ,data);
}

/**
  * Returns the all unmasked interrupt status after reading the DmaStatus register.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
u32 synopGMAC_get_interrupt_type(synopGMACdevice *gmacdev)
{
	u32 data;
	u32 interrupts = 0;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaStatus);
	TR("DMA status reg is  %08x\n",data);
	if(data & DmaIntErrorMask)	interrupts |= synopGMACDmaError;
	if(data & DmaIntRxNormMask)	interrupts |= synopGMACDmaRxNormal;
	if(data & DmaIntRxAbnMask)	interrupts |= synopGMACDmaRxAbnormal;
	if(data & DmaIntRxStoppedMask)	interrupts |= synopGMACDmaRxStopped;
	if(data & DmaIntTxNormMask)	interrupts |= synopGMACDmaTxNormal;
	if(data & DmaIntTxAbnMask)	interrupts |= synopGMACDmaTxAbnormal;
	if(data & DmaIntTxStoppedMask)	interrupts |= synopGMACDmaTxStopped;

	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaStatus ,data); //Clear all The Interrupts
	return interrupts;
}

/**
  * Returns the interrupt mask.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
u32 synopGMAC_get_interrupt_mask(synopGMACdevice *gmacdev)
{
	return(synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaInterrupt));
}

/**
  * Enable all the interrupts.
  * Enables the DMA interrupt as specified by the bit mask.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] bit mask of interrupts to be enabled.
  * \return returns void.
  */
void synopGMAC_enable_interrupt(synopGMACdevice *gmacdev, u32 interrupts)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaInterrupt, interrupts);
	return;
}


/**
  * Disable all the interrupts.
  * Disables all DMA interrupts.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  * \note This function disabled all the interrupts, if you want to disable a particular interrupt then
  *  use synopGMAC_disable_interrupt().
  */
void synopGMAC_disable_interrupt_all(synopGMACdevice *gmacdev)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaInterrupt, DmaIntDisable);
	return;
}

/**
  * Disable interrupt according to the bitfield supplied.
  * Disables only those interrupts specified in the bit mask in second argument.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] bit mask for interrupts to be disabled.
  * \return returns void.
  */
void synopGMAC_disable_interrupt(synopGMACdevice *gmacdev, u32 interrupts)
{
	synopGMACClearBits((u32 *)gmacdev->DmaBase, DmaInterrupt, interrupts);
	return;
}
/**
  * Enable the DMA Reception.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_enable_dma_rx(synopGMACdevice * gmacdev)
{
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
  	data |= DmaRxStart; 
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl ,data);
}

/**
  * Enable the DMA Transmission.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_enable_dma_tx(synopGMACdevice * gmacdev)
{
//	synopGMACSetBits((u32 *)gmacdev->DmaBase, DmaControl, DmaTxStart);
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
  	data |= DmaTxStart; 
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl ,data);

}

/**
  * Resumes the DMA Transmission.
  * the DmaTxPollDemand is written. (the data writeen could be anything).
  * This forces the DMA to resume transmission.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_resume_dma_tx(synopGMACdevice * gmacdev)
{
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaTxPollDemand, 0);

}
/**
  * Resumes the DMA Reception.
  * the DmaRxPollDemand is written. (the data writeen could be anything).
  * This forces the DMA to resume reception.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_resume_dma_rx(synopGMACdevice * gmacdev)
{
	u32 data;
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaRxPollDemand, 0);

	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
  	data |= DmaRxStart; 
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl ,data);
}
/**
  * Take ownership of this Descriptor.
  * The function is same for both the ring mode and the chain mode DMA structures.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_take_desc_ownership(DmaDesc * desc)
{
	if(desc){
		//desc->status &= ~DescOwnByDma;  //Clear the DMA own bit
		//desc->status |= DescError;	// Set the error to indicate this descriptor is bad
		arc_write_uncached_32(&(desc->status),(arc_read_uncached_32(&(desc->status)) & (~DescOwnByDma)) | DescError);
	}
}

/**
  * Take ownership of all the rx Descriptors.
  * This function is called when there is fatal error in DMA transmission.
  * When called it takes the ownership of all the rx descriptor in rx descriptor pool/queue from DMA.
  * The function is same for both the ring mode and the chain mode DMA structures.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  * \note Make sure to disable the transmission before calling this function, otherwise may result in racing situation.
  */
void synopGMAC_take_desc_ownership_rx(synopGMACdevice * gmacdev)
{
	s32 i;
	DmaDesc *desc;
	desc = gmacdev->RxDesc;
	for(i = 0; i < gmacdev->RxDescCount; i++){
		if(synopGMAC_is_rx_desc_chained(desc)){	//This descriptor is in chain mode
	
			synopGMAC_take_desc_ownership(desc);
			//desc = (DmaDesc *)desc->data2;
			desc = (DmaDesc *)arc_read_uncached_32(&desc->data2);
		}
		else{
			synopGMAC_take_desc_ownership(desc + i);
		}
	}
}

/**
  * Take ownership of all the rx Descriptors.
  * This function is called when there is fatal error in DMA transmission.
  * When called it takes the ownership of all the tx descriptor in tx descriptor pool/queue from DMA.
  * The function is same for both the ring mode and the chain mode DMA structures.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  * \note Make sure to disable the transmission before calling this function, otherwise may result in racing situation.
  */
void synopGMAC_take_desc_ownership_tx(synopGMACdevice * gmacdev)
{
	s32 i;
	DmaDesc *desc;
	desc = gmacdev->TxDesc;
	for(i = 0; i < gmacdev->TxDescCount; i++){
		if(synopGMAC_is_tx_desc_chained(desc)){	//This descriptor is in chain mode
			synopGMAC_take_desc_ownership(desc);
			//desc = (DmaDesc *)desc->data2;
			desc = (DmaDesc *)arc_read_uncached_32(&desc->data2);
		}
		else{
			synopGMAC_take_desc_ownership(desc + i);
		}
	}
	
}

/**
  * Disable the DMA for Transmission.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */

void synopGMAC_disable_dma_tx(synopGMACdevice * gmacdev)
{	
//	synopGMACClearBits((u32 *)gmacdev->DmaBase, DmaControl, DmaTxStart);
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
  	data &= (~DmaTxStart); 
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl ,data);
}
/**
  * Disable the DMA for Reception.
  * @param[in] pointer to synopGMACdevice.
  * \return 0 upon success. Error code upon failure.
  */
void synopGMAC_disable_dma_rx(synopGMACdevice * gmacdev)
{	
//	synopGMACClearBits((u32 *)gmacdev->DmaBase, DmaControl, DmaRxStart);
	u32 data;
	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
  	data &= (~DmaRxStart); 
	synopGMACWriteReg((u32 *)gmacdev->DmaBase, DmaControl ,data);
}


	
/*******************PMT APIs***************************************/




/**
  * Enables the assertion of PMT interrupt.
  * This enables the assertion of PMT interrupt due to Magic Pkt or Wakeup frame
  * reception.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_pmt_int_enable(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacPmtIntMask); 
        return;
}
/**
  * Disables the assertion of PMT interrupt.
  * This disables the assertion of PMT interrupt due to Magic Pkt or Wakeup frame
  * reception.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_pmt_int_disable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacPmtIntMask); 
        return;
}
/**
  * Enables the assertion of PMT interrupt.
  * This enables the assertion of PMT interrupt due to Magic Pkt or Wakeup frame
  * reception.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_rgmii_int_enable(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacRgmiiIntMask); 
        return;
}
/**
  * Disables the assertion of PMT interrupt.
  * This disables the assertion of PMT interrupt due to Magic Pkt or Wakeup frame
  * reception.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_rgmii_int_disable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacRgmiiIntMask); 
        return;
}
/**
  * Enables the power down mode of GMAC.
  * This function puts the Gmac in power down mode.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_power_down_enable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtPowerDown);	
	return;
}
/**
  * Disables the powerd down setting of GMAC.
  * If the driver wants to bring up the GMAC from powerdown mode, even though the magic packet or the
  * wake up frames received from the network, this function should be called.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_power_down_disable(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtPowerDown);	
	return;
}
/**
  * Enables the pmt interrupt generation in powerdown mode.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_enable_pmt_interrupt(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacPmtIntMask);	
}
/**
  * Disables the pmt interrupt generation in powerdown mode.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_disable_pmt_interrupt(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacPmtIntMask);	
}
/**
  * Enables the rgmii interrupt generation in powerdown mode.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_enable_rgmii_interrupt(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacRgmiiIntMask);	
}
/**
  * Disables the RGMII interrupt generation in powerdown mode.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_disable_rgmii_interrupt(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacInterruptMask,GmacRgmiiIntMask);	
}
/**
  * Enables GMAC to look for Magic packet.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_magic_packet_enable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtMagicPktEnable);	
	return;
}

/**
  * Enables GMAC to look for wake up frame. 
  * Wake up frame is defined by the user.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_wakeup_frame_enable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtWakeupFrameEnable);	
	return;
}

/**
  * Enables wake-up frame filter to handle unicast packets.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_pmt_unicast_enable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtGlobalUnicast);	
	return;
}
/**
  * Checks whether the packet received is a magic packet?.
  * @param[in] pointer to synopGMACdevice.
  * \return returns True if magic packet received else returns false.
  */
bool synopGMAC_is_magic_packet_received(synopGMACdevice *gmacdev)
{
	u32 data;
	data = 	synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus);	
	return((data & GmacPmtMagicPktReceived) == GmacPmtMagicPktReceived);
}
/**
  * Checks whether the packet received is a wakeup frame?.
  * @param[in] pointer to synopGMACdevice.
  * \return returns true if wakeup frame received else returns false.
  */
bool synopGMAC_is_wakeup_frame_received(synopGMACdevice *gmacdev)
{
	u32 data;
	data = 	synopGMACReadReg((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus);	
	return((data & GmacPmtWakeupFrameReceived) == GmacPmtWakeupFrameReceived);
}

/**
  * Populates the remote wakeup frame registers.
  * Consecutive 8 writes to GmacWakeupAddr writes the wakeup frame filter registers.
  * Before commensing a new write, frame filter pointer is reset to 0x0000.
  * A small delay is introduced to allow frame filter pointer reset operation.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] pointer to frame filter contents array.
  * \return returns void.
  */
void synopGMAC_write_wakeup_frame_register(synopGMACdevice *gmacdev, u32 * filter_contents)
{
	s32 i;
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacPmtCtrlStatus,GmacPmtFrmFilterPtrReset);
	plat_delay(10);	
	for(i =0; i<WAKEUP_REG_LENGTH; i++)
		synopGMACWriteReg((u32 *)gmacdev->MacBase, GmacWakeupAddr,  *(filter_contents + i));
	return;

}
/*******************PMT APIs***************************************/
/*******************MMC APIs***************************************/

/**
  * Freezes the MMC counters.
  * This function call freezes the MMC counters. None of the MMC counters are updated
  * due to any tx or rx frames until synopGMAC_mmc_counters_resume is called.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_stop(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterFreeze);
	return;
}
/**
  * Resumes the MMC counter updation.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_resume(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterFreeze);
	return;
}
/**
  * Configures the MMC in Self clearing mode.
  * Programs MMC interface so that counters are cleared when the counters are read.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_set_selfclear(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterResetOnRead);
	return;
}
/**
  * Configures the MMC in non-Self clearing mode.
  * Programs MMC interface so that counters are cleared when the counters are read.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_reset_selfclear(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterResetOnRead);
	return;
}
/**
  * Configures the MMC to stop rollover.
  * Programs MMC interface so that counters will not rollover after reaching maximum value.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_disable_rollover(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterStopRollover);
	return;
}
/**
  * Configures the MMC to rollover.
  * Programs MMC interface so that counters will rollover after reaching maximum value.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_mmc_counters_enable_rollover(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcCntrl,GmacMmcCounterStopRollover);
	return;
}

/**
  * Read the MMC Counter.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] the counter to be read.
  * \return returns the read count value.
  */
u32 synopGMAC_read_mmc_counter(synopGMACdevice *gmacdev, u32 counter)
{
	return(	synopGMACReadReg((u32 *)gmacdev->MacBase,counter));
}
/**
  * Read the MMC Rx interrupt status.
  * @param[in] pointer to synopGMACdevice.
  * \return returns the Rx interrupt status.
  */
u32 synopGMAC_read_mmc_rx_int_status(synopGMACdevice *gmacdev)
{
	return(	synopGMACReadReg((u32 *)gmacdev->MacBase,GmacMmcIntrRx));
}
/**
  * Read the MMC Tx interrupt status.
  * @param[in] pointer to synopGMACdevice.
  * \return returns the Tx interrupt status.
  */
u32 synopGMAC_read_mmc_tx_int_status(synopGMACdevice *gmacdev)
{
	return(	synopGMACReadReg((u32 *)gmacdev->MacBase,GmacMmcIntrTx));
}
/**
  * Disable the MMC Tx interrupt.
  * The MMC tx interrupts are masked out as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] tx interrupt bit mask for which interrupts needs to be disabled.
  * \return returns void.
  */
void synopGMAC_disable_mmc_tx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcIntrMaskTx,mask);
	return;
}
/**
  * Enable the MMC Tx interrupt.
  * The MMC tx interrupts are enabled as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] tx interrupt bit mask for which interrupts needs to be enabled.
  * \return returns void.
  */
void synopGMAC_enable_mmc_tx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcIntrMaskTx,mask);
}
/**
  * Disable the MMC Rx interrupt.
  * The MMC rx interrupts are masked out as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] rx interrupt bit mask for which interrupts needs to be disabled.
  * \return returns void.
  */
void synopGMAC_disable_mmc_rx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcIntrMaskRx,mask);
	return;
}
/**
  * Enable the MMC Rx interrupt.
  * The MMC rx interrupts are enabled as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] rx interrupt bit mask for which interrupts needs to be enabled.
  * \return returns void.
  */
void synopGMAC_enable_mmc_rx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcIntrMaskRx,mask);
	return;
}
/**
  * Disable the MMC ipc rx checksum offload interrupt.
  * The MMC ipc rx checksum offload interrupts are masked out as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] rx interrupt bit mask for which interrupts needs to be disabled.
  * \return returns void.
  */
void synopGMAC_disable_mmc_ipc_rx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacMmcRxIpcIntrMask,mask);
	return;
}
/**
  * Enable the MMC ipc rx checksum offload interrupt.
  * The MMC ipc rx checksum offload interrupts are enabled as per the mask specified.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] rx interrupt bit mask for which interrupts needs to be enabled.
  * \return returns void.
  */
void synopGMAC_enable_mmc_ipc_rx_interrupt(synopGMACdevice *gmacdev, u32 mask)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacMmcRxIpcIntrMask,mask);
	return;
}
/*******************MMC APIs***************************************/
/*******************Ip checksum offloading APIs***************************************/

/**
  * Enables the ip checksum offloading in receive path.
  * When set GMAC calculates 16 bit 1's complement of all received ethernet frame payload.
  * It also checks IPv4 Header checksum is correct. GMAC core appends the 16 bit checksum calculated
  * for payload of IP datagram and appends it to Ethernet frame transferred to the application.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_enable_rx_chksum_offload(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->MacBase,GmacConfig,GmacRxIpcOffload);
	return;
}
/**
  * Disable the ip checksum offloading in receive path.
  * Ip checksum offloading is disabled in the receive path.
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_disable_rx_Ipchecksum_offload(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->MacBase,GmacConfig,GmacRxIpcOffload);
}
/**
  * Instruct the DMA to drop the packets fails tcp ip checksum.
  * This is to instruct the receive DMA engine to drop the recevied packet if they 
  * fails the tcp/ip checksum in hardware. Valid only when full checksum offloading is enabled(type-2).
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_rx_tcpip_chksum_drop_enable(synopGMACdevice *gmacdev)
{
	synopGMACClearBits((u32 *)gmacdev->DmaBase,DmaControl,DmaDisableDropTcpCs);
	return;
}
/**
  * Instruct the DMA not to drop the packets even if it fails tcp ip checksum.
  * This is to instruct the receive DMA engine to allow the packets even if recevied packet
  * fails the tcp/ip checksum in hardware. Valid only when full checksum offloading is enabled(type-2).
  * @param[in] pointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_rx_tcpip_chksum_drop_disable(synopGMACdevice *gmacdev)
{
	synopGMACSetBits((u32 *)gmacdev->DmaBase,DmaControl,DmaDisableDropTcpCs);
	return;
}

/**
  * Decodes the Rx Descriptor status to various checksum error conditions.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] u32 status field of the corresponding descriptor.
  * \return returns decoded enum (u32) indicating the status.
  */

u32 synopGMAC_is_rx_checksum_error(synopGMACdevice *gmacdev, u32 status)
{
	if     (((status & DescRxChkBit5) == 0) && ((status & DescRxChkBit7) == 0) && ((status & DescRxChkBit0) == 0))
	return RxLenLT600;
	else if(((status & DescRxChkBit5) == 0) && ((status & DescRxChkBit7) == 0) && ((status & DescRxChkBit0) != 0))
	return RxIpHdrPayLoadChkBypass;
	else if(((status & DescRxChkBit5) == 0) && ((status & DescRxChkBit7) != 0) && ((status & DescRxChkBit0) != 0))
	return RxChkBypass;
	else if(((status & DescRxChkBit5) != 0) && ((status & DescRxChkBit7) == 0) && ((status & DescRxChkBit0) == 0))
	return RxNoChkError;
	else if(((status & DescRxChkBit5) != 0) && ((status & DescRxChkBit7) == 0) && ((status & DescRxChkBit0) != 0))
	return RxPayLoadChkError;
	else if(((status & DescRxChkBit5) != 0) && ((status & DescRxChkBit7) != 0) && ((status & DescRxChkBit0) == 0))
	return RxIpHdrChkError;
	else if(((status & DescRxChkBit5) != 0) && ((status & DescRxChkBit7) != 0) && ((status & DescRxChkBit0) != 0))
	return RxIpHdrPayLoadChkError;
	else
	return RxIpHdrPayLoadRes;
}

// Below two functions are for 3.30a support
bool synopGMAC_is_rx_ipv4header_checksum_error(synopGMACdevice *gmacdev, u32 status)
{
	return ((status & DescRxIpv4ChkError) == DescRxIpv4ChkError);
}
bool synopGMAC_is_rx_payload_checksum_error(synopGMACdevice *gmacdev, u32 status)
{
	return ((status & DescRxPayChkError) == DescRxPayChkError);
}


/**
  * Checks if any Ipv4 header checksum error in the frame just transmitted.
  * This serves as indication that error occureed in the IPv4 header checksum insertion.
  * The sent out frame doesnot carry any ipv4 header checksum inserted by the hardware.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] u32 status field of the corresponding descriptor.
  * \return returns true if error in ipv4 header checksum, else returns false.
  */
bool synopGMAC_is_tx_ipv4header_checksum_error(synopGMACdevice *gmacdev, u32 status)
{
	return((status & DescTxIpv4ChkError) == DescTxIpv4ChkError);
}


/**
  * Checks if any payload checksum error in the frame just transmitted.
  * This serves as indication that error occureed in the payload checksum insertion.
  * The sent out frame doesnot carry any payload checksum inserted by the hardware.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] u32 status field of the corresponding descriptor.
  * \return returns true if error in ipv4 header checksum, else returns false.
  */
bool synopGMAC_is_tx_payload_checksum_error(synopGMACdevice *gmacdev, u32 status)
{
	return((status & DescTxPayChkError) == DescTxPayChkError);
}
/**
  * The check summ offload engine is bypassed in the tx path.
  * Checksum is not computed in the Hardware.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] Pointer to tx descriptor for which  ointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_tx_checksum_offload_bypass(synopGMACdevice *gmacdev, DmaDesc *desc)
{
	#ifdef ENH_DESC
	//desc->status = (desc->length & (~DescTxCisMask));//ENH_DESC
	arc_write_uncached_32(&(desc->status),(arc_read_uncached_32(&(desc->status)) & (~DescTxCisMask)));
	#else
	//desc->length = (desc->length & (~DescTxCisMask));
	arc_write_uncached_32(&(desc->length),(arc_read_uncached_32(&(desc->length)) & (~DescTxCisMask)));
	#endif

}
/**
  * The check summ offload engine is enabled to do only IPV4 header checksum.
  * IPV4 header Checksum is computed in the Hardware.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] Pointer to tx descriptor for which  ointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_tx_checksum_offload_ipv4hdr(synopGMACdevice *gmacdev, DmaDesc *desc)
{
	#ifdef ENH_DESC
	//desc->status = ((desc->status & (~DescTxCisMask)) | DescTxCisIpv4HdrCs);//ENH_DESC
	arc_write_uncached_32(&(desc->status),(arc_read_uncached_32(&(desc->status)) & (~DescTxCisMask)) | DescTxCisIpv4HdrCs);
	#else
	//desc->length = ((desc->length & (~DescTxCisMask)) | DescTxCisIpv4HdrCs);
	arc_write_uncached_32(&(desc->length),(arc_read_uncached_32(&(desc->length)) & (~DescTxCisMask)) | DescTxCisIpv4HdrCs);
	#endif

}

/**
  * The check summ offload engine is enabled to do TCPIP checsum assuming Pseudo header is available.
  * Hardware computes the tcp ip checksum assuming pseudo header checksum is computed in software.
  * Ipv4 header checksum is also inserted.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] Pointer to tx descriptor for which  ointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_tx_checksum_offload_tcponly(synopGMACdevice *gmacdev, DmaDesc *desc)
{
	#ifdef ENH_DESC
	//desc->status = ((desc->status & (~DescTxCisMask)) | DescTxCisTcpOnlyCs);//ENH_DESC
	arc_write_uncached_32(&(desc->status),(arc_read_uncached_32(&(desc->status)) & (~DescTxCisMask)) | DescTxCisTcpOnlyCs);
	#else
	//desc->length = ((desc->length & (~DescTxCisMask)) | DescTxCisTcpOnlyCs);
	arc_write_uncached_32(&(desc->length),(arc_read_uncached_32(&(desc->length)) & (~DescTxCisMask)) | DescTxCisTcpOnlyCs);

	#endif

}
/**
  * The check summ offload engine is enabled to do complete checksum computation.
  * Hardware computes the tcp ip checksum including the pseudo header checksum.
  * Here the tcp payload checksum field should be set to 0000.
  * Ipv4 header checksum is also inserted.
  * @param[in] pointer to synopGMACdevice.
  * @param[in] Pointer to tx descriptor for which  ointer to synopGMACdevice.
  * \return returns void.
  */
void synopGMAC_tx_checksum_offload_tcp_pseudo(synopGMACdevice *gmacdev, DmaDesc *desc)
{
	#ifdef ENH_DESC
	//desc->status = ((desc->length & (~DescTxCisMask)) | DescTxCisTcpPseudoCs);
	arc_write_uncached_32(&(desc->status),(arc_read_uncached_32(&(desc->status)) & (~DescTxCisMask)) | DescTxCisTcpPseudoCs);
	#else
	//desc->length = ((desc->length & (~DescTxCisMask)) | DescTxCisTcpPseudoCs);
	arc_write_uncached_32(&(desc->length),(arc_read_uncached_32(&(desc->length)) & (~DescTxCisMask)) | DescTxCisTcpPseudoCs);
	#endif

}

/* optimized*/
s32 xc3_get_tx_qptr(synopGMACdevice * gmacdev, u32 * Status, u32 * Buffer1, u32 * Length1, u32 * Data1 )
{
	DmaDesc * txdesc;
	unsigned long               lockflags;
	int status;
	int length;
		
	spin_lock_irqsave(&gmacdev->tx_desc_lock, lockflags);
	
	txdesc = (DmaDesc *)NON_CACHE_ADDR(gmacdev->TxBusyDesc);

	status  = txdesc->status;
	length = txdesc->length;

	//If desc is owned by DMA engine or it is empty, return error
	if(((status & DescOwnByDma) == DescOwnByDma) || ((length & (DescSize1Mask | DescSize2Mask))==0))
	{
		spin_unlock_irqrestore(&gmacdev->tx_desc_lock, lockflags);
		return -1;
	}


	*Status = status;
	*Length1= (length & DescSize1Mask) >> DescSize1Shift;
	*Buffer1 = txdesc->buffer1;// send the status of this descriptor
	*Data1 = txdesc->data1;

	TR("xc3_get_tx_qptr, buf=0x%x, status=0x%08x\n", *Buffer1, *Status = status);
	gmacdev->TxBusyDesc = (DmaDesc *)txdesc->data2;
	synopGMAC_tx_desc_init_chain(txdesc);

	gmacdev->FreeSendDesc += 1;

	spin_unlock_irqrestore(&gmacdev->tx_desc_lock, lockflags);
	return 1;	
}

s32 xc3_set_tx_qptr(synopGMACdevice * gmacdev, u32 Buffer1, u32 Length1, u32 Data1,u32 offload_needed,u32 offload_option,u32 first, u32 last)
{
	DmaDesc * txdesc;
	unsigned long lockflags;
	u32 status;
	u32 length;
	u32 flag = 0;

	spin_lock_irqsave(&gmacdev->tx_desc_lock, lockflags);

	TR("tx 0x%x, size=%x, offload_needed=%d offload=0x%x\n", Buffer1, Length1, offload_needed, offload_option);

	txdesc = (DmaDesc *)NON_CACHE_ADDR(gmacdev->TxNextDesc);

	length = txdesc->length;
	if((length & (DescSize1Mask | DescSize2Mask))!= 0) //if not empty
	{
		spin_unlock_irqrestore(&gmacdev->tx_desc_lock, lockflags);
		return -1;
	}
	
	status = txdesc->status;
	length |= ((Length1 <<DescSize1Shift) & DescSize1Mask);
	
	#ifdef TR_INTR_ENABLE
	if (last)
    	flag |= DescTxIntEnable;
    #endif

    if (first) 
        flag |= DescTxFirst;
    if (last)
        flag |= DescTxLast;

	#ifdef ENH_DESC
	status |= flag;
	#else
    length |= flag;
	#endif

	txdesc->buffer1 = Buffer1;
	txdesc->data1 = Data1;
	txdesc->length = length;
	
	if(offload_needed)
	{
		status = (status & (~DescTxCisMask)) | offload_option;
	}

	#ifdef ENH_DESC
	status |= DescOwnByDma; 
	#else
	status = DescOwnByDma;
	#endif
	
	txdesc->status = status;

	gmacdev->FreeSendDesc -= 1;
	gmacdev->TxNextDesc = (DmaDesc *)txdesc->data2;

	spin_unlock_irqrestore(&gmacdev->tx_desc_lock, lockflags);


	return 1;	
}

s32 xc3_get_rx_qptr(synopGMACdevice * gmacdev, u32 * Status, u32 * Buffer1, u32 * Length1, u32 * Data1)
{
	
	DmaDesc * rxdesc = (DmaDesc *)NON_CACHE_ADDR(gmacdev->RxBusyDesc);
	u32 status  = rxdesc->status;
	u32 length = rxdesc->length;

	//If desc is owned by DMA engine or it is empty, return error
	if((status & DescOwnByDma) == DescOwnByDma) 
	{
		return -1;
	}


	*Status = status;
	
	*Length1 = (length & DescSize1Mask) >> DescSize1Shift;
	
	*Buffer1 = rxdesc->buffer1;
	*Data1 = rxdesc->data1;

	gmacdev->RxBusyDesc = (DmaDesc *)rxdesc->data2;

	synopGMAC_rx_desc_init_chain(rxdesc);

	#ifdef SKB_OVERWRITTEN_DEBUG
	{
		struct sk_buff *skb = (struct sk_buff *)*Data1;
		u32 temp;

		temp = rxdesc->pade1;
		if (temp != skb_shinfo(skb)->dataref.counter)
			printk("FAILED, skb overritten, expected %x, act %x\n", skb_shinfo(skb)->dataref.counter, temp);
		temp = rxdesc->pade2;
		if (temp != skb_shinfo(skb)->nr_frags)
			printk("FAILED, skb overritten, expected %x, act %x\n", skb_shinfo(skb)->nr_frags, temp);
	}
	#endif

	return 1;

}

s32 synopGMAC_set_rx_qptr(synopGMACdevice * gmacdev, u32 Buffer1, u32 Length1, u32 Data1, u32 Buffer2, u32 Length2, u32 Data2)
{
	DmaDesc * rxdesc;

	u32 status;

	
	if(!gmacdev)
	{
		printk("##############################################################################\n");
		return -1;
	}

	rxdesc  = (DmaDesc *)NON_CACHE_ADDR(gmacdev->RxNextDesc);

	if(!rxdesc)
	{
		printk("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		return -1;
	}
	status  = rxdesc->status;
#ifdef SKB_OVERWRITTEN_DEBUG
	struct sk_buff *skb = (struct sk_buff *)Data1;
#endif

	//If desc is owned by DMA engine or it is empty, return error
	if((status & DescOwnByDma) == DescOwnByDma) 
	{
		return -1;
	}
	

	//Only support chain mode
	rxdesc->length |= ((Length1 <<DescSize1Shift) & DescSize1Mask) | RxDescChain;
	rxdesc->buffer1 = Buffer1;
	rxdesc->data1 = Data1;

	smp_mb();
	
	rxdesc->status = DescOwnByDma;
	
	gmacdev->RxNextDesc = (DmaDesc *)rxdesc->data2;

	#ifdef SKB_OVERWRITTEN_DEBUG
	rxdesc->pade1 = skb_shinfo(skb)->dataref.counter;
	rxdesc->pade2 = (u32)skb_shinfo(skb)->nr_frags;
	#endif

	
	TR("Set Rx descriptor %08x %08x %08x %08x %08x %08x %08x\n",(u32)rxdesc,
					arc_read_uncached_32(&(rxdesc->status)),
					arc_read_uncached_32(&(rxdesc->length)),
					arc_read_uncached_32(&(rxdesc->buffer1)),
					arc_read_uncached_32(&(rxdesc->buffer2)),
					arc_read_uncached_32(&rxdesc->data1),
					arc_read_uncached_32(&rxdesc->data2));
	
	
	return 0;
}




