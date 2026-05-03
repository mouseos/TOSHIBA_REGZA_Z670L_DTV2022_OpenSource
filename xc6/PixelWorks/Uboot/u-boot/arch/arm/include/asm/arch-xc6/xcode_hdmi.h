/* Copyright 2012 ViXS Systems Inc. Proprietary and Confidential */

#ifndef _capri_hdmi_h
#define _capri_hdmi_h

#define HDMI_MC_PHYRSTZ 0x04005
    #define HDMI_PHYSICAL_RESET 01

#define HDMI_FC_INVIDCONF 0x1000
    #define IN_I_P_MASK 0x01
    #define IN_I_P_SHIFT 0
        #define HDMI_INTERLACE_INPUT 1
        #define HDMI_PROGRESSIVE_INPUT 0
    #define R_V_BLANK_IN_OSC_MASK 0x02
    #define R_V_BLANK_IN_OSC_SHIFT 1
        #define MODE_WITH_FRACTIONAL_VBLANK 1
        #define MODE_WITHOUT_FRACTIONAL_VBLANK 0
    #define DVI_MODEZ_MASK 0x08
    #define DVI_MODEZ_SHIFT 3
        #define NONE_DVI_MODE 1
        #define DVI_MODE 0
    #define DE_IN_POLARITY_MASK 0x10
    #define DE_IN_POLARITY_SHIFT 4
        #define DATA_ENABLE_INPUT 1
        #define NO_DATA_ENABLE_INPUT 0
    #define HSYNC_IN_POLARITY_MASK 0x20
    #define HSYNC_IN_POLARITY_SHIFT 5
    #define VSYNC_IN_POLARITY_MASK 0x40
    #define VSYNC_IN_POLARITY_SHIFT 6
    #define HDCP_KEEPOUT_MASK 0x80
    #define HDCP_KEEPOUT_SHIFT 7

#define HDMI_FC_INHACTIV0 0x1001
    #define FC_INHACTIV0_MASK 0x0ff

#define HDMI_FC_INHACTIV1 0x1002
    #define FC_INHACTIV1_MASK 0x3f00
    #define FC_INHACTIV1_SHIFT 8 

#define HDMI_FC_INHBLANK0 0x1003
    #define FC_INIHBLANK0_MASK 0x0ff

#define HDMI_FC_INHBLANK1 0x1004
    #define FC_INIHBLANK1_MASK 0x1f00
    #define FC_INIHBLANK1_SHIFT 8

#define HDMI_FC_INVACTIV0 0x1005
    #define FC_INVACTIV0_MASK 0x0ff

/* 
*    The mask below differs for A21 and A11,
*    the shift count is the same
*/
#define HDMI_FC_INVACTIV1 0x1006
    #define FC_INVACTIV1_MASK 0x0700
    #define FC_INVACTIV1_MASK_A21 0x1F00
    #define FC_INVACTIV1_SHIFT 8

#define HDMI_FC_INVBLANK 0x1007

#define HDMI_FC_HSYNCINDELAY0 0x1008
    #define FC_HSYNCINDELAY0_MASK 0x0ff;

#define HDMI_FC_HSYNCINDELAY1 0x1009
    #define FC_HSYNCINDELAY1_MASK 0x1f00
    #define FC_HSYNCINDELAY1_SHIFT 8

#define HDMI_FC_HSYNCINWIDTH0 0x100a
    #define FC_HSYNCINWIDTH0_MASK 0x0ff

#define HDMI_FC_HSYNCINWIDTH1 0x100b
    #define FC_HSYNCINWIDTH1_MASK 0x1f00
    #define FC_HSYNCINWIDTH1_SHIFT 8

#define HDMI_FC_VSYNCINDELAY 0x100c

#define HDMI_FC_VSYNCINWIDTH 0x100d
    #define FC_VSYNCINWIDTH_MASK 0x3f

#define HDMI_FC_INFREQ0 0x100e
    #define FC_INFREQ0_MASK 0x0ff

#define HDMI_FC_INFREQ1 0x100f
    #define FC_INFREQ1_MASK 0x0ff00
    #define FC_INFREQ1_SHIFT 8

#define HDMI_FC_INFREQ2 0x1010
    #define FC_INFREQ2_MASK 0x0f0000
    #define FC_INFREQ2_SHIFT 16

#define HDMI_FC_CTRLDUR 0x1011
    #define  HDMI_CONTROL_PERIOD_MINIMUM_DURATION 12

#define HDMI_FC_EXCTRLDUR 0x1012
    #define HDMI_EXTENDED_CONTROL_PERIOD_MINIMUM_DURATION 32

#define HDMI_FC_EXCTRLSPAC 0x1013
    #define HDMI_EXT_CTRL_PERIOD_SPACE 1

#define HDMI_FC_CH0PREAM 0x1014
    #define HDMI_CH0_PREAMBLE_FILTER 0x0b

#define HDMI_FC_CH1PREAM 0x1015
    #define HDMI_CH1_PREAMBLE_FILTER 0x16

#define HDMI_FC_CH2PREAM 0x1016
    #define HDMI_CH2_PREAMBLE_FILTER 0x21

#define HDMI_FC_GCP 0x1018

#define HDMI_FC_AVICONF0 0x1019
    #define AVICONF_YCBCR_444 0x02
    #define AVICONF_YCBCR_422 0x01
    #define AVICONF_RGB       0x00    
    #define AVICONF_YCBCR_420 0x03

#define HDMI_FC_AVICONF1 0x101a
    #define COLORIMETRY_SHIFT 6
        #define SMPTE_170M              0x01
        #define COLORIMETRY_ITU601      0x01
        #define COLORIMETRY_ITU709      0x02
        #define COLORIMETRY_XVYCC       0x03
        
    #define PICTURE_ASPECT_RATIO_SHIFT 4
        #define PICTURE_ASPECT_RATIO_4X3 0x01
        #define PICTURE_ASPECT_RATIO_16X9 0x02
    #define ACTIVE_ASPECT_RATIO_SHIFT 0
        #define SAME_AS_PICTURE_ASPECT_RATIO 0x8
        #define ACTIVE_ASPECT_RATIO_4X3 0x09
        #define ACTIVE_ASPECT_RATIO_16X9 0x0a

#define HDMI_FC_AVICONF2 0x101b
    #define EXTENDED_COLORIMETRY_SHIFT  4
        #define COLORIMETRY_XVYCC601    0
        #define COLORIMETRY_XVYCC709    1
        #define COLORIMETRY_SYCC601     2
        #define COLORIMETRY_ADOBEYCC601 3
        #define COLORIMETRY_ADOBERGB    4
        #define COLORIMETRY_BT2020C     5
        #define COLORIMETRY_BT2020      6
    #define QUANTIZATION_RANGE_SHIFT 2
        #define QUANTIZATION_RANGE_DEFAULT 0
        #define QUANTIZATION_RANGE_LIMITED 1
        #define QUANTIZATION_RANGE_FULL 2

#define HDMI_FC_AVICONF3 0x1017
    #define QUANTIZATION_RANGE_YCC_SHIFT 2
        #define QUANTIZATION_RANGE_YCC_LIMITED 0
        #define QUANTIZATION_RANGE_YCC_FULL 1
    #define IT_CONTENT_TYPE_SHIFT 0
        #define IT_CONTENT_TYPE_GRAPHIC 0
        #define IT_CONTENT_TYPE_PHOTO 1
        #define IT_CONTENT_TYPE_CINEMA 2
        #define IT_CONTENT_TYPE_GAME 3

#define HDMI_FC_AVIVID 0x101c

#define HDMI_FC_AVIETB0 0x101d

#define HDMI_FC_AVIETB1 0x101e

#define HDMI_FC_AVISBB0 0x101f

#define HDMI_FC_AVISBB1 0x1020

#define HDMI_FC_AVIELB0 0x1021

#define HDMI_FC_AVIELB1 0x1022

#define HDMI_FC_AVISRB0 0x1023

#define HDMI_FC_AVISRB1 0x1024

#define HDMI_FC_VSDIEEEID0 0x1029

#define HDMI_FC_VSDIEEEID1 0x1030

#define HDMI_FC_VSDIEEEID2 0x1031

#define HDMI_FC_VSDSIZE 0x102a

#define HDMI_TX_INVID0 0x200
    #define YCBCR_444_8     9
    #define YCBCR_444_10   11
    #define YCBCR_444_12   13    

#define HDMI_MC_FLOWCTRL 0x4004
    #define COLOR_CONVERT_BYPASS 0
    #define COLOR_CONVERT_ENABLE 1

#define HDMI_VP_PR_CD 0x801
    #define DEPTH_24_BIT_8_PER_COMPONENT 0x4
    #define DEPTH_30_BIT_10_PER_COMPONENT 0x5
    #define DEPTH_36_BIT_12_PER_COMPONENT 0x6
    #define COLOR_DEPTH_SHIFT 4
    #define NO_PIXEL_REPETITION 0
    #define PIXEL_REPEAT_ONCE   1
    #define PIXEL_REPEAT_TWICE     2
    #define PIXEL_REPEAT_THREE  3

#define HDMI_FC_PRCONF 0x10e0
    #define INPUT_NO_REPEAT 0x01
    #define INPUT_REPEAT_ONCE 0x02
    #define INCOMING_PR_FACTOR_SHIFT 4
    #define OUTPUT_NO_REPEAT  0x00
    #define OUTPUT_REPEAT_ONCE  0x01
    #define OUTPUT_REPEAT_TWICE 0x02
    #define OUTPUT_REPEAT_THREE 0x03

#define HDMI_FC_SCRAMBLER 0x10e1
    #define SCRAMBLE_ON_MASK 0x01

#define HDMI_VP_STUFF 0x0802
    #define IDEFAULT_PHASE_MASK 0x20
    #define DEFAULT_VP_STUFF 0
    #define VP_STUFF_DEEP_COLOR 0x20
    /* bit5 needs to be set to 0 if htotal for mode is 
    *  not divisible by 4
    */
    #define VP_STUFF_DEEP_COLOR_NON_DIV_BY_4 0x00

#define HDMI_VP_REMAP 0x0803
    #define YCC422_16BIT 0    //8 bits per component
    #define YCC422_20BIT 1    //10 bits per component
    #define YCC422_24BIT 2    //12 bits per component    
#define HDMI_VP_CONF 0x0804
        
        /* 
        *    Data select is in the lowest 2-bits of this register and there is no  
        *    need for a mask or a shift count    
        */
        #define DATA_FROM_8_BIT_BYPASS_BLOCK  0x02    
        #define    DATA_FROM_YCC422_REMAP_BLOCK  0x01        
        #define DATA_FROM_PIXEL_PACKING_BLOCK 0x00
    #define BYPASS_SELECT_SHIFT 2
        #define DATA_FROM_PIXEL_REPEATER 0
        #define DATA_FROM_VIDEO_PACKETIZER 1
    #define YUV_422_ENABLE_SHIFT 3
        #define YUV_422_ENABLE 1
    #define PIXEL_PACKET_ENABLE_SHIFT 5
        #define PIXEL_PACKET_ENABLE 1
    #define BYPASS_ENABLE_SHIFT 6
        #define BYPASS_ENABLE 1
    

#define HDMI_FC_SPDDEVICEINF 0x1062
    #define BLU_RAY_DISC 0x0a

#define HDMI_FC_DATAUTO0 0x10b3
    #define SPD_AUTO 0x10
    #define VSD_AUTO 0x08

#define HDMI_FC_DATAUTO1 0x10b4

#define HDMI_FC_DATAUTO2 0x10b5

#define HDMI_FC_DATAUTO3 0x10b7

#define HDMI_FC_RDRB0 0x10b8

#define HDMI_FC_RDRB1 0x10b9

#define HDMI_FC_RDRB2 0x10ba

#define HDMI_FC_RDRB3 0x10bb

#define HDMI_FC_RDRB4 0x10bc

#define HDMI_FC_RDRB5 0x10bd

#define HDMI_FC_RDRB6 0x10be

#define HDMI_FC_RDRB7 0x10bf

#define HDMI_FC_GMD_STAT    0x1100

#define HDMI_FC_GMD_EN 0x1101
    #define GMD_ENABLE_PACKET_MASK  0x1
    
#define HDMI_FC_GMD_UP 0x1102
    #define GMD_UPDATE_PACKET_MASK  0x1
    
#define HDMI_FC_GMD_CONF 0x1103
    #define HDMI_FC_GMD_CONF_DEFAULT    0x10

#define HDMI_FC_GMD_HB   0x1104
    #define GMD_HB_PROFILE_MASK     0x7
    #define GMD_HB_PROFILE_SHIFT    0x4
    
    #define GMD_HB_PROFILE_P0       0x0
    #define GMD_HB_PROFILE_P1       0x1
    #define GMD_HB_PROFILE_P2       0x2
    #define GMD_HB_PROFILE_P3       0x3
    
    #define GMD_HB_AFFECTED_MASK    0xF
    #define GMD_HB_AFFECTED_SHIFT   0x0

/* 
*   Define the base address of the packet body
*   All bytes are in contiguous sequence...
*/
#define HDMI_FC_GMD_PB0  0x1105

#define HDMI_CSC_CFG 0x4100

#define HDMI_CSC_SCALE 0x4101
    #define COLOR_SPACE_CONV_COLOR_DEPTH_SHIFT 4
      #define COLOR_DEPTH_24_BIT_PER_PIXEL 4
      #define COLOR_DEPTH_30_BIT_PER_PIXEL 5
      #define COLOR_DEPTH_36_BIT_PER_PIXEL 6

#define HDMI_CSC_COEFA1_MSB 0x4102

#define HDMI_CSC_COEFB2_MSB 0x410c

#define HDMI_CSC_COEFC3_MSB 0x4116

#define HDMI_CSC_COEFC4_LSB 0x4119

#define HDMI_TX_INSTUFFING 0x201

#define HDMI_TX_BCBDATA1 0x207

#define HDMI_VSD_PACKET_ID 0x81
    #define HDMI_VSD_VERSION_ID 0x01


#define HDMI_FC_VSDPAYLOAD0 0x1032
    #define STRUCTURE_3D 0x40
    #define STRUCTURE_4K 0x20
#define HDMI_FC_VSDPAYLOAD1 0x1033
    #define FRAME_PACKING_3D 0x0
    #define SIDE_BY_SIDE_3D 0x8
    #define TOP_AND_BOTTOM_3D 0x6
    #define MODE_MASK_3D 0x0f0
    #define MODE_SHIFT_3D 4

#define HDMI_FC_VSDPAYLOAD23 0x1049
    #define LENGTH_OF_VENDOR_SPECIFIC 24

#define HDMI_FC_SPDVENDORNAME0 0x104a

#define HDMI_FC_SPDVENDORNAME7 0x1051
    #define LENGTH_OF_SPD_VENDOR_NAME 8

#define HDMI_FC_SPDPRODUCTNAME0 0x1052

#define HDMI_FC_SPDPRODUCTNAME15 0x1061
    #define LENGTH_OF_SPD_PRODUCT_NAME 16

#define HDMI_FC_CTRLQHIGH 0x1073

#define HDMI_FC_CTRLQLOW 0x1074

#define HDMI_FC_ACP0 0x1075

#define HDMI_FC_ISCR1_0 0x1092
    #define ISRC_CONT 0x01
    #define ISRC_VALID 0x02

#define HDMI_FC_ISCR1_1 0x1093

#define HDMI_FC_ISCR1_16 0x10a2
    #define LENGTH_OF_ISCR1 16

#define HDMI_FC_ISCR2_0 0x10a3

#define HDMI_FC_ISCR2_15 0x10b2
    #define LENGTH_OF_ISCR2 16

#define FC_GMD_PB0 0x1105

#define FC_GMD_PB27 0x1120
    #define LENGTH_OF_GMD 28

#define HDMI_FC_DATMAN 0x10b6 
    #define SPD_TX 0x10
    #define VSD_TX 0x80
    #define ISCR2_TX 0x40
    #define ISCR1_TX 0x20

#define HDMI_MC_SFRDIV 0x4000

#define HDMI_PHY_CONF0 0x3000
    #define PHY_PWRDOWN_EN        0x10

#define HDMI_PHY_TST0 0x3001

#define HDMI_PHY_TST1 0x3002

#define HDMI_PHY_STAT0 0x3004
    #define HDMI_TX_PHY_LOCK_MASK  0x1
    #define HDMI_HPD_STATUS_LOCK   0x2
    
#define HDMI_PHY_MASK0 0x3006
    #define HDMI_HPD_MASK 0x02

#define HDMI_PHY_POL0  0x3007
    #define    HPD_POL_MASK  0x02
    
#define HDMI_MC_CLKGAT 0x4001
    #define CLKGATE_HDCP_MASK    0x40
    #define CLKGATE_CEC_MASK     0x20
    #define CLKGATE_CSC_MASK     0x10
    #define CLKGATE_AUD_MASK     0x08
    #define CLKGATE_PREP_MASK    0x04
    #define CLKGATE_TMDS_MASK    0x02
    #define CLKGATE_PCLK_MASK    0x01

/* Base address in HDMI for CSC matrix */    
#define HDMI_CSC_COEF_BASE 0x4102    

#define HDMI_A_HDCPCFG0 0x5000
    #define HDMI_DVI_MASK 0x01
    #define ENABLE_11_FEATURE 0x02
    #define RX_DETECT_MASK 0x04
    #define AV_MUTE_MASK 0x08
    #define SYNC_CHECK_MASK 0x10
    #define BYPASSES_DATA_ENCRYPTION_MASK 0x20
    #define ENHANCED_LINK_VERIFICATION_MASK 0x80
    
#define    HDMI_A_HDCPPOLCFG             0x5009
    #define VSYNC_POS_MASK            0x8
    #define HSYNC_POS_MASK            0x2
    #define DE_POS_MASK                0x10
    #define UNENCRYPT_COLOR_MASK    0x60
    #define UNENCRYPT_COLOR_SHIFT   5
    #define UNENCRYPT_COLOR_BLUE    0x2
    #define UNENCRYPT_COLOR_BLACK   0x0

#define HDMI_A_TIMER2SETUP0 0x500d
#define HDMI_A_TIMER2SETUP1 0x500e

#define HDMI_A_PRESETUP 0x501a
    #define PRE_CLK_ENABLE_MASK 0x80
    #define PRE_CLK_DIV_FULL_HD 4
    #define PRE_CLK_DIV_HD 2
    #define PRE_CLK_DIV_SD 1

#define     HDMI_CEC_STAT                   0x0106
#define         CEC_STAT_DONE_MASK          0x0001
#define         CEC_STAT_EOM_MASK           0x0002
#define         CEC_STAT_NACK               0x0004
#define         CEC_STAT_ARB_LOST           0x0008
#define         CEC_STAT_ERROR_INIT         0x0010
#define         CEC_STAT_ERROR_FOLLOW       0x0020
#define         CEC_STAT_WAKEUP             0x0040

#define     HDMI_CEC_TX_DATA_BASE                0x7D10
#define     HDMI_CEC_TX_LEN                      0x7D07

#define     HDMI_CEC_RX_DATA_BASE                0x7D20
#define     HDMI_CEC_RX_LEN                      0x7D08


#define     HDMI_CEC_CTRL                        0x7D00
#define         CEC_CTRL_SEND_MASK               0x01
#define         CEC_CTRL_FRAME_TYPE_3BIT         0x00
#define         CEC_CTRL_FRAME_TYPE_5BIT         0x01
#define         CEC_CTRL_FRAME_TYPE_7BIT         0x02
#define         CEC_CTRL_FRAME_TYPE_SHIFT           1
#define         CEC_CTRL_BCACK_ACK                  0x0
#define         CEC_CTRL_BCACK_NACK                 0x1
#define         CEC_CTRL_BCACK_SHIFT                3
#define         CEC_CTRL_STANDBY_ACK                0x0
#define         CEC_CTRL_STANDBY_NACK               0x1
#define         CEC_CTRL_STANDBY_SHIFT              4  

#define     HDMI_CEC_LOCK                        0x7D30
#define         CEC_LOCK_BUFFER_MASK             0x01

#endif
