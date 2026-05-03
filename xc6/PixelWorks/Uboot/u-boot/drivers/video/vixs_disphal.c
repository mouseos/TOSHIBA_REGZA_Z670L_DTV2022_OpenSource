#include "vixs_dispdef.h"
#include "videomodes.h"
#include <asm/arch-xc6/xcodeRegDef.h>
#include <malloc.h>
#include <video_fb.h>
#include <bmp_layout.h>


#define DISABLE_RANDOM 2
#define PEL_POSITION_ALPHA 0x03
#define PEL_POSITION_RED   0x02
#define PEL_POSITION_GREEN 0x01
#define PEL_POSITION_BLUE  0x00

#define DISP_FORMAT_MONO 0
#define DISP_FORMAT_CLUT2 1
#define DISP_FORMAT_CLUT4 2
#define DISP_FORMAT_CLUT8 3
#define DISP_FORMAT_RGB1555 4
#define DISP_FORMAT_RGB565 5
#define DISP_FORMAT_RGB24 6
#define DISP_FORMAT_ARGB 7

#define HDMI_REG_WRITE(addr,data) hdmi_reg_write(addr,data)
#define HDMI_REG_READ(addr) hdmi_reg_read(addr)

#define HDMI_REG_MASK_SET(reg,mask,val) hdmi_reg_write(reg, (hdmi_reg_read(reg) &~ (mask)) | (val))
#define HDMI_REG_MASK_GET(reg,mask) (hdmi_reg_read(reg) & (mask))

#define VIDEO_VISIBLE_COLS	(ctfb.winSizeX)
#define VIDEO_VISIBLE_ROWS	(ctfb.winSizeY)
#define VIDEO_PIXEL_SIZE	(ctfb.gdfBytesPP)
#define VIDEO_DATA_FORMAT	(ctfb.gdfIndex)
#define VIDEO_FB_ADRS		(ctfb.frameAdrs)

#define VIDEO_COLS		VIDEO_VISIBLE_COLS
#define VIDEO_ROWS		VIDEO_VISIBLE_ROWS
#define VIDEO_SIZE		(VIDEO_ROWS*VIDEO_COLS*VIDEO_PIXEL_SIZE)

#define FILL_32BIT_X888RGB(r,g,b) {			\
	*(unsigned long *)fb =				\
		SWAP32((unsigned long)(((0xff<<24) | \
		            (r<<16) |	\
					(g<<8)  |	\
					 b)));		\
	fb += 4;					\
}

#ifdef	VIDEO_FB_LITTLE_ENDIAN
#define SWAP16(x)		((((x) & 0x00ff) << 8) | \
				  ((x) >> 8) \
				)
#define SWAP32(x)		((((x) & 0x000000ff) << 24) | \
				 (((x) & 0x0000ff00) <<  8) | \
				 (((x) & 0x00ff0000) >>  8) | \
				 (((x) & 0xff000000) >> 24)   \
				)
#define SHORTSWAP32(x)		((((x) & 0x000000ff) <<  8) | \
				 (((x) & 0x0000ff00) >>  8) | \
				 (((x) & 0x00ff0000) <<  8) | \
				 (((x) & 0xff000000) >>  8)   \
				)
#else
#define SWAP16(x)		(x)
#define SWAP32(x)		(x)
#endif

u32 mmio_base = XC_SOC_PROC_MMREG_BASE;

/* Locals */
static GraphicDevice ctfb;	/* Graphic array */

void hdmi_reg_write( u32 addr, u32 data )
{
    /* make sure block is idle before changing registers */
    REG_POLL( DISP_HDMI_STATUS, (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK),
                                (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK) );

    REG_WRITE(DISP_HDMI_WRDATA,(data)&DISP_HDMI_WRDATA_WRDATA_MASK);
    
    REG_WRITE(DISP_HDMI_ADDR, (DISP_HDMI_ADDR_WOP_MASK |((addr)&DISP_HDMI_ADDR_ADDR_MASK)));

    REG_POLL( DISP_HDMI_STATUS, (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK),
                                (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK) );
}

u32 hdmi_reg_read( u32 addr )
{
    u32 value;

    /* make sure block is idle before changing registers */
    REG_POLL( DISP_HDMI_STATUS, (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK),
                                (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK) );

    REG_WRITE(DISP_HDMI_ADDR,(addr)&DISP_HDMI_ADDR_ADDR_MASK);
    
    REG_POLL( DISP_HDMI_STATUS, (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK),
                                (DISP_HDMI_STATUS_BUSY_MASK | DISP_HDMI_STATUS_UPRAM_BUSY_MASK) );
    
    value = REG_READ(DISP_HDMI_RDDATA)&RDDATA_MASK;

    return value;
}

static void _enable_disp_clk(int enable)
{
    u32 tmp;
    
    DISP_LOCK_CG;

    if(enable)
    {
        tmp = REG_READ(CG_CLK_STOP0);
        MEM_SET_FLD(tmp, CG_CLK_STOP0, VSCLK_STOP, 0);
        REG_WRITE(CG_CLK_STOP0, tmp);
        tmp = REG_READ(CG1_CLK_STOP0);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, VSDCLK_STOP, 0);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, VHDCLK_STOP, 0);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, PCLK_STOP, 0);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, DISP_656CLK_STOP, 0);
        REG_WRITE(CG1_CLK_STOP0, tmp);

        tmp = REG_READ(ACC_BLK_STOP1);
        MEM_SET_FLD(tmp, ACC_BLK_STOP1, DISP_BLK_STOP, 0);
        REG_WRITE(ACC_BLK_STOP1, tmp);

        tmp =   REG_READ(ACC_RESET_REG1);
        MEM_SET_FLD(tmp, ACC_RESET_REG1, DISP_RESET, 0);
        REG_WRITE(ACC_RESET_REG1, tmp);    
    }
    else
    {       
        tmp = REG_READ(CG_CLK_STOP0);
        MEM_SET_FLD(tmp, CG_CLK_STOP0, VSCLK_STOP, 1);
        REG_WRITE(CG_CLK_STOP0, tmp);
        tmp = REG_READ(CG1_CLK_STOP0);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, VSDCLK_STOP, 1);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, VHDCLK_STOP, 1);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, PCLK_STOP, 1);
        MEM_SET_FLD(tmp, CG1_CLK_STOP0, DISP_656CLK_STOP, 1);
        REG_WRITE(CG1_CLK_STOP0, tmp);

        tmp = REG_READ(ACC_BLK_STOP1);
        MEM_SET_FLD(tmp, ACC_BLK_STOP1, DISP_BLK_STOP, 1);
        REG_WRITE(ACC_BLK_STOP1, tmp);

        tmp =   REG_READ(ACC_RESET_REG1);
        MEM_SET_FLD(tmp, ACC_RESET_REG1, DISP_RESET, 1);
        REG_WRITE(ACC_RESET_REG1, tmp); 
    }
        
    DISP_UNLOCK_CG;
}

#define HD_CG1_PLL1_FREQ_NO_FRAC                0x22F851EB
#define HD_CG1_PLL1_FREQ_WITH_FRAC              0x22F78F79
#define HD_CG1_PLL1_FREQ_WITH_FRAC_PLUS         0x22F91490
#define VGA_CG1_PLL1_FREQ_25_2MHZ               0x22041894
#define VGA_CG1_PLL1_FREQ_25_175MHZ             0x22039496
#define HD_CG1_PLL1_FREQ_59_4MHZ                0x22604188
#define HD_CG1_PLL1_FREQ_59_34MHZ               0x225FA5FA

static s_xcfb_clock_param clock_param_table[] =
{
	{
		.clock = XCFB_SCR_PIX_CLK_594MHz,
		.pll1 = HD_CG1_PLL1_FREQ_NO_FRAC,
		.hdpll_sel = 0,      // 1/1 from pll1
		.vhd_sel   = 0,      // 1/1 from hdpll
		.dac0_sel  = 1,      // 1/2 from hdpll
		.gen90_den = 6600000,
		.gen90_num = 1000,
		.gen27_den = 22000,
		.gen27_num = 1000,
		.mod_num   = 8192,
		.mod_den   = 99000,
	},

	{
		.clock = XCFB_SCR_PIX_CLK_148_5MHz, //148.5MHz
		.pll1 = HD_CG1_PLL1_FREQ_NO_FRAC,
		.hdpll_sel = 0, 	 // 1/1 from pll1
		.vhd_sel   = 2, 	 // 1/4 from hdpll
		.dac0_sel  = 1, 	 // 1/2 from hdpll
		.gen90_den = 1650000,
		.gen90_num = 1000,
		.gen27_den = 5500,
		.gen27_num = 1000,
		.mod_num   = 8192,
		.mod_den   = 99000,
	},

	{
		.clock = XCFB_SCR_PIX_CLK_74_25MHz, //74.25MHz
		.pll1 = HD_CG1_PLL1_FREQ_NO_FRAC,
		.hdpll_sel = 0, 	 // 1/1 from pll1
		.vhd_sel   = 3, 	 // 1/8 from hdpll
		.dac0_sel  = 1, 	 // 1/2 from hdpll
		.gen90_den = 825000,
		.gen90_num = 1000,
		.gen27_den = 2750,
		.gen27_num = 1000,
		.mod_num   = 8192,
		.mod_den   = 99000,
	},

	{
		.clock = XCFB_SCR_PIX_CLK_27_027MHz, //27.027MHz
		.pll1 = HD_CG1_PLL1_FREQ_WITH_FRAC_PLUS,
		.hdpll_sel = 1, 	 // 1/2 from pll1
		.vhd_sel   = 6, 	 // 1/11 from hdpll
		.dac0_sel  = 2, 	 // 1/1 from sdpll
		.gen90_den = 300300,
		.gen90_num = 1000,
		.gen27_den = 1001,
		.gen27_num = 1000,
		.mod_num   = 8192,
		.mod_den   = 99099,
	},
};

int _get_clock_param(e_xcfb_screen_pixel_clock clock, ps_xcfb_clock_param param)
{
	int rc = XCFB_OK;
	int i;

	u32 pixel_clock = (clock & XCFB_SCR_PIX_CLK_CUSTOMIZED) ? (clock &~ XCFB_SCR_PIX_CLK_CUSTOMIZED) : 0;
	if (clock &  XCFB_SCR_PIX_CLK_CUSTOMIZED) {
		printf("%s: do not support customized clock\n", __func__);
	} else {
		rc = XCFB_ERROR_NOT_EXIST;

		for(i = 0; i < sizeof(clock_param_table) / sizeof(clock_param_table[0]); i++)
		{
			if(clock_param_table[i].clock == clock)
			{
				*param = clock_param_table[i];
				rc = XCFB_OK;
				break;
			}
		}
	}
	return rc;
}

static void _hd_set_clock(e_xcfb_screen_pixel_clock clock)
{
	int tmp;
	s_xcfb_clock_param param;

	if (_get_clock_param(clock, &param) == XCFB_OK) {
		//setup clock source 0
		tmp = REG_READ(CG1_CLK_SRC_SEL0);
		MEM_SET_FLD(tmp, CG1_CLK_SRC_SEL0, DISP_HDPLLCLK_SRC_SEL, param.hdpll_sel);//HD PLL1
		MEM_SET_FLD(tmp, CG1_CLK_SRC_SEL0, VHDCLK_SRC_SEL, param.vhd_sel);
		REG_WRITE(CG1_CLK_SRC_SEL0, tmp);

		//setup PLL1
		REG_WRITE(CG1_PLL1_FREQ, param.pll1);
		REG_WRITE(CG1_PLL1_CTRL, 0x41002f3);

		//write PLL1 to dummy
		REG_WRITE(DISP_DUMMY_REG_3, param.pll1);

		//setup STC genlock
		REG_WRITE(DISP_HD_GENLOCK_NUM, param.gen90_num);
		REG_WRITE(DISP_HD_GENLOCK_DEN, param.gen90_den);

		//setup PCR genlock
		tmp = 0;
		MEM_SET_FLD(tmp, DISP_HD_GENLOCK_PCR, NUM, param.gen27_num);
		MEM_SET_FLD(tmp, DISP_HD_GENLOCK_PCR, DEN, param.gen27_den);
		REG_WRITE(DISP_HD_GENLOCK_PCR, tmp);

		//setup MOD for audio
		REG_WRITE(CG1_MODDIV1_CTRL, 0x3);
		REG_WRITE(CG1_MODDIV1_NUM, param.mod_num);
		REG_WRITE(CG1_MODDIV1_DEN, param.mod_den);
		REG_WRITE(CG1_MODDIV1_CTRL, 0x2);
		REG_WRITE(CG1_MODDIV1_NUM, param.mod_num);
	} else {
		printf("%s: failed to set up lock\n", __func__);	
	}
}

static int _hd_set_dve(p_xcfb_screen_timing timing)
{
    u32     tmp;
    u32     field1_vblank_end;
    u32     field2_vblank_end;
    u32     field1_vblank_length;
    u32     field2_vblank_length;
    u32     hsync_start;
    u32     hsync_end;
    u32     field1_vsync_start;
    u32     field1_vsync_end;
    u32     field2_vsync_start;
    u32     field2_vsync_end;
    u32     f2offlow;
    u32     f2offhigh;
    u32     f2off;
    
    /* The start of second frame / field */
    f2off       =    (timing->h_total / 2) - (timing->h_total - timing->h_active) - 8;
    if (timing->f2off != 0)
    {
        f2off   =    timing->f2off; /* tune for correct values */
    }
    f2offlow    =    f2off & 0xFF;
    f2offhigh   =    (f2off >> 8) & 0x1F;

    /* Interlaced mode according to field length (might be different than format, for 3D interlaced case) */
    if (timing->field_length)
    {
        if (timing->field1_vsync_start > 1)
        {
            field1_vsync_start    =    timing->field1_vsync_start - 1;
        }
        else
        {
            field1_vsync_start    =    timing->v_total;
        }

        field2_vsync_start    =    timing->field2_vsync_start    -    1;

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_1, field1_vsync_start);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_2, field2_vsync_start);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, F2OFFLOW, f2offlow);
        REG_WRITE(DISP_HD_VENC0_DVE_VSYNC_START, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VSYNC_START, tmp);

        field1_vsync_end    =    timing->field1_vsync_start + timing->vsync_pulse_width - 1;
        field2_vsync_end    =    timing->field2_vsync_start + timing->vsync_pulse_width - 1;

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, FRAME_1, field1_vsync_end);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, FRAME_2, field2_vsync_end);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, F2OFFHIGH, f2offhigh);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, VSYNC_POL, timing->pol_vsync);
        REG_WRITE(DISP_HD_VENC0_DVE_VSYNC_END, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VSYNC_END, tmp);

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, DVE_VBLANK_START_BNDRY_1, timing->field1_vblank_start - 1);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, DVE_VBLANK_START_BNDRY_2, timing->field2_vblank_start - 1);
        REG_WRITE(DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VBLANK_START_BNDRY, tmp);

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_FIELD_BNDRY, DVE_FIELD_BNDRY_1, timing->field1_vblank_start - 1);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_FIELD_BNDRY, DVE_FIELD_BNDRY_2, timing->field2_vblank_start - 1);
        REG_WRITE(DISP_HD_VENC0_DVE_FIELD_BNDRY, tmp);

        //FIXME: workaround
        if (timing->v_active == 1080 )
        {
            REG_WRITE(DISP_HD_VENC0_6562_FIELD_BNDRY, 0x2330465);   /* 0x2330465 */
        }
        else if (timing->v_active == 480 )
        {
            REG_WRITE(DISP_HD_VENC0_6562_FIELD_BNDRY, 0x1090003);
        }
        else if (timing->v_active == 576 )
        {
            REG_WRITE(DISP_HD_VENC0_6562_FIELD_BNDRY, 0x1380271);   /* 0x1380271 */
        }
        else
        {
            REG_WRITE(DISP_HD_VENC0_6562_FIELD_BNDRY, tmp);
        }

        field1_vblank_length    =    (timing->v_total - timing->v_active) / 2;
        field2_vblank_length    =    field1_vblank_length + 1;

        field1_vblank_end       =    (timing->field1_vblank_start + field1_vblank_length) % timing->v_total - 1;
        field2_vblank_end       =    (timing->field2_vblank_start + field2_vblank_length - 1);

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, DVE_VBLANK_END_BNDRY_1, field1_vblank_end);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, DVE_VBLANK_END_BNDRY_2, field2_vblank_end);
        REG_WRITE(DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VBLANK_END_BNDRY, tmp);
    }
    else
    {
        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_FIELD_BNDRY, DVE_FIELD_BNDRY_1, timing->field1_vsync_start);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_FIELD_BNDRY, DVE_FIELD_BNDRY_2, DISP_HD_VENC0_DVE_FIELD_BNDRY_DVE_FIELD_BNDRY_1_MASK);
        REG_WRITE(DISP_HD_VENC0_DVE_FIELD_BNDRY, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_FIELD_BNDRY, tmp);

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, DVE_VBLANK_START_BNDRY_2, DISP_HD_VENC0_DVE_VBLANK_START_BNDRY_DVE_VBLANK_START_BNDRY_1_MASK);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, DVE_VBLANK_START_BNDRY_1, timing->field1_vblank_start - 1);
        REG_WRITE(DISP_HD_VENC0_DVE_VBLANK_START_BNDRY, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VBLANK_START_BNDRY, tmp);

        tmp    =    0;
        field1_vblank_length    =    timing->v_total    -    timing->v_active;
        field1_vblank_end       =    (timing->field1_vblank_start + field1_vblank_length) % timing->v_total - 1;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, DVE_VBLANK_END_BNDRY_1, field1_vblank_end);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, DVE_VBLANK_END_BNDRY_2, DISP_HD_VENC0_DVE_VBLANK_END_BNDRY_DVE_VBLANK_END_BNDRY_1_MASK);
        REG_WRITE(DISP_HD_VENC0_DVE_VBLANK_END_BNDRY, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VBLANK_END_BNDRY, tmp);

        tmp    =    0;
        if (timing->field1_vsync_start > 1)
        {
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_2, DISP_HD_VENC0_DVE_VSYNC_START_FRAME_1_MASK);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, F2OFFLOW, f2offlow);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_1, timing->field1_vsync_start - 1);
        }
        else
        {
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_2, DISP_HD_VENC0_DVE_VSYNC_START_FRAME_1_MASK);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, F2OFFLOW, f2offlow);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_START, FRAME_1, timing->v_total);
        }
        REG_WRITE(DISP_HD_VENC0_DVE_VSYNC_START, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VSYNC_START, tmp);

        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, FRAME_2, DISP_HD_VENC0_DVE_VSYNC_END_FRAME_1_MASK);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, F2OFFHIGH, f2offhigh);
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, FRAME_1, (timing->field1_vsync_start + timing->vsync_pulse_width - 1));
        MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_VSYNC_END, VSYNC_POL, timing->pol_vsync);
        REG_WRITE(DISP_HD_VENC0_DVE_VSYNC_END, tmp);
        REG_WRITE(DISP_HD_VENC0_6562_VSYNC_END, tmp);
    }
    
    /* Handle the setup of the horizontal sync generators ... */
    hsync_start  =    timing->hsync_edge_delay + 4;
    hsync_end    =    hsync_start    + timing->hsync_pulse_width;
    
    tmp    =    0;
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_HSYNC, END, hsync_end);
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_HSYNC, START, hsync_start);
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_HSYNC, HSYNC_POL, timing->pol_hsync);
    REG_WRITE(DISP_HD_VENC0_DVE_HSYNC, tmp);
    REG_WRITE(DISP_HD_VENC0_6562_HSYNC, tmp);
    
    return XCFB_OK;
}

static int _set_occ(u32 reg_addr, e_xcfb_screen_colorimetry dst)
{
    u32 matrix[6];
    
    XCFB_DBG_ENTER_PAR(dst);

    switch(dst)
    {
        case XCFB_SCR_COL_RGB:
        case XCFB_SCR_COL_RGB_2020:
        case XCFB_SCR_COL_RGB_ADOBE:
            matrix[0] = 0x20000000;
            matrix[1] = 0x00000000;
            matrix[2] = 0x00000000;
            matrix[3] = 0x00002000;
            matrix[4] = 0x00002000;
            matrix[5] = 0x00000000;
            break;
        case XCFB_SCR_COL_YUV_601:
        case XCFB_SCR_COL_YUV_601_XVYCC:
        case XCFB_SCR_COL_YUV_SYCC601:
        case XCFB_SCR_COL_YUV_709:
        case XCFB_SCR_COL_YUV_709_XVYCC:
            matrix[0] = 0x16E306CE;
            matrix[1] = 0x0200024F;
            matrix[2] = 0xF363FC40;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF123105E;
            matrix[5] = 0x1000FE80;
            break;
        case XCFB_SCR_COL_YUV_2020:
        case XCFB_SCR_COL_YUV_2020C:
            matrix[0] = 0x15B20868;
            matrix[1] = 0x020001E6;
            matrix[2] = 0xF434FB6E;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF0F3105E;
            matrix[5] = 0x1000FEAF;
            break;
        case XCFB_SCR_COL_YUV_ADOBE:
            matrix[0] = 0x14130984;
            matrix[1] = 0x02000269;
            matrix[2] = 0xF4E6FABD;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF163105E;
            matrix[5] = 0x1000FE3F;
            break;
        case XCFB_SCR_COL_YUV_470M:
            matrix[0] = 0x12C40991;
            matrix[1] = 0x020003AB;
            matrix[2] = 0xF529FA79;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF24F105E;
            matrix[5] = 0x1000FD53;
            break;
        case XCFB_SCR_COL_YUV_SMPT_240M:
            matrix[0] = 0x166F06CC;
            matrix[1] = 0x020002C5;
            matrix[2] = 0xF371FC32;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF16F105E;
            matrix[5] = 0x1000FE34;
            break;
        case XCFB_SCR_COL_YUV_GENERAL_FILM:
            matrix[0] = 0x15B5081D;
            matrix[1] = 0x0200022E;
            matrix[2] = 0xF416FB8C;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF121105E;
            matrix[5] = 0x1000FE82;
            break;
        default:
            matrix[0] = 0x16E306CE;
            matrix[1] = 0x0200024F;
            matrix[2] = 0xF363FC40;
            matrix[3] = 0x1000105E;
            matrix[4] = 0xF123105E;
            matrix[5] = 0x1000FE80;
            break;
    }

    REG_WRITE(reg_addr + 0x00, matrix[0]);
    REG_WRITE(reg_addr + 0x04, matrix[1]);
    REG_WRITE(reg_addr + 0x08, matrix[2]);
    REG_WRITE(reg_addr + 0x0c, matrix[3]);
    REG_WRITE(reg_addr + 0x10, matrix[4]);
    REG_WRITE(reg_addr + 0x14, matrix[5]);
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

static u32 _get_gra_format(e_xcfb_pixel_format format)
{
    u32 fmt;
    
    switch(format)
    {
        case XCFB_PIX_FORMAT_ARGB:
            fmt = DISP_FORMAT_ARGB;
            break;
        case XCFB_PIX_FORMAT_LUT8:
            fmt = DISP_FORMAT_CLUT8;
            break;
        case XCFB_PIX_FORMAT_ARGB1555:
            fmt = DISP_FORMAT_RGB1555;
            break;
        case XCFB_PIX_FORMAT_RGB16:
            fmt = DISP_FORMAT_RGB565;
            break;
        default:
            fmt = DISP_FORMAT_ARGB;
            break;
    }

    return fmt;
}

static int _hdmi_rvblank(u32 video_id_code)
{
    switch(video_id_code)
    {
        case 5:
        case 6:
        case 7:
        case 20:
        case 21:
        case 22:
            return 1;
        default:
            return 0;
    }
}

static void _hdmi_iscr(void)
{
    u32 reg;

    static char iscr1_packet[LENGTH_OF_ISCR1]=
    {
        0x47, 0xa7, 0x89, 0xaa, 0x07, 0x00, 0xff, 0x87,
        0xf1, 0x11, 0xff, 0x05, 0xa5, 0xbc, 0xb5, 0x07
    };

    static char iscr2_packet[LENGTH_OF_ISCR2]=
    {
        0x54, 0xf7, 0xff, 0x41, 0x00, 0xdc, 0xc7, 0x79,
        0x44, 0xf1, 0x0f, 0xaa, 0xab, 0x12, 0x0a, 0xff
    };
    
    //set the ISCR1 packet
    for (reg = HDMI_FC_ISCR1_1; reg <= HDMI_FC_ISCR1_16; reg++)
    {
        HDMI_REG_WRITE(reg, iscr1_packet[reg - HDMI_FC_ISCR1_1]);
    }

    //set the ISCR2 packet
    for (reg = HDMI_FC_ISCR2_0; reg <= HDMI_FC_ISCR2_15; reg++)
    {
        HDMI_REG_WRITE(reg, iscr2_packet[reg - HDMI_FC_ISCR2_0]);
    }

}

int xcfb_hdmi_set_phy(e_xcfb_screen_pixel_clock clock, e_xcfb_screen_color_depth color_depth);

p_xcfb_screen_timing xcfb_screen_find_timing(e_xcfb_screen_format fmt);

int xcfb_hardware_compositor_init(void)
{
    XCFB_DBG_ENTER;

    //disable all the interrupt    
    REG_WRITE(DISP_INT_MASK, 0);

    //clear previous device interrupt
    REG_WRITE(DISP_INT_STATUS, 0xffffffff);

    //enable clock
    REG_WRITE(DISP_CLOCK_STOP, 0);

    //enable display
    _enable_disp_clk(1);

    //disable all layers
    REG_SET_FLD(DISP_HD_GRA0_FORMAT, PIPE_DISABLE, 1);
    REG_SET_FLD(DISP_HD_GRA1_FORMAT, PIPE_DISABLE, 1);
    REG_SET_FLD(DISP_HD_VID0_FORMAT, PIPE_DISABLE, 1);
    REG_SET_FLD(DISP_HD_VID1_FORMAT, PIPE_DISABLE, 1);
    REG_SET_FLD(DISP_SD_GRA0_FORMAT, PIPE_DISABLE, 1);
    REG_SET_FLD(DISP_SD_VID0_FORMAT, PIPE_DISABLE, 1);

    //set opacity to zero
    REG_WRITE(DISP_HD_COMP_ALPHA, 0);
    REG_WRITE(DISP_SD_COMP_ALPHA, 0);

    //clear software reset
    REG_WRITE(DISP_SFT_RESET, 0xFFFF);
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

//DISP_HD functions
int xcfb_hardware_compositor_set_hd_power_mode(e_xcfb_screen_power_mode mode)
{
    XCFB_DBG_ENTER;

    switch(mode & ~XCFB_SCR_MOD_SYNC)
    {
        case XCFB_SCR_MOD_OFF:
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_MODE, 0);
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_EN, 0);
            break;
        case XCFB_SCR_MOD_ON:
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_MODE, 0);
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_EN, 1);
            break;
        case XCFB_SCR_MOD_BLANK:
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_MODE, 0);
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_EN, 1);
            break;
        case XCFB_SCR_MOD_BGCOLOR:
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_MODE, 1);
            REG_SET_FLD(DISP_HD_COMP_CTRL, COMP_EN, 1);
            break;
        default:
            break;
    }
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

//DISP_HD functions

int xcfb_hardware_compositor_set_hd_timing(e_xcfb_screen_format fmt)
{
    u32     tmp;
    u32     field1_vblank_end;
    u32     field2_vblank_end;
    u32     field1_vblank_length;
    u32     field2_vblank_length;
    u32     xsize;
	u32 	v_active, v_total;
    p_xcfb_screen_timing timing;


    XCFB_DBG_ENTER;
	timing = xcfb_screen_find_timing(fmt);

    /* Setup START_FLD_POS */
    if(XCFB_SCR_GET_SCANMODE(timing->format) == XCFB_SCR_SCAN_INTERLACED)
    {
        field1_vblank_length    =    (timing->v_total - timing->v_active) / 2;
        field2_vblank_length    =    field1_vblank_length + 1;

        field1_vblank_end       =    (timing->field1_vblank_start + field1_vblank_length) % timing->v_total - 1;
        field2_vblank_end       =    (timing->field2_vblank_start + field2_vblank_length - 1);

        tmp =   0;
        MEM_SET_FLD(tmp, DISP_HD_COMP_START_FLD_POS, FLD1_POS_Y, field1_vblank_end + 1);
        MEM_SET_FLD(tmp, DISP_HD_COMP_START_FLD_POS, FLD2_POS_Y, field2_vblank_end + 1);
        REG_WRITE(DISP_HD_COMP_START_FLD_POS, tmp);
    }
    else
    {
        field1_vblank_length    =    timing->v_total - timing->v_active;
        field1_vblank_end       =    (timing->field1_vblank_start + field1_vblank_length) % timing->v_total - 1;
        
        tmp    =    0;
        MEM_SET_FLD(tmp, DISP_HD_COMP_START_FLD_POS, FLD1_POS_Y, field1_vblank_end + 1);
        MEM_SET_FLD(tmp, DISP_HD_COMP_START_FLD_POS, FLD2_POS_Y, DISP_HD_COMP_START_FLD_POS_FLD1_POS_Y_MASK);
        REG_WRITE(DISP_HD_COMP_START_FLD_POS, tmp);
    }

    /* Setup  START_PIX_POS */
    tmp =   REG_READ(DISP_HD_COMP_START_PIX_POS);
    MEM_SET_FLD(tmp, DISP_HD_COMP_START_PIX_POS, POS_X, (timing->h_total - timing->h_active) / timing->disp_rep_factor);
    REG_WRITE(DISP_HD_COMP_START_PIX_POS, tmp);

	/* Special mode for 3D */
    if(XCFB_SCR_GET_SCANMODE(timing->format) == XCFB_SCR_SCAN_PROGRESSIVE &&
        XCFB_SCR_GET_STEREO(timing->format) == XCFB_SCR_STEREO_FP &&
        XCFB_SCR_GET_RESOLUTION(timing->format) >= XCFB_SCR_RES_3840x2160 &&
        XCFB_SCR_GET_RESOLUTION(timing->format) <= XCFB_SCR_RES_3840x2160R)
    {
        v_total = timing->v_total / 2;
        v_active = (timing->v_active - (timing->v_total - timing->v_active)) / 2;
        REG_SET_FLD(DISP_HD_COMP_CTRL, ENABLE_3D, 1);
    }
    else
    {
        v_total = timing->v_total;
        v_active = timing->v_active;
        REG_SET_FLD(DISP_HD_COMP_CTRL, ENABLE_3D, 0);
    }

    /* Setup COMP_GLOBAL_SIZE */
    tmp    =    0;
    MEM_SET_FLD(tmp, DISP_HD_COMP_GLOBAL_SIZE, SIZE_X, timing->h_total / timing->disp_rep_factor);
    MEM_SET_FLD(tmp, DISP_HD_COMP_GLOBAL_SIZE, SIZE_Y, v_total);
    REG_WRITE(DISP_HD_COMP_GLOBAL_SIZE, tmp);

    /* Setup COMP_SIZE */
    tmp    =    0;
    xsize  =   (timing->h_active / timing->disp_rep_factor);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, SIZE_X, xsize);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, SIZE_Y, v_active);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, OSR, timing->disp_rep_factor > 1);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, INTERLACED, XCFB_SCR_GET_SCANMODE(timing->format) == XCFB_SCR_SCAN_INTERLACED);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, SIZE_X_B12, xsize >> 12);
    MEM_SET_FLD(tmp, DISP_HD_COMP_SIZE, BYPASS_VID0_XTRAS, xsize > 2048);
    REG_WRITE(DISP_HD_COMP_SIZE, tmp);

    /* Change clock timing */
    _hd_set_clock(timing->clock);

    /* Next we'll update two 656 encoder for HDMI and external 656 */
    _hd_set_dve(timing);
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_hdmi_init(void)
{
    // enable HDMI hpd/rxsense interrupt
    HDMI_REG_WRITE( 0x3006, 0xed );
    HDMI_REG_WRITE( 0x0104, 0xff );     
        
     /* VP_MASK Video Packetizer Mask */
    HDMI_REG_WRITE( 0x0807, 0xff );
        
    /* FC_MASK0(0x10D2), FC_MASK1(0x10D6) and FC_MASK2(0x10DA) are all set to 0xFF to disable the interrupts */
    HDMI_REG_WRITE( 0x10D2, 0xff );
    HDMI_REG_WRITE( 0x10D6, 0xff );
    HDMI_REG_WRITE( 0x10DA, 0xff );
               
    /* CEC_MASK (0x7D02) */
    HDMI_REG_WRITE( 0x7D02, 0xff );
        
    /* I2CM_INT */
    HDMI_REG_WRITE( 0x7E05, 0xbf );
        
    /* I2CM_CTLINT */
    HDMI_REG_WRITE( 0x7E06, 0xff );
        
    /* AUD_INT register */
    HDMI_REG_WRITE( 0x3102, 0x0f );
        
    /* AUD_SPDIFINT */
    HDMI_REG_WRITE( 0x3302, 0x0f );
        
    /* AUD_HBRMASK */
    HDMI_REG_WRITE( 0x3404, 0x0f );
        
    /* Mask off HDCP interrupts */
    HDMI_REG_WRITE( 0x5008, 0xFF );

    /* Mute PHY_I2CM_INT MASK */
    HDMI_REG_WRITE( 0x0188, 0x03 );
    
    /* Clear any HDCP flags ... */
    HDMI_REG_WRITE( 0x5006, 0xFF );
    
    /* Enable HDCP fail and I2C NACK.. and HDCP success, in these cases, we should reset and retry ... */
    HDMI_REG_WRITE( 0x5008, 0x2D );

	/* Enable HDCP22 fail and success interrupt */
	HDMI_REG_WRITE( 0x790D, 0x3F );
	HDMI_REG_WRITE( 0x790E, 0x20 );
        
    /* Change I2C clock divider ratio (0x7E07) */
    HDMI_REG_WRITE( 0x7E07, 0x05 );
	HDMI_REG_WRITE( 0x7E0C, 0x78 );
	HDMI_REG_WRITE( 0x7E0E, 0x90 );
        
    /* Clear out any status bits that may be left over */
    
    /* IH_FC_STAT0 */
    HDMI_REG_WRITE( 0x0100, 0xff );
    
    /* IH_FC_STAT1 */
    HDMI_REG_WRITE( 0x0101, 0xff );
    
    /* IH_FC_STAT2 */
    HDMI_REG_WRITE( 0x0102, 0xff );
    
    /* IH_AS_STAT0 */
    HDMI_REG_WRITE( 0x0103, 0xff );
    
    /* IH_PHY_STAT0 */
    HDMI_REG_WRITE( 0x0104, 0xff );
    
    /* IH_I2CM_STAT0 */
    HDMI_REG_WRITE( 0x0105, 0xff );
    
    /* IH_CEC_STAT0 */
    HDMI_REG_WRITE( 0x0106, 0xff );
    
    /* IH_VP_STAT0 */
    HDMI_REG_WRITE( 0x0107, 0xff );
    
    /* 
    *    Clear out the IH_MUTE bits (0x1FF), reg defaults to 0x3
    *    Set to 0x2, interrupts will be enabled
    */
    HDMI_REG_WRITE( 0x01FF, 0x02 );

	/* Disable HDR update interrupt */
	REG_SET_FLD(DISP_HDMI_TEST_CTRL, HDR_UPDATE_MASK, 0);
	REG_SET_FLD(DISP_HDMI_TEST_CTRL, HDR_EOBUFFER_MASK, 0);

    return XCFB_OK;
}

int xcfb_hardware_hdmi_set_power_mode(e_xcfb_screen_power_mode mode)
{
    XCFB_DBG_ENTER_PAR(mode);

    if(mode & XCFB_SCR_MOD_SYNC)
    {
        mode = XCFB_SCR_MOD_BLANK;
    }

    if(mode == XCFB_SCR_MOD_ON)
    {
        //Enable clock
        HDMI_REG_WRITE(HDMI_MC_CLKGAT, 0);

        //Enable phy
        HDMI_REG_WRITE(HDMI_PHY_CONF0, 0xCE);

        //Reprogram FC_INVIDCONF
        HDMI_REG_WRITE(HDMI_FC_INVIDCONF, HDMI_REG_READ(HDMI_FC_INVIDCONF));        
    }
    else if(mode == XCFB_SCR_MOD_BLANK)
    {
        //Disable clock
        HDMI_REG_WRITE(HDMI_MC_CLKGAT, CLKGATE_AUD_MASK | CLKGATE_TMDS_MASK);

        //Enable phy
        HDMI_REG_WRITE(HDMI_PHY_CONF0, 0xCE);
    }
    else
    {
        //Disable clock
        HDMI_REG_WRITE(HDMI_MC_CLKGAT, CLKGATE_HDCP_MASK | CLKGATE_AUD_MASK | CLKGATE_TMDS_MASK);

        //Disable phy
        HDMI_REG_WRITE(HDMI_PHY_CONF0, PHY_PWRDOWN_EN);
    }
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_hdmi_set_video_mode(e_xcfb_screen_format fmt, e_xcfb_screen_color_depth color_depth)
{
    u32 tmp;
    u32 h_active, h_total;
    u32 vertical_active_width, field1_vblank_length;
    u32 h_active_edge_delay;
    u32 h_active_pulse_width;
    u32 refresh_rate;
    p_xcfb_screen_timing timing;

    timing = xcfb_screen_find_timing(fmt);

    if(timing == NULL)
    {
        return XCFB_ERROR_INVALID_FORMAT;
    }
    
    XCFB_DBG_ENTER_PAR(timing->format);

    xcfb_hdmi_set_phy(timing->clock, color_depth);

    /* configure DVE */
    tmp = REG_READ(DISP_HD_VENC0_DVE_CNTRL);
    switch(color_depth)
    {
        case XCFB_SCR_DEP_8BIT:
        case XCFB_SCR_DEP_8BIT_422:
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSWIDTH, 0);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, ENABLE_420, 0);
            break;
        case XCFB_SCR_DEP_10BIT:
        case XCFB_SCR_DEP_10BIT_422:
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSWIDTH, 1);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, ENABLE_420, 0);
            break;
        case XCFB_SCR_DEP_12BIT:
        case XCFB_SCR_DEP_12BIT_422:
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSWIDTH, 2);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, ENABLE_420, 0);
            break;
        case XCFB_SCR_DEP_8BIT_420:
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSWIDTH, 0);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, ENABLE_420, 1);
            break;
        case XCFB_SCR_DEP_10BIT_420:
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSWIDTH, 1);
            MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, ENABLE_420, 1);
            break;
        default:
            break;
    }
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, CLKDIV, 0);
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, BUSTYPE, 2);             /* 4:4:4 */
    MEM_SET_FLD(tmp, DISP_HD_VENC0_DVE_CNTRL, OUTPUT_ORDER, 0);        /* 0 for 4:4:4 */
    REG_WRITE(DISP_HD_VENC0_DVE_CNTRL, tmp);

    /* output pixel repetition ratio */
    switch (timing->hdmi_rep_factor)
    {
        case 1:
            tmp = NO_PIXEL_REPETITION;
            break;
            
        case 2:
            tmp = PIXEL_REPEAT_ONCE;
            break;
            
        case 4:
            tmp = PIXEL_REPEAT_THREE;
            break;
            
        default:
            tmp = NO_PIXEL_REPETITION;
            break;
    }
    HDMI_REG_MASK_SET(HDMI_VP_PR_CD, 0x0F, tmp);

    //input pixel repetition ratio
    if (timing->disp_rep_factor > 1)
    {
        /* 
        *    This has to be set to OUTPUT_REPEAT_ONCE since it is transmitted over AVI Inforame,
        *    even though repeat is done in DISP
        */
        tmp = (INPUT_REPEAT_ONCE << INCOMING_PR_FACTOR_SHIFT) ;
    }
    else
    {
        tmp = (INPUT_NO_REPEAT << INCOMING_PR_FACTOR_SHIFT) ;
    }
    
    /* This needs to be set correctly as it is being sent over the AVI
    *  InfoFrame
    */
    switch (timing->disp_rep_factor * timing->hdmi_rep_factor)
    {
        case 1:    
            tmp|= OUTPUT_NO_REPEAT;
            break;
        
        case 2:
            tmp|= OUTPUT_REPEAT_ONCE;
            break;
            
        case 4:
            tmp|= OUTPUT_REPEAT_THREE;
            break;
            
        default:
            tmp|= OUTPUT_NO_REPEAT;
            break;
    }
    
    HDMI_REG_WRITE(HDMI_FC_PRCONF, tmp);

    //FC_INVIDCONF configure the Interlaced/progressive, Vblank variation and polarity of all video synchronism of the input video signal.
    tmp = HDMI_REG_READ(HDMI_FC_INVIDCONF);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, IN_I_P, timing->field_length > 0 ? HDMI_INTERLACE_INPUT : HDMI_PROGRESSIVE_INPUT);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, R_V_BLANK_IN_OSC, _hdmi_rvblank(timing->video_id_code) ? MODE_WITH_FRACTIONAL_VBLANK : MODE_WITHOUT_FRACTIONAL_VBLANK);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, DE_IN_POLARITY, DATA_ENABLE_INPUT);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, HSYNC_IN_POLARITY, timing->pol_hsync);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, VSYNC_IN_POLARITY, timing->pol_vsync);
    MEM_SET_FLD2(tmp, HDMI_FC_INVIDCONF, DVI_MODEZ, 1);    
    HDMI_REG_WRITE(HDMI_FC_INVIDCONF, tmp);

    /* Configure HDCP polarity settings for this mode (important) */
    /* 
    *    reg is used to configure the HDCP polarity registers 
    *    DE is always positive
    *    
    */
    tmp = DE_POS_MASK;
    if(timing->pol_hsync)
    {
        tmp |= HSYNC_POS_MASK;
    }
    if(timing->pol_vsync)
    {
        tmp |= VSYNC_POS_MASK;
    }
    HDMI_REG_WRITE(HDMI_A_HDCPPOLCFG, tmp);
    
    //Hactive positions will need to be modified
    //They need to be multiplied by the outgoing_factor
    //of the mode to be set..
    h_active    =   timing->h_active *    timing->hdmi_rep_factor;
    h_total     =   timing->h_total  *    timing->hdmi_rep_factor;
    h_active_edge_delay     =   timing->hsync_edge_delay   * timing->hdmi_rep_factor;
    h_active_pulse_width    =   timing->hsync_pulse_width  * timing->hdmi_rep_factor;  

    //420 has reduced horizontal size
    if(color_depth == XCFB_SCR_DEP_8BIT_420 || color_depth == XCFB_SCR_DEP_10BIT_420)
    {
        h_active /= 2;
        h_total /= 2;
        h_active_edge_delay /= 2;
        h_active_pulse_width /= 2;
    }
    
    //set the Input video Horizontal active pixel region width
    tmp = h_active & FC_INHACTIV0_MASK;
    HDMI_REG_WRITE(HDMI_FC_INHACTIV0, tmp);

    tmp = (h_active & FC_INHACTIV1_MASK) >> FC_INHACTIV1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_INHACTIV1, tmp);

    //set the Input video Horizontal blanking pixel region width
    tmp = (h_total - h_active)  &  FC_INIHBLANK0_MASK;
    HDMI_REG_WRITE(HDMI_FC_INHBLANK0, tmp);

    tmp = ((h_total - h_active) & FC_INIHBLANK1_MASK) >> FC_INIHBLANK1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_INHBLANK1, tmp);

    //set Input video Vertical active pixel region width:
    if (timing->field_length > 0)
    {
        vertical_active_width = timing->v_active >> 1;
        field1_vblank_length = (timing->v_total - timing->v_active) / 2;
    }
    else
    {
        vertical_active_width = timing->v_active;
        field1_vblank_length = timing->v_total - timing->v_active;
    }
    tmp = vertical_active_width & FC_INVACTIV0_MASK;
    HDMI_REG_WRITE(HDMI_FC_INVACTIV0, tmp);
    
    tmp = (vertical_active_width & FC_INVACTIV1_MASK_A21) >> FC_INVACTIV1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_INVACTIV1, tmp);

    //set Input video Vertical blanking pixel region width
    HDMI_REG_WRITE(HDMI_FC_INVBLANK, field1_vblank_length);
    
    //Set Input video Hsync active edge delay
    tmp = h_active_edge_delay & FC_HSYNCINDELAY0_MASK;
    HDMI_REG_WRITE(HDMI_FC_HSYNCINDELAY0, tmp);

    tmp = (h_active_edge_delay & FC_HSYNCINDELAY1_MASK) >> FC_HSYNCINDELAY1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_HSYNCINDELAY1, tmp);

    //set Input video Hsync active pulse width
    tmp = h_active_pulse_width & FC_HSYNCINWIDTH0_MASK;
    HDMI_REG_WRITE(HDMI_FC_HSYNCINWIDTH0, tmp);

    tmp = (h_active_pulse_width & FC_HSYNCINWIDTH1_MASK) >> FC_HSYNCINWIDTH1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_HSYNCINWIDTH1, tmp);

    //set Input video Vsync active edge delay
    HDMI_REG_WRITE(HDMI_FC_VSYNCINDELAY, timing->vsync_edge_delay);

    //set Input video Vsync active pulse width
    tmp = timing->vsync_pulse_width & FC_VSYNCINWIDTH_MASK;
    HDMI_REG_WRITE(HDMI_FC_VSYNCINWIDTH, tmp);

    //set video refresh rate
    refresh_rate = XCFB_SCR_GET_FREQUENCY(timing->format);

    tmp = refresh_rate & FC_INFREQ0_MASK;
    HDMI_REG_WRITE(HDMI_FC_INFREQ0, tmp);

    tmp = (refresh_rate & FC_INFREQ1_MASK) >> FC_INFREQ1_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_INFREQ1, tmp);

    tmp = (refresh_rate & FC_INFREQ2_MASK) >> FC_INFREQ2_SHIFT;
    HDMI_REG_WRITE(HDMI_FC_INFREQ2, tmp);

    //set the extended control period minimum duration
    HDMI_REG_WRITE(HDMI_FC_EXCTRLDUR, HDMI_EXTENDED_CONTROL_PERIOD_MINIMUM_DURATION);

    //set the maximum spacing between consecutive extended control period
    HDMI_REG_WRITE(HDMI_FC_EXCTRLSPAC, HDMI_EXT_CTRL_PERIOD_SPACE);

    //set the 8bits that are going to fill the channel 0 data lines not used to transmit the preamble
    HDMI_REG_WRITE(HDMI_FC_CH0PREAM, HDMI_CH0_PREAMBLE_FILTER);

    //set the 6bits that are going to fill the channel 1 data lines not used to transmit the preamble
    HDMI_REG_WRITE(HDMI_FC_CH1PREAM, HDMI_CH1_PREAMBLE_FILTER);

    //set the 6bits that are going to fill the channel 2 data lines not used to transmit the preamble
    HDMI_REG_WRITE(HDMI_FC_CH2PREAM, HDMI_CH2_PREAMBLE_FILTER);

    //set the video id code as table 3 in the CEA-861-D    
    HDMI_REG_WRITE(HDMI_FC_AVIVID, timing->video_id_code);

    //AVI infoFrame End of Top Bar value
    HDMI_REG_WRITE(HDMI_FC_AVIETB0, 0);
    HDMI_REG_WRITE(HDMI_FC_AVIETB1, 0);

    //AVI infoFrame Start of Bottom Bar value
    HDMI_REG_WRITE(HDMI_FC_AVISBB0, 0);
    HDMI_REG_WRITE(HDMI_FC_AVISBB1, 0);

    //AVI infoFrame End of Left Bar value
    HDMI_REG_WRITE(HDMI_FC_AVIELB0, 0);
    HDMI_REG_WRITE(HDMI_FC_AVIELB1, 0);

    //AVI infoFrame End of Start of Right Bar value
    HDMI_REG_WRITE(HDMI_FC_AVISRB0, 0);
    HDMI_REG_WRITE(HDMI_FC_AVISRB1, 0);

    //the number of high priority packets
    HDMI_REG_WRITE(HDMI_FC_CTRLQHIGH, 0x0f);

    //the number of low priority packets
    HDMI_REG_WRITE(HDMI_FC_CTRLQLOW, 0x03);

    HDMI_REG_WRITE(HDMI_FC_ACP0, 0x01);

    //set the ISRC  
    HDMI_REG_WRITE(HDMI_FC_ISCR1_0, ISRC_VALID | ISRC_CONT);

    //Configures the Frame Composer (FC) RDRB frame interpolation
    HDMI_REG_WRITE(HDMI_FC_DATAUTO1, 0x00);

    HDMI_REG_WRITE(HDMI_FC_DATAUTO2, 0x10);

    HDMI_REG_WRITE(HDMI_FC_DATAUTO3, 0x0f);

    HDMI_REG_WRITE(HDMI_FC_RDRB0, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB1, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB2, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB3, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB4, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB5, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB5, 0);

    HDMI_REG_WRITE(HDMI_FC_RDRB7, 0);

    HDMI_REG_WRITE(HDMI_FC_GMD_EN, 0);

    HDMI_REG_WRITE(HDMI_FC_GMD_CONF, 0x10);

    //disable interpolation and decimation
    HDMI_REG_WRITE(HDMI_CSC_CFG, 0x00);

    //set the control period minimum duration
    HDMI_REG_WRITE(HDMI_FC_CTRLDUR, HDMI_CONTROL_PERIOD_MINIMUM_DURATION);

    //VID
    HDMI_REG_WRITE(HDMI_FC_AVIVID, timing->video_id_code);

    //ISCR
    _hdmi_iscr();
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_hdmi_set_color_mode(e_xcfb_screen_colorimetry colorimetry, e_xcfb_screen_color_depth color_depth, e_xcfb_screen_color_quantization color_quant)
{
    u32 tmp, reg;
    
    XCFB_DBG_ENTER_PAR2(colorimetry, color_depth);

    /* Set input format */
    switch(color_depth)
    {
        case XCFB_SCR_DEP_10BIT:
        case XCFB_SCR_DEP_10BIT_420:
        case XCFB_SCR_DEP_10BIT_422:
            HDMI_REG_WRITE(HDMI_TX_INVID0, YCBCR_444_10);
            break;
        case XCFB_SCR_DEP_12BIT:
        case XCFB_SCR_DEP_12BIT_422:
            HDMI_REG_WRITE(HDMI_TX_INVID0, YCBCR_444_12);
            break;
        default:
            HDMI_REG_WRITE(HDMI_TX_INVID0, YCBCR_444_8);
            break;
    }

    /* Set color_depth */
    switch(color_depth)
    {
        case XCFB_SCR_DEP_10BIT:
        case XCFB_SCR_DEP_10BIT_420:
            HDMI_REG_MASK_SET(HDMI_VP_PR_CD, 0xF0, (DEPTH_30_BIT_10_PER_COMPONENT << COLOR_DEPTH_SHIFT));
            break;
        case XCFB_SCR_DEP_12BIT:
            HDMI_REG_MASK_SET(HDMI_VP_PR_CD, 0xF0, (DEPTH_36_BIT_12_PER_COMPONENT << COLOR_DEPTH_SHIFT));
            break;
        default:
            HDMI_REG_MASK_SET(HDMI_VP_PR_CD, 0xF0, (DEPTH_24_BIT_8_PER_COMPONENT << COLOR_DEPTH_SHIFT));
            break;
    }

    /* reset PP clock of VP_CONF */
    HDMI_REG_MASK_SET(HDMI_VP_CONF, 0x20, 0);
    switch(color_depth)
    {
        case XCFB_SCR_DEP_8BIT_422:
        case XCFB_SCR_DEP_10BIT_422:
        case XCFB_SCR_DEP_12BIT_422:
            tmp =   (YUV_422_ENABLE   << YUV_422_ENABLE_SHIFT) |
                (DATA_FROM_VIDEO_PACKETIZER  << BYPASS_SELECT_SHIFT) |
                (DATA_FROM_YCC422_REMAP_BLOCK);
            break;
        case XCFB_SCR_DEP_10BIT:
        case XCFB_SCR_DEP_12BIT:
        case XCFB_SCR_DEP_10BIT_420:
            tmp =   (PIXEL_PACKET_ENABLE << PIXEL_PACKET_ENABLE_SHIFT)   |
                (DATA_FROM_VIDEO_PACKETIZER << BYPASS_SELECT_SHIFT)  |
                DATA_FROM_PIXEL_PACKING_BLOCK;
            break;
        default:
            tmp =   (BYPASS_ENABLE << BYPASS_ENABLE_SHIFT) |
                (DATA_FROM_VIDEO_PACKETIZER  << BYPASS_SELECT_SHIFT) |
                (DATA_FROM_8_BIT_BYPASS_BLOCK);
            break;
    }
    HDMI_REG_WRITE(HDMI_VP_CONF, tmp);
    
    //YCC422 remap of the Video Packetizer
    switch(color_depth)
    {
        case XCFB_SCR_DEP_10BIT_422:
            HDMI_REG_WRITE(HDMI_VP_REMAP, YCC422_20BIT);
            break;
        case XCFB_SCR_DEP_12BIT_422:
            HDMI_REG_WRITE(HDMI_VP_REMAP, YCC422_24BIT);
            break;
        default:
            HDMI_REG_WRITE(HDMI_VP_REMAP, YCC422_16BIT);
            break;
    }

    //diable stuffing
    HDMI_REG_WRITE(HDMI_VP_STUFF, 0x00);
    for (reg = HDMI_TX_INSTUFFING; reg <= HDMI_TX_BCBDATA1; reg++)
    {
        HDMI_REG_WRITE(reg, 0);
    }

    //Y0Y1
    switch(color_depth)
    {
        case XCFB_SCR_DEP_8BIT:
        case XCFB_SCR_DEP_10BIT:
        case XCFB_SCR_DEP_12BIT:
            if(colorimetry == XCFB_SCR_COL_RGB || colorimetry == XCFB_SCR_COL_RGB_ADOBE || colorimetry == XCFB_SCR_COL_RGB_2020 || colorimetry == XCFB_SCR_COL_NONE)
            {
                HDMI_REG_MASK_SET(HDMI_FC_AVICONF0, 0x03, AVICONF_RGB);
            }
            else
            {
                HDMI_REG_MASK_SET(HDMI_FC_AVICONF0, 0x03, AVICONF_YCBCR_444);
            }
            break;
        case XCFB_SCR_DEP_8BIT_422:
        case XCFB_SCR_DEP_10BIT_422:
        case XCFB_SCR_DEP_12BIT_422:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF0, 0x03, AVICONF_YCBCR_422);
            break;
        case XCFB_SCR_DEP_8BIT_420:
        case XCFB_SCR_DEP_10BIT_420:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF0, 0x03, AVICONF_YCBCR_420);
            break;
        default: 
            break;
    }

    //C0C1
    switch(colorimetry)
    {
        case XCFB_SCR_COL_RGB:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, 0);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, 0);
            break;
        case XCFB_SCR_COL_RGB_2020:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_BT2020 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_RGB_ADOBE:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_ADOBERGB << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_601:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_ITU601 << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, 0);
            break;
        case XCFB_SCR_COL_YUV_709:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_ITU709 << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, 0);
            break;
        case XCFB_SCR_COL_YUV_601_XVYCC:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_XVYCC601 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_709_XVYCC:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_XVYCC709 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_2020:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_BT2020 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_2020C:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_BT2020C << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_ADOBE:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_ADOBEYCC601 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        case XCFB_SCR_COL_YUV_SYCC601:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, COLORIMETRY_XVYCC << COLORIMETRY_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, COLORIMETRY_SYCC601 << EXTENDED_COLORIMETRY_SHIFT);
            break;
        default:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF1, 0xC0, 0);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x70, 0);
            break;
    }

    //Q0Q1, YQ0YQ1
    switch(color_quant)
    {
        case XCFB_SCR_COL_QUAN_LIMITED:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x0C, QUANTIZATION_RANGE_LIMITED << QUANTIZATION_RANGE_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF3, 0x0C, QUANTIZATION_RANGE_YCC_LIMITED << QUANTIZATION_RANGE_YCC_SHIFT);
            break;
        case XCFB_SCR_COL_QUAN_FULL:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x0C, QUANTIZATION_RANGE_FULL << QUANTIZATION_RANGE_SHIFT);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF3, 0x0C, QUANTIZATION_RANGE_YCC_FULL << QUANTIZATION_RANGE_YCC_SHIFT);
            break;
        default:
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF2, 0x0C, 0);
            HDMI_REG_MASK_SET(HDMI_FC_AVICONF3, 0x0C, 0);
            break;
    }

    _set_occ(DISP_HD_HDMI_OCC_C11, colorimetry);

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_init(void)
{
    u32 tmp;
    
    XCFB_DBG_ENTER;

    tmp = REG_READ(DISP_HD_GRA0_FORMAT);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT, DITHER, DISABLE_RANDOM);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT, UNDERFLOW_RESET_OFF, 1);
    REG_WRITE(DISP_HD_GRA0_FORMAT, tmp);

    tmp = REG_READ(DISP_HD_GRA0_FORMAT1);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT1, PEL_POSITION_A, PEL_POSITION_ALPHA);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT1, PEL_POSITION_R, PEL_POSITION_RED);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT1, PEL_POSITION_G, PEL_POSITION_GREEN);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_FORMAT1, PEL_POSITION_B, PEL_POSITION_BLUE);
    REG_WRITE(DISP_HD_GRA0_FORMAT1, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_GRA0_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    REG_WRITE(DISP_HD_GRA0_CMD0_FIFO, tmp);

    tmp = REG_READ(DISP_HD_GRA1_FORMAT);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT, DITHER, DISABLE_RANDOM);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT, UNDERFLOW_RESET_OFF, 1);
    REG_WRITE(DISP_HD_GRA1_FORMAT, tmp);

    tmp = REG_READ(DISP_HD_GRA1_FORMAT1);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT1, PEL_POSITION_A, PEL_POSITION_ALPHA);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT1, PEL_POSITION_R, PEL_POSITION_RED);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT1, PEL_POSITION_G, PEL_POSITION_GREEN);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_FORMAT1, PEL_POSITION_B, PEL_POSITION_BLUE);
    REG_WRITE(DISP_HD_GRA1_FORMAT1, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_GRA1_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_GRA1_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    REG_WRITE(DISP_HD_GRA1_CMD0_FIFO, tmp);

    tmp = REG_READ(DISP_HD_VID0_FORMAT);
    MEM_SET_FLD(tmp, DISP_HD_VID0_FORMAT, URGENT_REQ_FLAG, 0x80);
    REG_WRITE(DISP_HD_VID0_FORMAT, tmp);

    tmp = REG_READ(DISP_HD_VID0_INPUT_Y_LIMIT);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_Y_LIMIT, Y_LIMIT_PLUS, 0xEB);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_Y_LIMIT, Y_LIMIT_MINUS, 0x10);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_Y_LIMIT, Y_ZERO, 0x10);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_Y_LIMIT, Y_CENTER, 0x6E);
    REG_WRITE(DISP_HD_VID0_INPUT_Y_LIMIT, tmp);

    tmp = REG_READ(DISP_HD_VID0_INPUT_CBCR_LIMIT);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_CBCR_LIMIT, CBCR_LIMIT_PLUS, 0xF0);
    MEM_SET_FLD(tmp, DISP_HD_VID0_INPUT_CBCR_LIMIT, CBCR_LIMIT_MINUS, 0x10);
    REG_WRITE(DISP_HD_VID0_INPUT_CBCR_LIMIT, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_VID0_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID0_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID0_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID0_CMD0_FIFO, FLD_SEL, 7);
    REG_WRITE(DISP_HD_VID0_CMD0_FIFO, tmp);

    REG_SET_FLD(DISP_HD_VID0_SAMPLES, VHFIFO_MIN_FE_Y, 3);
    REG_SET_FLD(DISP_HD_VID0_SAMPLES, VHFIFO_MIN_FE_CBCR, 6);
    REG_SET_FLD(DISP_HD_VID0_HSCA_NONLINEAR, FISHEYE_DISABLE, 1);

    tmp = REG_READ(DISP_HD_VID1_FORMAT);
    MEM_SET_FLD(tmp, DISP_HD_VID1_FORMAT, URGENT_REQ_FLAG, 0x80);
    REG_WRITE(DISP_HD_VID1_FORMAT, tmp);
    
    tmp = REG_READ(DISP_HD_VID1_INPUT_Y_LIMIT);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_Y_LIMIT, Y_LIMIT_PLUS, 0xEB);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_Y_LIMIT, Y_LIMIT_MINUS, 0x10);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_Y_LIMIT, Y_ZERO, 0x10);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_Y_LIMIT, Y_CENTER, 0x6E);
    REG_WRITE(DISP_HD_VID1_INPUT_Y_LIMIT, tmp);

    tmp = REG_READ(DISP_HD_VID1_INPUT_CBCR_LIMIT);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_CBCR_LIMIT, CBCR_LIMIT_PLUS, 0xF0);
    MEM_SET_FLD(tmp, DISP_HD_VID1_INPUT_CBCR_LIMIT, CBCR_LIMIT_MINUS, 0x10);
    REG_WRITE(DISP_HD_VID1_INPUT_CBCR_LIMIT, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_VID1_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID1_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID1_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    MEM_SET_FLD(tmp, DISP_HD_VID1_CMD0_FIFO, FLD_SEL, 7);
    REG_WRITE(DISP_HD_VID1_CMD0_FIFO, tmp);

    REG_SET_FLD(DISP_HD_VID1_SAMPLES, VHFIFO_MIN_FE_Y, 3);
    REG_SET_FLD(DISP_HD_VID1_SAMPLES, VHFIFO_MIN_FE_CBCR, 6);

    REG_WRITE(DISP_HD_COMP_VID0_POS, 0);
    REG_WRITE(DISP_HD_COMP_VID1_POS, 0);

    tmp = REG_READ(DISP_SD_GRA0_FORMAT);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT, INTERWOVEN, 1);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT, DITHER, DISABLE_RANDOM);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT, UNDERFLOW_RESET_OFF, 1);
    REG_WRITE(DISP_SD_GRA0_FORMAT, tmp);

    tmp = REG_READ(DISP_SD_GRA0_FORMAT1);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT1, PEL_POSITION_A, PEL_POSITION_ALPHA);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT1, PEL_POSITION_R, PEL_POSITION_RED);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT1, PEL_POSITION_G, PEL_POSITION_GREEN);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_FORMAT1, PEL_POSITION_B, PEL_POSITION_BLUE);
    REG_WRITE(DISP_SD_GRA0_FORMAT1, tmp);

    tmp = REG_READ(DISP_SD_GRA0_CMD0_CTRL);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_CMD0_CTRL, CMD_FIFO_BASE, 1);
    REG_WRITE(DISP_SD_GRA0_CMD0_CTRL, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_SD_GRA0_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_SD_GRA0_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    REG_WRITE(DISP_SD_GRA0_CMD0_FIFO, tmp);

    tmp = REG_READ(DISP_SD_VID0_FORMAT);
    MEM_SET_FLD(tmp, DISP_SD_VID0_FORMAT, DITHER, DISABLE_RANDOM);
    REG_WRITE(DISP_SD_VID0_FORMAT, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_SD_VID0_CMD0_FIFO, WE, 1);
    MEM_SET_FLD(tmp, DISP_SD_VID0_CMD0_FIFO, UPDATE_DATA_REGS, 1);
    MEM_SET_FLD(tmp, DISP_SD_VID0_CMD0_FIFO, UPDATE_COMP_REGS, 1);
    MEM_SET_FLD(tmp, DISP_SD_VID0_CMD0_FIFO, FLD_SEL, 4);
    REG_WRITE(DISP_SD_VID0_CMD0_FIFO, tmp);

    REG_SET_FLD(DISP_SD_VID0_SAMPLES, VHFIFO_MIN_FE_Y, 3);
    REG_SET_FLD(DISP_SD_VID0_SAMPLES, VHFIFO_MIN_FE_CBCR, 6);

    XCFB_DBG_EXIT;

    return XCFB_OK;
}


int xcfb_hardware_layer_set_gra0_enable(int enable)
{
    XCFB_DBG_ENTER_PAR(enable);

    REG_SET_FLD(DISP_HD_GRA0_FORMAT, PIPE_DISABLE, !enable);
    REG_SET_FLD(DISP_HD_COMP_ALPHA, ALPHA_G0, enable ? 255 : 0);

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_format(e_xcfb_pixel_format format)
{
    XCFB_DBG_ENTER_PAR(format);

    REG_SET_FLD(DISP_HD_GRA0_FORMAT, MEM_FORMAT, _get_gra_format(format));

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_size(int width, int height)
{
    u32 tmp, bpp;
    XCFB_DBG_ENTER_PAR2(width, height);

    tmp = REG_READ(DISP_HD_GRA0_SIZE);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SIZE, H_SIZE, width);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SIZE, V_SIZE, height);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SIZE, H_SIZE_B12, width >> 12);
    REG_WRITE(DISP_HD_GRA0_SIZE, tmp);

    tmp = REG_GET_FLD(DISP_HD_GRA0_FORMAT, MEM_FORMAT);
    switch(tmp)
    {
        case DISP_FORMAT_ARGB:
            bpp = 4;
            break;
        case DISP_FORMAT_CLUT8:
            bpp = 1;
            break;
        case DISP_FORMAT_RGB1555:
        case DISP_FORMAT_RGB565:
            bpp = 2;
            break;
        default:
            bpp = 4;
            break;
    }
    REG_WRITE(DISP_HD_GRA0_SAMPLES, width * bpp);

    REG_SET_FLD(DISP_HD_GRA0_FORMAT, MAX_REQ_SIZE, (bpp == 1 || width > 2048) ? 0x20 : 0x28);

    REG_SET_FLD(DISP_HD_GRA0_FORMAT1, MEMCLI_4K, (width > 2048));
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_scale_size(int width, int height)
{
    u32 tmp;
    XCFB_DBG_ENTER_PAR2(width, height);

    tmp = REG_READ(DISP_HD_GRA0_SCALED_SIZE);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SCALED_SIZE, H_SCALED_SIZE, width);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SCALED_SIZE, V_SCALED_SIZE, height);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_SCALED_SIZE, H_SCALED_SIZE_B12, width >> 12);
    REG_WRITE(DISP_HD_GRA0_SCALED_SIZE, tmp);
    
    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_buffer_size(int pitch, int size)
{
    u32 tmp;
    XCFB_DBG_ENTER_PAR2(pitch, size);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_GRA0_BUF, PITCH, pitch >> 4);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_BUF, SIZE, size >> 8);
    REG_WRITE(DISP_HD_GRA0_BUF, tmp);

    REG_SET_FLD(DISP_HD_GRA0_SIZE, BUF_SIZE_MSB, size >> 24);

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_pos(int x, int y)
{
    u32 tmp;
    XCFB_DBG_ENTER_PAR2(x, y);
    
    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_COMP_GRA0_POS, POS_X, x);
    MEM_SET_FLD(tmp, DISP_HD_COMP_GRA0_POS, POS_Y, y);
    REG_WRITE(DISP_HD_COMP_GRA0_POS, tmp);

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_addr(u32 addr0, u32 addr1, u32 addr2, int pitch)
{
    XCFB_DBG_ENTER_PAR(addr0);
    
    REG_WRITE(DISP_HD_GRA0_ADR, addr0);

    XCFB_DBG_EXIT;
    return XCFB_OK;
}

int xcfb_hardware_layer_set_gra0_scaler_coef(u32 iw, u32 ih, u32 ow, u32 oh)
{
    u32 tmp;
    
    REG_WRITE(DISP_HD_GRA0_VSCA_INV_RATIO, ((iw << 18) / ow) << 10);
    REG_WRITE(DISP_HD_GRA0_HSCA_INV_RATIO, ((ih << 18) / oh) << 10);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_GRA0_VSCA_START, START_POS0, 1);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_VSCA_START, START_POS1, 0);
    REG_WRITE(DISP_HD_GRA0_VSCA_START, tmp);

    tmp = 0;
    MEM_SET_FLD(tmp, DISP_HD_GRA0_VSCA_INIT, POSREG_INIT0, 0);
    MEM_SET_FLD(tmp, DISP_HD_GRA0_VSCA_INIT, POSREG_INIT1, 0);
    REG_WRITE(DISP_HD_GRA0_VSCA_INIT, tmp);

    return XCFB_OK;
}

int xcfb_hardware_init(u32 fbAddr, u32 xres, u32 yres, const char * options)
{
    e_xcfb_screen_format xcfb_scr_fmt;
	e_xcfb_screen_color_depth xcfb_scr_color_dep;
    /* Convert the X,Y resolution pair into a single number */
#define RESOLUTION(x, y) (((x & 0xffff) << 16) | (y & 0xffff))
	switch (RESOLUTION(xres, yres)) {
	case RESOLUTION(720, 480):
		xcfb_scr_fmt = XCFB_SCR_FMT_720x480_P_60;
		xcfb_scr_color_dep = XCFB_SCR_DEP_8BIT;
		break;
	case RESOLUTION(1280, 720):
		xcfb_scr_fmt = XCFB_SCR_FMT_1280x720_P_60;
		xcfb_scr_color_dep = XCFB_SCR_DEP_8BIT;
		break;
	case RESOLUTION(1920, 1080):
		if((strncmp(options, "p", 1) == 0) || (strncmp(options, "P", 1) == 0)) {
			xcfb_scr_fmt = XCFB_SCR_FMT_1920x1080_P_60;
		} else {
			xcfb_scr_fmt = XCFB_SCR_FMT_1920x1080_I_60;
		}
		xcfb_scr_color_dep = XCFB_SCR_DEP_8BIT;
		break;
	case RESOLUTION(3840, 2160):
		xcfb_scr_fmt = XCFB_SCR_FMT_3840x2160_P_60;
		xcfb_scr_color_dep = XCFB_SCR_DEP_8BIT_420;
		break;
	case RESOLUTION(4096, 2160):
		xcfb_scr_fmt = XCFB_SCR_FMT_4096x2160_P_60;
		xcfb_scr_color_dep = XCFB_SCR_DEP_8BIT_420;
		break;
	default:
		printf("XCFB:   Unsupported resolution %ux%u\n", xres, yres);
		return -1;
	}

	debug("XCFB: resolution %ux%u\n", xres, yres);

    xcfb_hardware_compositor_init();
    xcfb_hardware_compositor_set_hd_power_mode(XCFB_SCR_MOD_ON);
    xcfb_hardware_compositor_set_hd_timing(xcfb_scr_fmt);

    xcfb_hardware_hdmi_init();    
    xcfb_hardware_hdmi_set_video_mode(xcfb_scr_fmt, xcfb_scr_color_dep);
    xcfb_hardware_hdmi_set_color_mode(XCFB_SCR_COL_YUV_709, XCFB_SCR_DEP_8BIT, XCFB_SCR_COL_QUAN_NONE);
	xcfb_hardware_hdmi_set_power_mode(XCFB_SCR_MOD_ON);

    xcfb_hardware_layer_init();
    xcfb_hardware_layer_set_gra0_format(XCFB_PIX_FORMAT_ARGB);
    xcfb_hardware_layer_set_gra0_buffer_size(xres * 4, 0);
    xcfb_hardware_layer_set_gra0_size(xres, yres);
    xcfb_hardware_layer_set_gra0_scale_size(xres, yres);
    xcfb_hardware_layer_set_gra0_scaler_coef(xres, yres, xres, yres);
    xcfb_hardware_layer_set_gra0_pos(0, 0);
    xcfb_hardware_layer_set_gra0_addr(fbAddr, 0, 0, 0);
    xcfb_hardware_layer_set_gra0_enable(1);

    return 0;
}

int drv_video_init(void)
{
	const char *options;
	unsigned int depth = 0, freq = 0;

	if (!video_get_video_mode(&ctfb.winSizeX, &ctfb.winSizeY, &depth, &freq,
				  &options))
    {
        ctfb.winSizeX = 1280;
        ctfb.winSizeY = 720;
        depth = 24;
        freq = 60;
    }
	debug("%s: res= %d x %d depth=%d freq=%d\n", __func__,  ctfb.winSizeX,  ctfb.winSizeY, depth, freq);

	/* fill in Graphic device struct */
	sprintf(ctfb.modeIdent, "%ix%ix%i %ikHz %iHz",
		ctfb.winSizeX, ctfb.winSizeY, depth, 64, freq);

	ctfb.plnSizeX = ctfb.winSizeX;
	ctfb.plnSizeY = ctfb.winSizeY;

	ctfb.gdfBytesPP = 4;
	ctfb.gdfIndex = GDF_32BIT_X888RGB;

	ctfb.isaBase = 0;
	ctfb.pciBase = 0;
	ctfb.memSize = VIDEO_SIZE;
    ctfb.frameAdrs = getenv_ulong("video-offs", 16, CONFIG_VIXS_DISP_FB_ADDR);

    memset((void *)ctfb.frameAdrs, 0, VIDEO_SIZE);

	/* Cursor Start Address */
	ctfb.dprBase = 0;
	ctfb.vprBase = 0;
	ctfb.cprBase = 0;

    if (xcfb_hardware_init(ctfb.frameAdrs, ctfb.winSizeX, ctfb.winSizeY, options) != 0) {
		printf("%s failed\n", __func__);
		return 0;
	}
	return 1;
}

/*
 * Display the BMP file located at address bmp_image.
 */
int video_display_bitmap(ulong bmp_image, int x, int y)
{
	ushort xcount, ycount;
	uchar *fb;
	bmp_image_t *bmp = (bmp_image_t *) bmp_image;
	uchar *bmap;
	ushort padded_line;
	unsigned long width, height, bpp;
	unsigned colors;
	unsigned long compression;

	if (!((bmp->header.signature[0] == 'B') &&
	      (bmp->header.signature[1] == 'M'))) {
	      
		printf("Error: no valid bmp image at %lx\n", bmp_image);
		return 1;
	}

	width = le32_to_cpu(bmp->header.width);
	height = le32_to_cpu(bmp->header.height);
	bpp = le16_to_cpu(bmp->header.bit_count);
	colors = le32_to_cpu(bmp->header.colors_used);
	compression = le32_to_cpu(bmp->header.compression);

	if (compression != BMP_BI_RGB) {
		printf("Error: compression type %ld not supported\n",
		       compression);
		return 1;
	}

	padded_line = (((width * bpp + 7) / 8) + 3) & ~0x3;
    
    /*
     * Just ignore elements which are completely beyond screen
     * dimensions.
     */
    if ((x >= VIDEO_VISIBLE_COLS) || (y >= VIDEO_VISIBLE_ROWS))
        return 0;
    
    if ((x + width) > VIDEO_VISIBLE_COLS)
        width = VIDEO_VISIBLE_COLS - x;
    if ((y + height) > VIDEO_VISIBLE_ROWS)
        height = VIDEO_VISIBLE_ROWS - y;
    
    bmap = (uchar *) bmp + le32_to_cpu(bmp->header.data_offset);
    fb = (uchar *) (ctfb.frameAdrs +
            ((y + height - 1) * VIDEO_COLS * VIDEO_PIXEL_SIZE) +
            x * VIDEO_PIXEL_SIZE);

	/* We handle only 24 bpp bitmaps */
	switch (le16_to_cpu(bmp->header.bit_count)) {
	case 24:
		padded_line -= 3 * width;
		ycount = height;
		switch (VIDEO_DATA_FORMAT) {
		case GDF_32BIT_X888RGB:
			while (ycount--) {
				xcount = width;
				while (xcount--) {
					FILL_32BIT_X888RGB(bmap[2], bmap[1],
							   bmap[0]);
					bmap += 3;
				}
				bmap += padded_line;
				fb -= (VIDEO_VISIBLE_COLS + width) *
							VIDEO_PIXEL_SIZE;
			}
			break;
		default:
			printf("Error: 24 bits/pixel bitmap incompatible "
				"with current video mode\n");
			break;
		}
		break;
	default:
		printf("Error: %d bit/pixel bitmaps not supported by U-Boot\n",
			le16_to_cpu(bmp->header.bit_count));
		break;
	}

    flush_dcache_all();

	return (0);
}


