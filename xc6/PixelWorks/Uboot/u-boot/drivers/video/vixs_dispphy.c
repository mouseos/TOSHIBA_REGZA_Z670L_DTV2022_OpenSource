#include "vixs_dispdef.h"
#include <asm/arch-xc6/xcodeRegDef.h>

typedef void (*f_xcfb_set_hdmi_phy) (void);
typedef struct {
	e_xcfb_screen_pixel_clock	clock;
	e_xcfb_screen_color_depth	color_depth;
	f_xcfb_set_hdmi_phy			set_phy;
} s_xcfb_set_hdmi_phy_entry;

static void xcfb_reset_hdmi_phy(void)
{
    REG_WRITE(DISP_HDMI_WRDATA, 0x00);
    REG_WRITE(DISP_HDMI_ADDR, 0x80000801);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, 0xd6);   // start cycle of pddq and txpwron, then cycle phy reset
    REG_WRITE(DISP_HDMI_ADDR, 0x80003000);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, 0x1);
    REG_WRITE(DISP_HDMI_ADDR, 0x80004005);  // Toggle phy reset
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
	udelay(50);
    REG_WRITE(DISP_HDMI_WRDATA, 0x0);
    REG_WRITE(DISP_HDMI_ADDR, 0x80004005);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
}

static void xcfb_resume_hdmi_phy(void)
{
	udelay(50);
    REG_WRITE(DISP_HDMI_WRDATA, 0xc6);   // finish pddq and txpoweron sequencing
    REG_WRITE(DISP_HDMI_ADDR, 0x80003000);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
	udelay(50);
    //REG_WRITE(DISP_HDMI_WRDATA, 0xce);
    //REG_WRITE(DISP_HDMI_ADDR, 0x80003000);
    //REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
}

static void xcfb_write_hdmi_phy_reg(int reg, u16 value)
{
    REG_WRITE(DISP_HDMI_WRDATA, reg);  //  Register
    REG_WRITE(DISP_HDMI_ADDR, 0x80003021);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, value >> 8);  // Data msb's
    REG_WRITE(DISP_HDMI_ADDR, 0x80003022);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, value & 0xFF);  // Data lsb's
    REG_WRITE(DISP_HDMI_ADDR, 0x80003023);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, 0x10);  // Trigger write
    REG_WRITE(DISP_HDMI_ADDR, 0x80003026);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_ADDR, 0x108);  // poll for done status
    REG_POLL(DISP_HDMI_RDDATA, 0x2, 0x2);

    REG_WRITE(DISP_HDMI_WRDATA, 0x02);
    REG_WRITE(DISP_HDMI_ADDR, 0x80000108);  // clear done status
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
}

static void xcfb_read_hdmi_phy_reg(int reg, u16 *value)
{
    u8 b0, b1;
    
    REG_WRITE(DISP_HDMI_WRDATA, reg);  //  Register
    REG_WRITE(DISP_HDMI_ADDR, 0x80003021);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_WRDATA, 0x1);  // Trigger read
    REG_WRITE(DISP_HDMI_ADDR, 0x80003026);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_ADDR, 0x108);  // poll for done status
    REG_POLL(DISP_HDMI_RDDATA, 0x2, 0x2);

    REG_WRITE(DISP_HDMI_WRDATA, 0x02);
    REG_WRITE(DISP_HDMI_ADDR, 0x80000108);  // clear done status
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    REG_WRITE(DISP_HDMI_ADDR, 0x3024);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
    b0 = REG_READ(DISP_HDMI_RDDATA);

    REG_WRITE(DISP_HDMI_ADDR, 0x3025);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
    b1 = REG_READ(DISP_HDMI_RDDATA);

    *value = (b0 << 8) | b1;
}

static void xcfb_set_hdmi_phy_reg(int reg, u16 value)
{
    int i;
    u16 tmp;

    for(i = 0; i < 5; i++)
    {
        xcfb_write_hdmi_phy_reg(reg, value);

        xcfb_read_hdmi_phy_reg(reg, &tmp);

        if((tmp & 0x7FFF) == (value & 0x7FFF))
        {
            break;
        }
		udelay(50);
    }
	if(i == 5)
		printf("%s: failed\n",__func__);
}

static void xcfb_set_hdmi_phy_param(int reg06, int reg10, int reg15, int reg09, int reg0e, int reg19)
{
    REG_WRITE(DISP_HDMI_WRDATA, 0x08);       // Set up phy I2C write control register
    REG_WRITE(DISP_HDMI_ADDR, 0x80003029);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);
    
    REG_WRITE(DISP_HDMI_WRDATA, 0x20);       // I2C address
    REG_WRITE(DISP_HDMI_ADDR, 0x80003020);
    REG_POLL(DISP_HDMI_STATUS, 0x1, 0x1);

    xcfb_set_hdmi_phy_reg(0x06, reg06);
    xcfb_set_hdmi_phy_reg(0x10, reg10);
    xcfb_set_hdmi_phy_reg(0x15, reg15);
    xcfb_set_hdmi_phy_reg(0x19, reg19);
    xcfb_set_hdmi_phy_reg(0x0e, reg0e);
    xcfb_set_hdmi_phy_reg(0x09, reg09);
}

static void xcfb_set_hdmi_phy_init_594MHz_1x_8bit(void)
{
    XCFB_DBG_ENTER;
    
    xcfb_set_hdmi_phy_param(0x1A7C, 0x0008, 0x0003, 0x800d, 0x0000, 0x0000);

    XCFB_DBG_EXIT;
}

static void xcfb_set_hdmi_phy_init_148x5MHz_1x_8bit(void)
{
    XCFB_DBG_ENTER;

    xcfb_set_hdmi_phy_param(0x0051, 0x001b, 0x0002, 0x800b, 0x0108, 0x0000);

    XCFB_DBG_EXIT;
}

static void xcfb_set_hdmi_phy_init_74x25MHz_1x_8bit(void)
{
    XCFB_DBG_ENTER;

    xcfb_set_hdmi_phy_param(0x0072, 0x0008, 0x0001, 0x8009, 0x0210, 0x0000);

    XCFB_DBG_EXIT;
}

static void xcfb_set_hdmi_phy_init_27MHz_1x_8bit(void)
{
    XCFB_DBG_ENTER;

    xcfb_set_hdmi_phy_param(0x00B3, 0x0000, 0x0000, 0x8009,0x0210, 0x0000);

    XCFB_DBG_EXIT;
}

static const s_xcfb_set_hdmi_phy_entry xcfb_set_hdmi_phy_entry[] =
{
	{XCFB_SCR_PIX_CLK_594MHz, XCFB_SCR_DEP_8BIT, xcfb_set_hdmi_phy_init_594MHz_1x_8bit},
	{XCFB_SCR_PIX_CLK_148_5MHz, XCFB_SCR_DEP_8BIT, xcfb_set_hdmi_phy_init_148x5MHz_1x_8bit},
	{XCFB_SCR_PIX_CLK_74_25MHz, XCFB_SCR_DEP_8BIT, xcfb_set_hdmi_phy_init_74x25MHz_1x_8bit},
	{XCFB_SCR_PIX_CLK_27_027MHz, XCFB_SCR_DEP_8BIT, xcfb_set_hdmi_phy_init_27MHz_1x_8bit},
};

static void xcfb_set_pclk(e_xcfb_screen_pixel_clock clock, e_xcfb_screen_color_depth color_depth)
{
    int pclk_sel;
    int tmp;

    switch(clock)
    {
        case XCFB_SCR_PIX_CLK_594MHz:
            pclk_sel = 1;           // 1/2 from hdpll for 420 only
            break;
 
        case XCFB_SCR_PIX_CLK_148_5MHz:        
            if(color_depth == XCFB_SCR_DEP_8BIT_420 || color_depth == XCFB_SCR_DEP_10BIT_420)
            {
                pclk_sel = 3;       // 1/8 from hdpll for 420
            }
            else
            {
                pclk_sel = 2;       // 1/4 from hdpll for 444
            }
            break;

        case XCFB_SCR_PIX_CLK_74_25MHz:
            if(color_depth == XCFB_SCR_DEP_8BIT_420 || color_depth == XCFB_SCR_DEP_10BIT_420)
            {
                pclk_sel = 4;       // 1/16 from hdpll for 420
            }
            else
            {
                pclk_sel = 3;       // 1/8 from hdpll for 444
            }
            break;
        
        case XCFB_SCR_PIX_CLK_27_027MHz:
            if(color_depth == XCFB_SCR_DEP_8BIT_420 || color_depth == XCFB_SCR_DEP_10BIT_420)
            {
                pclk_sel = 7;           // 1/22 from hdpll for 420
            }
            else
            {
                pclk_sel = 6;           // 1/11 from hdpll for 444
            }
            break;
 
        default:
			printf("%s: not support clock\n", __func__);
            return;
    }

    //setup clock source 0
    tmp = REG_READ(CG1_CLK_SRC_SEL0);
    MEM_SET_FLD(tmp, CG1_CLK_SRC_SEL0, PCLK_SRC_SEL, pclk_sel);
    REG_WRITE(CG1_CLK_SRC_SEL0, tmp);
}

int xcfb_hdmi_set_phy(e_xcfb_screen_pixel_clock clock, e_xcfb_screen_color_depth color_depth)
{
    int i;

    int ret = XCFB_ERROR_INVALID_FORMAT;
    
    for(i = 0; i < sizeof(xcfb_set_hdmi_phy_entry) / sizeof(xcfb_set_hdmi_phy_entry[0]); i++)
    {
        const s_xcfb_set_hdmi_phy_entry * entry = &xcfb_set_hdmi_phy_entry[i];
        
        if(entry->clock == clock && entry->color_depth == color_depth)
        {
            xcfb_set_pclk(clock, color_depth);

			xcfb_reset_hdmi_phy();            
            entry->set_phy();
			xcfb_resume_hdmi_phy();

            ret = XCFB_OK;            
            break;
        }
    }

    return ret;
}

#define     VIDEO_ID_CODE_REGULAR(x)        (x)
#define XCFB_SCR_GET_2D_FMT(fmt) ((fmt) & ~(XCFB_SCR_STEREO_SS | XCFB_SCR_STEREO_TB))

/* 720x480p 60Hz 27M */
static s_xcfb_screen_timing timing_720x480p60 =
{
    .video_id_code =   VIDEO_ID_CODE_REGULAR(2), 
    .format = XCFB_SCR_FMT_720x480_P_60,

    .h_active   =   720, 
    .v_active   =   480, 
    .h_total    =   858, 
    .v_total    =   525, 
    .hsync_edge_delay = 16, 
    .hsync_pulse_width = 62, 
    .vsync_edge_delay  = 9, 
    .vsync_pulse_width = 6, 
    .field1_vblank_start = 523, 
    .field2_vblank_start = 0, 
    .field1_vsync_start  = 7, 
    .field2_vsync_start  = 0, 
    .clock    =   XCFB_SCR_PIX_CLK_27_027MHz, 
    .pol_hsync  =   0, 
    .pol_vsync  =   0,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   0, 
    .field_length   =   0,    
};

/* 1280x720p 60Hz */
static s_xcfb_screen_timing timing_1280x720p60 =
{
    .video_id_code =   VIDEO_ID_CODE_REGULAR(4), 
    .format = XCFB_SCR_FMT_1280x720_P_60,

    .h_active   =   1280, 
    .v_active   =   720, 
    .h_total    =   1650, 
    .v_total    =   750, 
    .hsync_edge_delay = 110, 
    .hsync_pulse_width = 40, 
    .vsync_edge_delay  = 5, 
    .vsync_pulse_width = 5, 
    .field1_vblank_start = 746, 
    .field2_vblank_start = 0, 
    .field1_vsync_start  = 1, 
    .field2_vsync_start  = 0, 
    .clock    =   XCFB_SCR_PIX_CLK_74_25MHz, 
    .pol_hsync  =   1, 
    .pol_vsync  =   1,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   0, 
    .field_length   =   0,    
};

/* 1920x1080p 60Hz */
static s_xcfb_screen_timing timing_1920x1080p60 =
{
    .video_id_code =   VIDEO_ID_CODE_REGULAR(16), 
    .format = XCFB_SCR_FMT_1920x1080_P_60,
 
    .h_active   =   1920, 
    .v_active   =   1080, 
    .h_total    =   2200, 
    .v_total    =   1125, 
    .hsync_edge_delay = 88, 
    .hsync_pulse_width = 44, 
    .vsync_edge_delay  = 4, 
    .vsync_pulse_width = 5, 
    .field1_vblank_start = 1122, 
    .field2_vblank_start = 0, 
    .field1_vsync_start  = 1, 
    .field2_vsync_start  = 0, 
    .clock    =   XCFB_SCR_PIX_CLK_148_5MHz, 
    .pol_hsync  =   1, 
    .pol_vsync  =   1,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   0, 
    .field_length   =   0,    
};

/* 1920x1080i 60Hz */
static s_xcfb_screen_timing timing_1920x1080i60 =
{
    .video_id_code  =   VIDEO_ID_CODE_REGULAR(5),
    .format = XCFB_SCR_FMT_1920x1080_I_60,

    .h_active   =   1920,
    .v_active   =   1080,
    .h_total    =   2200,
    .v_total    =   1125,
    .hsync_edge_delay   =   88,
    .hsync_pulse_width  =   44,
    .vsync_edge_delay   =   2,
    .vsync_pulse_width  =   5,
    .field1_vblank_start=   1124,
    .field2_vblank_start=   561,
    .field1_vsync_start =   1,
    .field2_vsync_start =   563,
    .clock    =   XCFB_SCR_PIX_CLK_74_25MHz, 
    .pol_hsync  =   1, 
    .pol_vsync  =   1,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   912, 
    .field_length   =   540,    
};

/* 3840x2160p 60Hz */
static s_xcfb_screen_timing timing_3840x2160p60 =
{
    .video_id_code =   VIDEO_ID_CODE_REGULAR(97), 
    .format = XCFB_SCR_FMT_3840x2160_P_60,

    .h_active   =   3840, 
    .v_active   =   2160, 
    .h_total    =   4400, 
    .v_total    =   2250, 
    .hsync_edge_delay = 176, 
    .hsync_pulse_width = 88, 
    .vsync_edge_delay  = 8, 
    .vsync_pulse_width = 10, 
    .field1_vblank_start = 2243,     /* Vsync pulse width+back porch+active video*/ 
    .field2_vblank_start = 0, 
    .field1_vsync_start  = 1, 
    .field2_vsync_start  = 0, 
    .clock    =   XCFB_SCR_PIX_CLK_594MHz, 
    .pol_hsync  =   1, 
    .pol_vsync  =   1,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   0, 
    .field_length   =   0,    
};

/* 4096x2160 60Hz */
static s_xcfb_screen_timing timing_4096x2160p60 =
{
    .video_id_code =   VIDEO_ID_CODE_REGULAR(102), 
    .format = XCFB_SCR_FMT_4096x2160_P_60,

    .h_active   =   4096, 
    .v_active   =   2160, 
    .h_total    =   4400, 
    .v_total    =   2250, 
    .hsync_edge_delay = 88, 
    .hsync_pulse_width = 88, 
    .vsync_edge_delay  = 8, 
    .vsync_pulse_width = 10, 
    .field1_vblank_start = 2243,     /* Vsync pulse width+back porch+active video*/ 
    .field2_vblank_start = 0, 
    .field1_vsync_start  = 1, 
    .field2_vsync_start  = 0, 
    .clock    =   XCFB_SCR_PIX_CLK_594MHz, 
    .pol_hsync  =   1, 
    .pol_vsync  =   1,
    .disp_rep_factor    =   1, 
    .hdmi_rep_factor    =   1, 
    .f2off  =   0, 
    .field_length   =   0,    
};


static p_xcfb_screen_timing timing_table[] =
{	
	&timing_720x480p60,
	&timing_1280x720p60,
	&timing_1920x1080p60,
	&timing_1920x1080i60,
	&timing_3840x2160p60,
	&timing_4096x2160p60,
};

p_xcfb_screen_timing xcfb_screen_find_timing(e_xcfb_screen_format fmt)
{
    int i;
    for(i = 0; i < sizeof(timing_table) / sizeof(timing_table[0]); i++)
    {
        if(timing_table[i]->format == XCFB_SCR_GET_2D_FMT(fmt))
        {
            return timing_table[i];
        }
    }

    return NULL;
}

