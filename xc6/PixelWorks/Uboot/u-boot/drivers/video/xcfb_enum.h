/* Copyright 2012 ViXS Systems Inc. Proprietary and Confidential */

#ifndef _xcfb_enum_h
#define _xcfb_enum_h

enum {
     XCFB_OK = 0,
     XCFB_CONTINUE = 1,
     XCFB_ERROR_GENERAL = 0x80000000,
     XCFB_ERROR_NOT_INITIALIZED,
     XCFB_ERROR_NOT_IMPLEMENTED,    
     XCFB_ERROR_INVALID_RANGE,
     XCFB_ERROR_INVALID_FORMAT,
     XCFB_ERROR_INVALID_ARGS,
     XCFB_ERROR_FIFO_EMPTY,
     XCFB_ERROR_FIFO_FULL,
     XCFB_ERROR_LOCKED,
     XCFB_ERROR_TIMEOUT,
     XCFB_ERROR_INSUFFICIENT_MEMORY,
     XCFB_ERROR_BUSY,
     XCFB_ERROR_ALREADY_EXIST,
     XCFB_ERROR_NOT_EXIST,
     XCFB_ERROR_BUFFER_TOO_SMALL,
     XCFB_ERROR_NACK,
     XCFB_ERROR_DISCARD,
     XCFB_ERROR_REPEAT,
};

#define XCFB_IS_ERROR(x) ((x) != XCFB_OK)

typedef enum {
     XCFB_G2D_ACC_FILLRECTANGLE  = 0x00000001,  /* FillRectangle() is accelerated. */
     XCFB_G2D_ACC_DRAWRECTANGLE  = 0x00000002,  /* DrawRectangle() is accelerated. */
     XCFB_G2D_ACC_DRAWLINE       = 0x00000004,  /* DrawLine() is accelerated. */
     XCFB_G2D_ACC_FILLTRIANGLE   = 0x00000008,  /* FillTriangle() is accelerated. */     
     XCFB_G2D_ACC_BLIT           = 0x00010000,  /* Blit() and TileBlit() are accelerated. */
     XCFB_G2D_ACC_STRETCHBLIT    = 0x00020000,  /* StretchBlit() is accelerated. */
}e_xcfb_g2d_acc_mask;

typedef enum {
     XCFB_G2D_BLIT_NOFX               = 0x00000000, /* uses none of the effects */
     XCFB_G2D_BLIT_BLEND_ALPHACHANNEL = 0x00000001, /* enables blending and uses
                                                alphachannel from source */
     XCFB_G2D_BLIT_BLEND_COLORALPHA   = 0x00000002, /* enables blending and uses
                                                alpha value from color */
     XCFB_G2D_BLIT_COLORIZE           = 0x00000004, /* modulates source color with
                                                the color's r/g/b values */
     XCFB_G2D_BLIT_SRC_COLORKEY       = 0x00000008, /* don't blit pixels matching the source color key */
     XCFB_G2D_BLIT_DST_COLORKEY       = 0x00000010, /* write to destination only if the destination pixel
                                                matches the destination color key */
     XCFB_G2D_BLIT_SRC_PREMULTIPLY    = 0x00000020, /* modulates the source color with the (modulated)
                                                source alpha */
     XCFB_G2D_BLIT_DST_PREMULTIPLY    = 0x00000040, /* modulates the dest. color with the dest. alpha */
     XCFB_G2D_BLIT_DEMULTIPLY         = 0x00000080, /* divides the color by the alpha before writing the
                                                data to the destination */
     XCFB_G2D_BLIT_DEINTERLACE        = 0x00000100, /* deinterlaces the source during blitting by reading
                                                only one field (every second line of full
                                                image) scaling it vertically by factor two */
     XCFB_G2D_BLIT_SRC_PREMULTCOLOR   = 0x00000200, /* modulates the source color with the color alpha */
     XCFB_G2D_BLIT_XOR                = 0x00000400, /* bitwise xor the destination pixels with the
                                                source pixels after premultiplication */
     XCFB_G2D_BLIT_INDEX_TRANSLATION  = 0x00000800, /* do fast indexed to indexed translation,
                                                this flag is mutual exclusive with all others */
     XCFB_G2D_BLIT_ROTATE90           = 0x00002000, /* rotate the image by 90 degree */
     XCFB_G2D_BLIT_ROTATE180          = 0x00001000, /* rotate the image by 180 degree */
     XCFB_G2D_BLIT_ROTATE270          = 0x00004000, /* rotate the image by 270 degree */
     XCFB_G2D_BLIT_FLIP_HORIZONTAL    = 0x01000000, /* flip the image horizontally */
     XCFB_G2D_BLIT_FLIP_VERTICAL      = 0x02000000, /* flip the image vertically */
     XCFB_G2D_BLIT_LINEAR_STRETCH     = 0x04000000, /* force to use linear stretch */
     XCFB_G2D_BLIT_NEAREST_STRETCH    = 0x08000000, /* force to use nearest neighbour stretch */
     XCFB_G2D_BLIT_A8                 = 0x10000000, /* source is A8 format */
     XCFB_G2D_BLIT_SYNC_YUV           = 0x20000000, /* synchronize YUV operation */
     XCFB_G2D_BLIT_ALT_YUV            = 0x40000000, /* use alternative YUV engine */
     XCFB_G2D_BLIT_ABGR               = 0x80000000, /* convert to ABGR from YUV */
     XCFB_G2D_BLIT_MONO_YUV           = 0x00020000, /* YUV source doesn't have chroma */
} e_xcfb_g2d_blit_flag;

typedef enum {
     XCFB_G2D_DRAW_NOFX               = 0x00000000, /* uses none of the effects */
     XCFB_G2D_DRAW_BLEND              = 0x00000001, /* uses alpha from color */
     XCFB_G2D_DRAW_DST_COLORKEY       = 0x00000002, /* write to destination only if the destination pixel
                                                matches the destination color key */
     XCFB_G2D_DRAW_SRC_PREMULTIPLY    = 0x00000004, /* multiplies the color's rgb channels by the alpha
                                                channel before drawing */
     XCFB_G2D_DRAW_DST_PREMULTIPLY    = 0x00000008, /* modulates the dest. color with the dest. alpha */
     XCFB_G2D_DRAW_DEMULTIPLY         = 0x00000010, /* divides the color by the alpha before writing the
                                                data to the destination */
     XCFB_G2D_DRAW_XOR                = 0x00000020  /* bitwise xor the destination pixels with the
                                                specified color after premultiplication */
} e_xcfb_g2d_draw_flag;

typedef enum {
     XCFB_G2D_BLEND_NONE            = 0,  /*                             */
     XCFB_G2D_BLEND_ZERO               = 1,  /* cf:    0           af:    0 */
     XCFB_G2D_BLEND_ONE                = 2,  /* cf:    1           af:    1 */
     XCFB_G2D_BLEND_SRCCOLOR           = 3,  /* cf:   sc           af:   sa */
     XCFB_G2D_BLEND_INVSRCCOLOR        = 4,  /* cf: 1-sc           af: 1-sa */
     XCFB_G2D_BLEND_SRCALPHA           = 5,  /* cf:   sa           af:   sa */
     XCFB_G2D_BLEND_INVSRCALPHA        = 6,  /* cf: 1-sa           af: 1-sa */
     XCFB_G2D_BLEND_DESTALPHA          = 7,  /* cf:   da           af:   da */
     XCFB_G2D_BLEND_INVDESTALPHA       = 8,  /* cf: 1-da           af: 1-da */
     XCFB_G2D_BLEND_DESTCOLOR          = 9,  /* cf:   dc           af:   da */
     XCFB_G2D_BLEND_INVDESTCOLOR       = 10, /* cf: 1-dc           af: 1-da */
     XCFB_G2D_BLEND_SRCALPHASAT        = 11, /* cf: min(sa, 1-da)  af:    1 */
} e_xcfb_g2d_blend_function;

#define XCFB_PIX_MAKE_FMT(id,abits,cbits,bytes) ((id << 24) | (abits << 16) | (cbits << 8) | bytes)
#define XCFB_PIX_GET_BYTES(fmt) (((fmt) & 0x000000FF))
#define XCFB_PIX_HAS_ALPHA(fmt) (((fmt) & 0x00FF0000) != 0)
#define XCFB_PIX_HAS_COLOR(fmt) (((fmt) & 0x0000FF00) != 0)
#define XCFB_PIX_HAS_INDEX(fmt) (((fmt) & 0x00FFFF00) == 0)
#define XCFB_PIX_IS_RGB(fmt)    (((fmt) >> 24) <= 4)
#define XCFB_PIX_IS_PLANAR(fmt) (((fmt) >> 24) >= 6)

typedef enum {
    XCFB_PIX_FORMAT_ARGB        = XCFB_PIX_MAKE_FMT(0, 8, 24, 4),
    XCFB_PIX_FORMAT_LUT8        = XCFB_PIX_MAKE_FMT(1, 0,  0, 1),
    XCFB_PIX_FORMAT_ARGB1555    = XCFB_PIX_MAKE_FMT(2, 1, 15, 2),
    XCFB_PIX_FORMAT_ARGB4444    = XCFB_PIX_MAKE_FMT(3, 4, 12, 2),
    XCFB_PIX_FORMAT_RGB16       = XCFB_PIX_MAKE_FMT(4, 0, 16, 2),
    XCFB_PIX_FORMAT_AYUV        = XCFB_PIX_MAKE_FMT(5, 8, 24, 4),
    XCFB_PIX_FORMAT_Y420        = XCFB_PIX_MAKE_FMT(6, 0, 12, 1),
    XCFB_PIX_FORMAT_Y422        = XCFB_PIX_MAKE_FMT(7, 0, 16, 1),
    XCFB_PIX_FORMAT_Y420_10     = XCFB_PIX_MAKE_FMT(8, 0, 15, 1),
    XCFB_PIX_FORMAT_Y422_10     = XCFB_PIX_MAKE_FMT(9, 0, 20, 1),
    XCFB_PIX_FORMAT_Y420_12     = XCFB_PIX_MAKE_FMT(10, 0, 18, 1),
    XCFB_PIX_FORMAT_Y422_12     = XCFB_PIX_MAKE_FMT(11, 0, 24, 1),
    XCFB_PIX_FORMAT_Y444        = XCFB_PIX_MAKE_FMT(12, 0, 24, 1),
    XCFB_PIX_FORMAT_Y444_10     = XCFB_PIX_MAKE_FMT(13, 0, 30, 1),
    XCFB_PIX_FORMAT_Y444_12     = XCFB_PIX_MAKE_FMT(14, 0, 36, 1),
} e_xcfb_pixel_format;

typedef enum {
    XCFB_SCR_SCAN_PROGRESSIVE = 0,
    XCFB_SCR_SCAN_INTERLACED,
} e_xcfb_scr_scan_mode;

typedef enum {
    XCFB_SCR_STEREO_2D = 0,
    XCFB_SCR_STEREO_FP = 1,
    XCFB_SCR_STEREO_SS = 2,
    XCFB_SCR_STEREO_TB = 4,
} e_xcfb_scr_stereo_mode;

typedef enum {
    XCFB_SCR_FREQ_60 = 0,
    XCFB_SCR_FREQ_5994,
    XCFB_SCR_FREQ_50,
    XCFB_SCR_FREQ_30,
    XCFB_SCR_FREQ_2997,
    XCFB_SCR_FREQ_25,
    XCFB_SCR_FREQ_24,
    XCFB_SCR_FREQ_2398,
    XCFB_SCR_FREQ_120,
    XCFB_SCR_FREQ_11988,
    XCFB_SCR_FREQ_100,
    XCFB_SCR_FREQ_200,
    XCFB_SCR_FREQ_240,
    XCFB_SCR_FREQ_239_76,
    XCFB_SCR_FREQ_75,
    XCFB_SCR_FREQ_85,
} e_xcfb_scr_frequency;

typedef enum {
    XCFB_SCR_RES_640x480 = 0,
    XCFB_SCR_RES_720x480,
    XCFB_SCR_RES_720x576,
    XCFB_SCR_RES_1440x480,
    XCFB_SCR_RES_1440x576,
    XCFB_SCR_RES_1280x720,
    XCFB_SCR_RES_1920x1080,
    XCFB_SCR_RES_3840x2160,
    XCFB_SCR_RES_4096x2160,
    XCFB_SCR_RES_3840x2160R,

    XCFB_SCR_RES_800x600,
    XCFB_SCR_RES_1024x768,
    XCFB_SCR_RES_1152x864,
    XCFB_SCR_RES_1280x768,
    XCFB_SCR_RES_1280x960,
    XCFB_SCR_RES_1280x1024,
    XCFB_SCR_RES_1400x1050,
    XCFB_SCR_RES_1440x900,
    XCFB_SCR_RES_1680x720,
    XCFB_SCR_RES_1600x1200,
    XCFB_SCR_RES_1920x1200,
    XCFB_SCR_RES_1920x1440,
    XCFB_SCR_RES_2560x1080,
    XCFB_SCR_RES_3440x1440,

    XCFB_SCR_RES_2880x480,
    XCFB_SCR_RES_2880x576,
} e_xcfb_scr_resolution;

#define XCFB_SCR_RESOLUTION_MASK 0xFF000000
#define XCFB_SCR_RESOLUTION_SHIFT 24
#define XCFB_SCR_SCANMODE_MASK 0xFF0000
#define XCFB_SCR_SCANMODE_SHIFT 16
#define XCFB_SCR_FREQUENCY_MASK 0xFF00
#define XCFB_SCR_FREQUENCY_SHIFT 8
#define XCFB_SCR_STEREO_MASK 0xFF
#define XCFB_SCR_STEREO_SHIFT 0

#define XCFB_SCR_MAKE_FMT(resolution, scanmode, freqency, stereo) ((resolution << XCFB_SCR_RESOLUTION_SHIFT) | (scanmode << XCFB_SCR_SCANMODE_SHIFT) | (freqency << XCFB_SCR_FREQUENCY_SHIFT) | (stereo << XCFB_SCR_STEREO_SHIFT))
#define XCFB_SCR_MAKE_2D_FMT(resolution, scanmode, freqency) XCFB_SCR_MAKE_FMT(resolution, scanmode, freqency, XCFB_SCR_STEREO_2D)
#define XCFB_SCR_MAKE_3D_FMT(fmt, stereo) ((fmt) | stereo)

#define XCFB_SCR_GET_RESOLUTION(fmt) ((fmt >> XCFB_SCR_RESOLUTION_SHIFT) & 0xFF)
#define XCFB_SCR_GET_SCANMODE(fmt) ((fmt >> XCFB_SCR_SCANMODE_SHIFT) & 0xFF)
#define XCFB_SCR_GET_FREQUENCY(fmt) ((fmt >> XCFB_SCR_FREQUENCY_SHIFT) & 0xFF)
#define XCFB_SCR_GET_STEREO(fmt) ((fmt >> XCFB_SCR_STEREO_SHIFT) & 0xFF)

#define XCFB_SCR_SET_RESOLUTION(fmt,_x) (fmt = (fmt &~ XCFB_SCR_RESOLUTION_MASK) | (_x << XCFB_SCR_RESOLUTION_SHIFT))
#define XCFB_SCR_SET_SCANMODE(fmt,_x) (fmt = (fmt &~ XCFB_SCR_SCANMODE_MASK) | (_x << XCFB_SCR_SCANMODE_SHIFT))
#define XCFB_SCR_SET_FREQUENCY(fmt,_x) (fmt = (fmt &~ XCFB_SCR_FREQUENCY_MASK) | (_x << XCFB_SCR_FREQUENCY_SHIFT))
#define XCFB_SCR_SET_STEREO(fmt,_x) (fmt = (fmt &~ XCFB_SCR_STEREO_MASK) | (_x << XCFB_SCR_STEREO_SHIFT))

typedef enum {
    XCFB_SCR_FMT_720x480_P_60           = XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_720x480, XCFB_SCR_SCAN_PROGRESSIVE, XCFB_SCR_FREQ_60),
    XCFB_SCR_FMT_1280x720_P_60          = XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_1280x720, XCFB_SCR_SCAN_PROGRESSIVE, XCFB_SCR_FREQ_60),
    XCFB_SCR_FMT_1920x1080_P_60         = XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_1920x1080, XCFB_SCR_SCAN_PROGRESSIVE, XCFB_SCR_FREQ_60),    
	XCFB_SCR_FMT_1920x1080_I_60 		= XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_1920x1080, XCFB_SCR_SCAN_INTERLACED, XCFB_SCR_FREQ_60),
    XCFB_SCR_FMT_3840x2160_P_60         = XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_3840x2160, XCFB_SCR_SCAN_PROGRESSIVE, XCFB_SCR_FREQ_60),
    XCFB_SCR_FMT_4096x2160_P_60         = XCFB_SCR_MAKE_2D_FMT(XCFB_SCR_RES_4096x2160, XCFB_SCR_SCAN_PROGRESSIVE, XCFB_SCR_FREQ_60),
    
    XCFB_SCR_FMT_UNKNOWN                = 0xFFFFFFFF,
} e_xcfb_screen_format;

typedef enum {
    XCFB_SCR_ASP_NONE = 0,
    XCFB_SCR_ASP_DEFAULT,
    XCFB_SCR_ASP_4x3,
    XCFB_SCR_ASP_4x3_LETTERBOX,
    XCFB_SCR_ASP_4x3_PANSCAN,
    XCFB_SCR_ASP_16x9,
    XCFB_SCR_ASP_16x9_LETTERBOX_TOP,
    XCFB_SCR_ASP_16x9_LETTERBOX_CENTER,
    XCFB_SCR_ASP_14x9,
    XCFB_SCR_ASP_14x9_LETTERBOX_TOP,
    XCFB_SCR_ASP_14x9_LETTERBOX_CENTER,
    XCFB_SCR_ASP_16X9_MORE_LETTERBOX_CENTER,
} e_xcfb_screen_aspect_ratio;

typedef enum {
    XCFB_SCR_COL_NONE,
    XCFB_SCR_COL_RGB,               /* IN/OUT */
    XCFB_SCR_COL_RGB_2020,          /* OUT */
    XCFB_SCR_COL_RGB_ADOBE,         /* OUT */
    XCFB_SCR_COL_YUV_470M,          /* IN */
    XCFB_SCR_COL_YUV_SMPT_240M,     /* IN */
    XCFB_SCR_COL_YUV_GENERAL_FILM,  /* IN */    
    XCFB_SCR_COL_YUV_601,           /* IN/OUT */
    XCFB_SCR_COL_YUV_709,           /* IN/OUT */
    XCFB_SCR_COL_YUV_2020,          /* IN/OUT */
    XCFB_SCR_COL_YUV_601_XVYCC,     /* OUT */
    XCFB_SCR_COL_YUV_709_XVYCC,     /* OUT */
    XCFB_SCR_COL_YUV_2020C,         /* OUT */
    XCFB_SCR_COL_YUV_ADOBE,         /* OUT */
    XCFB_SCR_COL_YUV_SYCC601,       /* OUT */
    XCFB_SCR_COL_YUV_AUTO = 0x100,  /* OUT, follow VID0 */
    XCFB_SCR_COL_YUV_AUTO_XVYCC,    /* OUT, follow VID0, enable XVYCC if possible */
} e_xcfb_screen_colorimetry;

typedef enum {
    XCFB_SCR_COL_QUAN_NONE,
    XCFB_SCR_COL_QUAN_LIMITED,
    XCFB_SCR_COL_QUAN_FULL,
} e_xcfb_screen_color_quantization;

typedef enum {
    XCFB_SCR_ITC_NONE,
    XCFB_SCR_ITC_GRAPHIC,
    XCFB_SCR_ITC_PHOTO,
    XCFB_SCR_ITC_CINEMA,
    XCFB_SCR_ITC_GAME,
} e_xcfb_screen_it_content_type;

typedef enum {
    XCFB_SCR_DEP_NONE,
    XCFB_SCR_DEP_8BIT,
    XCFB_SCR_DEP_10BIT,
    XCFB_SCR_DEP_12BIT,
    XCFB_SCR_DEP_8BIT_422,
    XCFB_SCR_DEP_10BIT_422,
    XCFB_SCR_DEP_12BIT_422,
    XCFB_SCR_DEP_8BIT_420,
    XCFB_SCR_DEP_10BIT_420,
    XCFB_SCR_DEP_12BIT_420,
} e_xcfb_screen_color_depth;

typedef enum {
    XCFB_SRC_BUS_NONE = 0,
    XCFB_SRC_BUS_SINGAL,
    XCFB_SRC_BUS_DUAL,
    XCFB_SRC_BUS_TRIPLE,
    XCFB_SRC_BUS_SINGAL_BOTH,
} e_xcfb_screen_bus;

typedef enum {
     XCFB_SRC_ORD_NONE = 0,
     XCFB_SRC_ORD_CBYCR,
     XCFB_SRC_ORD_CRCBY,
     XCFB_SRC_ORD_YCRCB,
     XCFB_SRC_ORD_CBCRY,
     XCFB_SRC_ORD_YCBCR,
     XCFB_SRC_ORD_CRYCB,
} e_xcfb_screen_order;

typedef enum {
    XCFB_SCR_TRANS_NONE = 0,            /* Follow colorimetry */
    XCFB_SCR_TRANS_SRGB,                /* sRGB/sYCC */
    XCFB_SCR_TRANS_BT709,               /* BT.709/BT.601 */
    XCFB_SCR_TRANS_BT2020,              /* BT2020 */
    XCFB_SCR_TRANS_ADOBE,               /* RGB_ADOBE/YCC_ADOBE */
    XCFB_SCR_TRANS_SMPTE2084,           /* SMPTE 2084 */
    XCFB_SCR_TRANS_LINEAR = 0x100,      /* Linear */
} e_xcfb_screen_trans_char;

#define XCFB_SCR_TRANS_LINEAR_NIS(x) ((e_xcfb_screen_trans_char)(XCFB_SCR_TRANS_LINEAR | ((x)/100)))
#define XCFB_SCR_TRANS_LINEAR_IDX(x) ((x) &~ XCFB_SCR_TRANS_LINEAR)

typedef enum {
    XCFB_SCR_OUT_HDMI          = 0x00000001,
    XCFB_SCR_OUT_COMPONENT     = 0x00000002,
    XCFB_SCR_OUT_656           = 0x00000004,
    XCFB_SCR_OUT_COMPOSITE     = 0x00000008,
    XCFB_SCR_OUT_MAX           = 4,
} e_xcfb_screen_output;

typedef enum {
    XCFB_SCR_HD = 0,
    XCFB_SCR_SD,
    XCFB_SCR_MAX,
} e_xcfb_screen_id;

typedef enum {
    XCFB_SCR_MOD_OFF        = 0,
    XCFB_SCR_MOD_ON         = 1,
    XCFB_SCR_MOD_BLANK      = 2,
    XCFB_SCR_MOD_BGCOLOR    = 3,
    XCFB_SCR_MOD_SYNC       = 0x80000000,
} e_xcfb_screen_power_mode;

typedef enum {
    XCFB_LINE_TOP_FLD       = 0x00000001,
    XCFB_LINE_BOT_FLD       = 0x00000002,
    XCFB_LINE_TOP_FLD_END   = 0x00000004,
    XCFB_LINE_BOT_FLD_END   = 0x00000008,
    XCFB_LINE_USER          = 0x00000100,
    XCFB_LINE_SYSTEM        = 0x00000200,
    XCFB_LINE_FLD_MASK      = 0x000000FF,
} e_xcfb_line_type;

typedef enum {
    XCFB_LAY_OPT_ENABLE         = 0x00000001,   /* Enable layer */
    XCFB_LAY_OPT_PREMULTIPLIED  = 0x00000002,   /* Layer color space is premultiplied */
    XCFB_LAY_OPT_ALPHACHANNEL   = 0x00000004,   /* Layer is using alpha channel */
    XCFB_LAY_OPT_COLORKEY       = 0x00000008,   /* Layer has color key */
    XCFB_LAY_OPT_INTERLACED     = 0x00000010,   /* Input is interlaced video */
    XCFB_LAY_OPT_DEINTERLACE    = 0x00000020,   /* Enable backend deinterlacer (only valid if INTERLACED==1 ) */
    XCFB_LAY_OPT_FRAMESCALE     = 0x00000040,   /* Enable frame scale (only valid if INTERLACED==1) */
    XCFB_LAY_OPT_SYNC_FLIP      = 0x00000080,   /* Layer flip should be synced together */
    XCFB_LAY_OPT_DELAY_CONFIG   = 0x00010000,   /* Configurations on layer will delay until previous frames are consumed */
    XCFB_LAY_OPT_CHROMA_UPSCALE = 0x00020000,   /* Apply special filter if interlaced video is converted to progressive */
    XCFB_LAY_OPT_SOLID_COLOR    = 0x00040000,   /* Show solid color instead of picture */
    XCFB_LAY_OPT_STACKED        = 0x00080000,   /* Input field is top/bot stacked stored in one frame (only valid if INTERLACED==1) */
    XCFB_LAY_OPT_FIELD_BASED    = 0x00100000,   /* Input is field based (only valid if INTERLACED==1) */
    XCFB_LAY_OPT_WIDE_GAMUT     = 0x00200000,   /* Input has wide gamut (xvycc) */
    XCFB_LAY_OPT_CHROMAKEY      = 0x00400000,   /* Video layer has chroma key */
    XCFB_LAY_OPT_DYNAMIC_BASE   = 0x00800000,   /* Dynamic base surface is used */
    XCFB_LAY_OPT_DIAGONAL       = 0x01000000,   /* Enable diagonal filter (only valid if INTERLACED==0) */
    XCFB_LAY_OPT_TILED          = 0x02000000,   /* Input is tiled video */
    XCFB_LAY_OPT_MONO_YUV       = 0x04000000,   /* Input doesn't have chroma data */
} e_xcfb_layer_option;

typedef enum {
    XCFB_LAY_ID_GRA0 = 0,
    XCFB_LAY_ID_GRA1 = 1,
    XCFB_LAY_ID_VID0 = 2,
    XCFB_LAY_ID_VID1 = 3,
    XCFB_LAY_ID_SDGRA = 4,
    XCFB_LAY_ID_SDVID = 5,
    XCFB_LAY_ID_MAX,
} e_xcfb_layer_id;

typedef enum {
    XCFB_LAY_FRM_FLAG_NONE       = 0x00000000,
    XCFB_LAY_FRM_FLAG_SYNC       = 0x80000000,
    XCFB_LAY_FRM_FLAG_TOP_FIELD  = 0x40000000,
    XCFB_LAY_FRM_FLAG_BOT_FIELD  = 0x20000000,
    XCFB_LAY_FRM_FLAG_MARKER     = 0x08000000,
    XCFB_LAY_FRM_FLAG_METADATA   = 0x04000000,
    XCFB_LAY_FRM_FLAG_3D_L       = 0x02000000,
    XCFB_LAY_FRM_FLAG_3D_R       = 0x01000000,
} e_xcfb_layer_frame_flag;

typedef enum
{
     XCFB_ANC_DATA_TYPE_NONE    = 0x00000000,
     XCFB_ANC_DATA_TYPE_WSS     = 0x00000001,
     XCFB_ANC_DATA_TYPE_VPS     = 0x00000002,
     XCFB_ANC_DATA_TYPE_CGMS    = 0x00000004,
     XCFB_ANC_DATA_TYPE_CC      = 0x00000010,
     XCFB_ANC_DATA_TYPE_TTX     = 0x00000020,
     XCFB_ANC_DATA_TYPE_VANC    = 0x00000040,
     XCFB_ANC_DATA_TYPE_HANC    = 0x00000080,
     XCFB_ANC_DATA_TYPE_ALL     = 0x000000FF,
} e_xcfb_anc_data_type;

typedef enum {
    XCFB_ANC_TYPE_HANC,
    XCFB_ANC_TYPE_VANC,
    XCFB_ANC_TYPE_MAX,
} e_xcfb_anc_type;

typedef enum {
    XCFB_ANC_NONE_FIELD     = 0x00000000,
    XCFB_ANC_TOP_FIELD      = 0x00000001,
    XCFB_ANC_BOT_FIELD      = 0x00000002,
    XCFB_ANC_BOTH_FIELD     = (XCFB_ANC_TOP_FIELD | XCFB_ANC_BOT_FIELD),
} e_xcfb_anc_field_mode;

typedef enum
{
    XCFB_HDMI_EVENT_NONE            = 0x00000000,
    XCFB_HDMI_EVENT_PLUGGED         = 0x00000001,
    XCFB_HDMI_EVENT_UNPLUGED        = 0x00000002,
    XCFB_HDMI_EVENT_CEC_RECEIVED    = 0x00000010,
    XCFB_HDMI_EVENT_HDCP_SUCCESS    = 0x00000100,
    XCFB_HDMI_EVENT_HDCP_FAIL       = 0x00000200,
    XCFB_HDMI_EVENT_EDID_UPDATED    = 0x00001000,
    XCFB_HDMI_EVENT_RXSENSE_ON      = 0x00010000,
    XCFB_HDMI_EVENT_RXSENSE_OFF     = 0x00020000,
    XCFB_HDMI_EVENT_SCDC_UPDATED    = 0x00040000,
} e_xcfb_hdmi_event;

typedef enum
{
    XCFB_HDMI_HDCP_STATUS_NOT_AUTHENTICATED         = 0,
    XCFB_HDMI_HDCP_STATUS_AUTHENTICATED             = 0x00000001,
    XCFB_HDMI_HDCP_STATUS_KSV_VERIFYING             = 0x00000002,
    XCFB_HDMI_HDCP_STATUS_KSV_INVALID               = 0x00000004,
    XCFB_HDMI_HDCP_STATUS_LINK_INTEGRITY_FAILED     = 0x00000008,
    XCFB_HDMI_HDCP_STATUS_LINK_INTEGRITY_NOT_ACK    = 0x00000010,
    XCFB_HDMI_HDCP_STATUS_CONNECTION_LOST           = 0x00000020,
    XCFB_HDMI_HDCP_STATUS_HDCP22_NOT_CAPABLE        = 0x00000040,
    XCFB_HDMI_HDCP_STATUS_HDCP22_SYNC_LOST          = 0x00000080,
    XCFB_HDMI_HDCP_STATUS_HDCP22_AUTHENTICATION_FAIL= 0x00000100,
    XCFB_HDMI_HDCP_STATUS_DISABLED                  = 0x80000000,
} e_xcfb_hdmi_hdcp_status;

typedef enum
{
    XCFB_HDMI_CEC_IDLE      = 0x00000000,
    XCFB_HDMI_CEC_READABLE  = 0x00000001,
    XCFB_HDMI_CEC_WRITABLE  = 0x00000002,
    XCFB_HDMI_CEC_ERROR     = 0x00000004,
    XCFB_HDMI_CEC_NACK      = 0x00000008,
    XCFB_HDMI_CEC_WAIT      = 0x00000010,
} e_xcfb_hdmi_cec_status;

typedef enum
{
    XCFB_HDMI_EDID_NOT_AVAILABLE = 1,
    XCFB_HDMI_EDID_READING,
    XCFB_HDMI_EDID_READY,
} e_xcfb_hdmi_edid_status;

typedef enum
{
    XCFB_HDMI_SINK_UNKOWN = 0,
    XCFB_HDMI_SINK_HDMI,
    XCFB_HDMI_SINK_DVI,
    XCFB_HDMI_SINK_AUTO,
} e_xcfb_hdmi_sink_type;

typedef enum
{
    XCFB_HDMI_SCAN_NO_DATA = 0,
    XCFB_HDMI_SCAN_OVERSCAN,
    XCFB_HDMI_SCAN_UNDERSCAN,
} e_xcfb_hdmi_scan_info;

typedef enum
{
    XCFB_HDMI_PIC_NO_DATA = 0,
    XCFB_HDMI_PIC_4X3,
    XCFB_HDMI_PIC_16X9,
} e_xcfb_hdmi_picture_aspect_ratio;

typedef enum
{
    XCFB_HDMI_SC_NO_DATA = 0,
    XCFB_HDMI_SC_HORI,
    XCFB_HDMI_SC_VERT,
    XCFB_HDMI_SC_BOTH,
} e_xcfb_hdmi_non_uniform_scaling;

typedef enum
{
    XCFB_HDMI_BAR_NO_DATA = 0,
    XCFB_HDMI_BAR_VERT,
    XCFB_HDMI_BAR_HORI,
    XCFB_HDMI_BAR_BOTH,
} e_xcfb_hdmi_bar_info;

typedef enum
{
    XCFB_HDMI_HDCP_DISABLE = 0,
    XCFB_HDMI_HDCP_1_4,
    XCFB_HDMI_HDCP_2_2,
} e_xcfb_hdmi_hdcp_mode;

typedef enum
{
    XCFB_HDMI_HDR_DISABLE = 0,
    XCFB_HDMI_HDR_AUTO,
    XCFB_HDMI_HDR_MANUAL,
} e_xcfb_hdmi_hdr_mode;

typedef enum {
    XCFB_DBG_LEVEL_DEBUG = 0,
    XCFB_DBG_LEVEL_INFO  = 1,
    XCFB_DBG_LEVEL_WARN  = 2,
    XCFB_DBG_LEVEL_ERROR = 3,
} e_xcfb_debug_level;

typedef enum {
    XCFB_DBG_OBJ_SCREEN  = 0x00000001,
    XCFB_DBG_OBJ_LAYER   = 0x00000010,
    XCFB_DBG_OBJ_HDMI    = 0x00000100,
    XCFB_DBG_OBJ_ANC     = 0x00001000,
    XCFB_DBG_OBJ_V2M     = 0x00001000,
    XCFB_DBG_OBJ_BLANK   = 0x00001000,
    XCFB_DBG_OBJ_G2D     = 0x00010000,
    XCFB_DBG_OBJ_DISP    = 0x00010000,

    XCFB_DBG_MASK_DRV    = 0x00100000,    
    XCFB_DBG_MASK_INT    = 0x00200000,
    XCFB_DBG_MASK_MMIOW  = 0x00400000,
    XCFB_DBG_MASK_MMIOR  = 0x00800000,
    
    XCFB_DBG_MASK_HAL    = 0x01000000,
    XCFB_DBG_MASK_PALLETE= 0x02000000,
    XCFB_DBG_MASK_RAMTAB = 0x02000000,
    XCFB_DBG_MASK_TIMING = 0x04000000,
    XCFB_DBG_MASK_G2D    = 0x08000000,

    XCFB_DBG_MASK_FLIP   = 0x10000000,
    XCFB_DBG_MASK_STANDBY= 0x20000000,
    XCFB_DBG_MASK_HDMIIO = 0x40000000,
    XCFB_DBG_MASK_FORCE  = 0x80000000,
} e_xcfb_debug_object;

#define XCFB_DBG_MAKE_MASK(obj,level) ((obj) << (level))

typedef enum {
    XCFB_ANL_STANDARD_NONE      = 0x00000000,
    XCFB_ANL_STANDARD_PAL_BDGHI = 0x00000001,
    XCFB_ANL_STANDARD_PAL_M     = 0x00000002,
    XCFB_ANL_STANDARD_PAL_N     = 0x00000004,
    XCFB_ANL_STANDARD_PAL_N_C   = 0x00000008,
    XCFB_ANL_STANDARD_NTSC_M    = 0x00000010,
    XCFB_ANL_STANDARD_NTSC_M_442= 0x00000020,
    XCFB_ANL_STANDARD_NTSC_J    = 0X00000040,
} e_xcfb_analog_standard;

typedef enum
{
    XCFB_ANL_MACROV_NONE = 0,
    XCFB_ANL_MACROV_NTSC_TYPE1,
    XCFB_ANL_MACROV_NTSC_TYPE2,
    XCFB_ANL_MACROV_NTSC_TYPE3,
    XCFB_ANL_MACROV_PAL_TYPE123,
    XCFB_ANL_MACROV_NTSC_TEST1,
    XCFB_ANL_MACROV_NTSC_TEST2,
    XCFB_ANL_MACROV_PAL_TEST1,
    XCFB_ANL_MACROV_PAL_TEST2,
    XCFB_ANL_MACROV_480P,
    XCFB_ANL_MACROV_576P,
    XCFB_ANL_MACROV_USER_DEFINED = 100,
} e_xcfb_macrovision_type;

typedef enum
{
    XCFB_SCREEN_EVENT_NONE              = 0x00000000,
    XCFB_SCREEN_EVENT_HD_CHANGED        = 0x00000001,
    XCFB_SCREEN_EVENT_SD_CHANGED        = 0X00000002,
    XCFB_SCREEN_EVENT_HDMI_CHANGED      = 0x00000010,
    XCFB_SCREEN_EVENT_COMPONENT_CHANGED = 0x00000020,
    XCFB_SCREEN_EVENT_656_CHANGED       = 0x00000040,
    XCFB_SCREEN_EVENT_COMPOSITE_CHANGED = 0X00000080,
} e_xcfb_screen_event;

#define XCFB_SCR_MAKE_SCREEN_EVENT(x) (1 << (x))
#define XCFB_SCR_MAKE_OUTPUT_EVENT(x) ((x) << 4)
#define XCFB_SCR_GET_EVENT_SCREEN(x) ((x) >> 1)
#define XCFB_SCR_GET_EVENT_OUTPUT(x) ((x) >> 4)
#define XCFB_SCR_IS_SCREEN_EVENT(x) ((x) & 0xF)
#define XCFB_SCR_IS_OUTPUT_EVENT(x) ((x) & 0xF0)

typedef enum
{
    XCFB_HDMI_PREEMPHASIS_AUTO = 0,
    XCFB_HDMI_PREEMPHASIS_LV_1,
    XCFB_HDMI_PREEMPHASIS_LV_2,
    XCFB_HDMI_PREEMPHASIS_LV_3,
} e_xcfb_hdmi_preemphasis;

typedef enum {
    XCFB_LAY_FILTER_HORI_Y = 0,
    XCFB_LAY_FILTER_VERT_Y,
    XCFB_LAY_FILTER_HORI_C,
    XCFB_LAY_FILTER_VERT_C,
} e_xcfb_layer_filter_pos;

typedef enum {
    XCFB_METADATA_HDR_STATIC = 0,
    XCFB_METADATA_HDR_DYNAMIC,
    XCFB_METADATA_MAX_TYPE,
} e_xcfb_metadata_type;

typedef enum {
    XCFB_LAY_HDR_OPT_NONE           = 0x00000000,
    XCFB_LAY_HDR_OPT_GRAPHICS       = 0x00000001,
    XCFB_LAY_HDR_OPT_PRIMARY        = 0x00000002,
    XCFB_LAY_HDR_OPT_LINEAR         = 0x00000004,
} e_xcfb_layer_hdr_option;

typedef enum {
    XCFB_HDMI_SCDC_NONE             = 0x00000000,
    XCFB_HDMI_SCDC_ENABLE_RR        = 0x00000001,
    XCFB_HDMI_SCDC_ENABLE_POLL      = 0x00000002,
} e_xcfb_hdmi_scdc_mode;

#endif
