#ifndef _dispdef_h
#define _dispdef_h

//#include <xcfb_types.h>
#include <common.h>
#include <asm/arch-xc6/xcodeRegDef.h>
#include <asm/arch-xc6/xcode_hdmi.h>

#include "xcfb_enum.h"

#if 1
#define XCFB_DBG_ENTER
#define XCFB_DBG_EXIT
#define XCFB_DBG_ENTER_PAR(_x) do{}while(0)
#define XCFB_DBG_ENTER_PAR2(_x,_y) do{}while(0)
#else
#include <stdio.h>
#define XCFB_DBG_ENTER_PAR(_x) printf("%s enter, "#_x"=0x%x\n", __FUNCTION__, (u32)(_x))
#define XCFB_DBG_ENTER_PAR2(_x,_y) printf("%s enter, "#_x"=0x%x, "#_y"=0x%x\n", __FUNCTION__, (u32)(_x), (u32)(_y))
#define XCFB_DBG_ENTER printf("%s enter\n", __FUNCTION__)
#define XCFB_DBG_EXIT  printf("%s exit\n", __FUNCTION__)
#endif

#define DISP_LOCK_CG
#define DISP_UNLOCK_CG

/* read and write all bits in a register */
extern u32 mmio_base;

/* read/write register */
#define REG_WRITE(reg,val) (*(u32 volatile*)((mmio_base)+(reg))=(val))
#define REG_READ(reg)      (*(u32 volatile*)((mmio_base)+(reg)))

/* get/set individual bit fields in a register */
#define REG_GET_FLD(reg,fld)     ((REG_READ(reg)&reg##_##fld##_MASK)>>reg##_##fld##_SHIFT)
#define REG_SET_FLD(reg,fld,val) REG_WRITE(reg,(REG_READ(reg)&(~reg##_##fld##_MASK))|(((val)<<reg##_##fld##_SHIFT)&reg##_##fld##_MASK))

/* get/set individual bit fields in a memory copy of a register */
#define MEM_GET_FLD(var,reg,fld)     (((var)&reg##_##fld##_MASK)>>reg##_##fld##_SHIFT)
#define MEM_SET_FLD(var,reg,fld,val) (var)=(((var)&(~reg##_##fld##_MASK))|(((val)<<reg##_##fld##_SHIFT)&reg##_##fld##_MASK))

/* get/set individual bit fields in a register */
#define REG_GET_FLD2(reg,fld)     ((REG_READ(reg)&fld##_MASK)>>fld##_SHIFT)
#define REG_SET_FLD2(reg,fld,val) REG_WRITE(reg,(REG_READ(reg)&(~fld##_MASK))|(((val)<<fld##_SHIFT)&fld##_MASK))

/* get/set individual bit fields in a memory copy of a register */
#define MEM_GET_FLD2(var,reg,fld)     (((var)&fld##_MASK)>>fld##_SHIFT)
#define MEM_SET_FLD2(var,reg,fld,val) (var)=(((var)&(~fld##_MASK))|(((val)<<fld##_SHIFT)&fld##_MASK))

/* poll register */
#define REG_POLL(reg, bit_mask, value)\
{\
    int counter;\
    for(counter = 1000000; (REG_READ(reg) & bit_mask) != value && counter; counter--);\
}

typedef enum {
    XCFB_SCR_PIX_CLK_148_5MHz = 0, 
    XCFB_SCR_PIX_CLK_74_25MHz,     
    XCFB_SCR_PIX_CLK_27_027MHz,     
    XCFB_SCR_PIX_CLK_594MHz,     
    XCFB_SCR_PIX_CLK_CUSTOMIZED = 0x80000000,
} e_xcfb_screen_pixel_clock;

typedef struct _xcfb_screen_timing
{
    u32    video_id_code;          /* HDMI video code */
    e_xcfb_screen_format format;   /* Output format */

    u32    h_active;               /* Horizontal active pixels = n(xres)*/
    u32    v_active;               /* Vertical active lines */
    u32    h_total;                /* Horizontal total pixels */
    u32    v_total;                /* Vertical total pixels */
    u32    hsync_edge_delay;       /* Front Porch Width */
    u32    hsync_pulse_width;      /* Pulse Width, Back Porch = h_total - h_active - (hsync_edge_delay + hsync_pulse_width)*/
    u32    vsync_edge_delay;       /* Front Porch V side */
    u32    vsync_pulse_width;      /* Number of lines Vsync is asserted for */
    u32    field1_vblank_start;    /* Line number in which vblank in field 1 starts */
    u32    field2_vblank_start;    /* Line number in which vblank in field 2 starts */
    u32    field1_vsync_start;     /* Line number in which vsync assertion starts in field 1 */
    u32    field2_vsync_start;     /* Line number in which vsync assertion starts in field 2 */

    e_xcfb_screen_pixel_clock clock;    /*Pixel clock*/

    u32    pol_hsync;              /* Polarity (1 = rising edge active hi, 0 = falling edge active low) */
    u32    pol_vsync;              /* Same as above */

    u32    disp_rep_factor;        /* DISP repetition factor (1 or 2) */
    u32    hdmi_rep_factor;        /* Repetition factor on HDMI (1 or 2 usually) */

    u32    f2off;                  /* Fixed offset (for vsync in field 2) */
    u32    field_length;           /* vertical lines of each field */
    u32    fpga_factor;            /* Put in DISP_VDAC_BIAS_CNTL, 27M = 0x80000, 74.25M = 0x10000, 148.5M = 0x00000 */
} s_xcfb_screen_timing;

typedef const struct _xcfb_screen_timing * p_xcfb_screen_timing;

typedef struct _xcfb_clock_param
{
    e_xcfb_screen_pixel_clock clock;
    
    u32     pll1;
    u32     hdpll_sel;
    u32     vhd_sel;
    u32     dac0_sel;
    u32     gen90_num;
    u32     gen90_den;
    u32     gen27_num;
    u32     gen27_den;
    u32     mod_num;
    u32     mod_den;
}s_xcfb_clock_param, *ps_xcfb_clock_param;

typedef void * p_xcfb_handle;

#endif
