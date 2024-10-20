// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Copyright 2019 Toradex AG
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include <video/of_videomode.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <sound/core.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/of_graph.h>


enum {
	LT8912_AUDIO_NONE,
	LT8912_AUDIO_SPDIF,
	LT8912_AUDIO_I2S
};

struct lt8912 {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct device *dev;
	struct backlight_device *backlight;
	struct mipi_dsi_device *dsi;
	struct device_node *host_node;
	u8 num_dsi_lanes;
	u8 channel_id;
	unsigned int irq;
	enum drm_connector_status hpd_status;
	u8 sink_is_hdmi;
	struct regmap *regmap[3];
	struct gpio_desc *hpd_gpio;
	struct gpio_desc *reset_n;
	struct i2c_adapter *ddc;        /* optional regular DDC I2C bus */
	struct i2c_client *i2c_main;
	u32 lvds_ctr;   // lvds enable
	u32 bypass_scaler;
	u32 bit_color;   // lvds bit  0:rgb888   1:rgb565 
	u32 lvds_mode;   //   0: lvds   1: hdmi  2: hdmi and lvds 
	u32 hdmi_mode;
	struct videomode lvds_vm;
	u8 audio_mode;	 /* selected audio mode - valid only for HDMI output */

	unsigned int f_tmds;
	unsigned int f_audio;
	unsigned int audio_source;
	struct platform_device *audio_pdev;
};

//--------------------------------------------//
// 设置LVDS屏色深
//#define _8_Bit_Color_           // 24 bit
// #define _6_Bit_Color_ // 18 bit

// 定义LVDS输出模式
#define _VESA_
//#define _JEIDA_

//#define _De_mode_
#define _Sync_Mode_

//--------------------------------------------//

#ifdef _VESA_
#define _VesaJeidaMode 0x00
#else
#define _VesaJeidaMode 0x20
#endif

#ifdef _De_mode_
#define _DE_Sync_mode 0x08
#else
#define _DE_Sync_mode 0x00
#endif

// #ifdef _8_Bit_Color_
// #define _ColorDeepth 0x13
// #else
// #define _ColorDeepth 0x17
// #endif

union Temp
{
	u8  Temp8[4];
	u16 Temp16[2];
	u32 Temp32;
}; 

enum                                                                                                                                         
{                                                                                                                                            
    H_act = 0,                                                                                                                               
    V_act,                                                                                                                                   
    H_tol,                                                                                                                                   
    V_tol,                                                                                                                                   
    H_bp,                                                                                                                                    
    H_sync,                                                                                                                                  
    V_sync,                                                                                                                                  
    V_bp                                                                                                                                     
};  


/**
 *  Horizontal display area +  HS Blanking  = HS period time
 *  Vertical display area  +   HS Blanking  =  VS period time
*/

/*
 * test demo
 *  htotal: hactive + hback-porch + hfront-porch + hsync-len
 *           1024      1344   
      1447=       1280       80          70               17      
 *  vtotal: vactive + vback-porch + vfront-porch + vsync-len
 *   823=        800         10           10           3
 *  htotal * vtotal * fps = clock-frequency
 * 
 */

static const struct drm_display_mode default_mode[] = {
	{
		.clock = 148500,
		.hdisplay = 1920,
		.hsync_start = 1920 + 88,
		.hsync_end = 1920 + 88 + 44,
		.htotal = 1920 + 88 + 44 + 148,
		.vdisplay = 1080,
		.vsync_start = 1080 + 36,
		.vsync_end = 1080 + 36 + 5,
		.vtotal = 1080 + 36 + 5 + 4,
	},
	{
		.clock = 74250,
		.hdisplay = 1280,
		.hsync_start = 1280 + 110,
		.hsync_end = 1280 + 110 + 40,
		.htotal = 1280 + 110 + 40 + 220,
		.vdisplay = 720,
		.vsync_start = 720 + 5,
		.vsync_end = 720 + 5 + 5,
		.vtotal = 720 + 5  + 5 + 20,
	}
};

/* alex audio
*/

static void lt8912_calc_cts_n(unsigned int f_tmds, unsigned int fs,
			       unsigned int *cts, unsigned int *n)
{
	switch (fs) {
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		*n = fs * 128 / 1000;
		break;
	case 44100:
	case 88200:
	case 176400:
		*n = fs * 128 / 900;
		break;
	}

	*cts = ((f_tmds * *n) / (128 * fs)) * 1000;
}

static int lt8912_update_cts_n(struct lt8912 *lt8912)
{
	unsigned int cts = 0;
	unsigned int n = 0;
	return 0;
}

enum {
	_32KHz = 0,
	_44d1KHz,
	_48KHz,

	_88d2KHz,
	_96KHz,
	_176Khz,
	_196KHz
};

u16 IIS_N[] =
{
	4096,               // 32K
	6272,               // 44.1K
	6144,               // 48K
	12544,              // 88.2K
	12288,              // 96K
	25088,              // 176K
	24576               // 196K
};

u16 Sample_Freq[] =
{
	0x30,               // 32K
	0x00,               // 44.1K
	0x20,               // 48K
	0x80,               // 88.2K
	0xa0,               // 96K
	0xc0,               // 176K
	0xe0                // 196K
};

int lt8912_hdmi_hw_params(struct device *dev, void *data,
			   struct hdmi_codec_daifmt *fmt,
			   struct hdmi_codec_params *hparms)
{
	struct lt8912 *lt8912 = dev_get_drvdata(dev);
	unsigned int audio_source, i2s_format = 0;
	unsigned int invert_clock;
	unsigned int rate;
	unsigned int len;

	return 0;
}

static int audio_startup(struct device *dev, void *data)
{
//	struct lt8912 *lt8912 = dev_get_drvdata(dev);

	return 0;
}

static void audio_shutdown(struct device *dev, void *data)
{
//	struct lt8912 *lt8912 = dev_get_drvdata(dev);
}

static int lt8912_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
					struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static const struct hdmi_codec_ops lt8912_codec_ops = {
	.hw_params	= lt8912_hdmi_hw_params,
	.audio_shutdown = audio_shutdown,
	.audio_startup	= audio_startup,
	.get_dai_id	= lt8912_hdmi_i2s_get_dai_id,
};

static const struct hdmi_codec_pdata codec_data = {
	.ops = &lt8912_codec_ops,
	.max_i2s_channels = 2,
	.i2s = 1,
	.spdif = 1,
};

int lt8912_audio_init(struct device *dev, struct lt8912 *lt8912)
{
	lt8912->audio_pdev = platform_device_register_data(dev,
					HDMI_CODEC_DRV_NAME,
					PLATFORM_DEVID_AUTO,
					&codec_data,
					sizeof(codec_data));
	return PTR_ERR_OR_ZERO(lt8912->audio_pdev);
}

void lt8912_audio_exit(struct lt8912 *lt8912)
{
	if (lt8912->audio_pdev) {
		platform_device_unregister(lt8912->audio_pdev);
		lt8912->audio_pdev = NULL;
	}
}


static int lt8912_attach_dsi(struct lt8912 *lt);

static inline struct lt8912 *bridge_to_lt8912(struct drm_bridge *b)
{
	return container_of(b, struct lt8912, bridge);
}

static inline struct lt8912 *connector_to_lt8912(struct drm_connector *c)
{
	return container_of(c, struct lt8912, connector);
}

static void lt8912_mipi_timing_config( struct lt8912 *lt )
{
	const struct drm_display_mode *mode = &lt->mode;
	u32 hactive, hfp, hsync, hbp, vfp, vsync, vbp, htotal, vtotal;
	unsigned int hsync_activehigh, vsync_activehigh;

	hactive = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hsync_activehigh = !!(mode->flags & DRM_MODE_FLAG_PHSYNC);
	hbp = mode->htotal - mode->hsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
	vsync_activehigh = !!(mode->flags & DRM_MODE_FLAG_PVSYNC);
	vbp = mode->vtotal - mode->vsync_end;
	htotal = mode->htotal;
	vtotal = mode->vtotal;

	/* MIPIDig */
	// regmap_write(lt->regmap[1], 0x10, 0x01);
	// regmap_write(lt->regmap[1], 0x11, 0x0a);
	regmap_write(lt->regmap[1], 0x18, (u8)(hsync%256));
	regmap_write(lt->regmap[1], 0x19, (u8)(vsync%256));
	regmap_write(lt->regmap[1], 0x1c, (u8)(hactive %256));
	regmap_write(lt->regmap[1], 0x1d, (u8)(hactive/256));

	regmap_write(lt->regmap[1], 0x1e, 0x67);
	regmap_write(lt->regmap[1], 0x2f, 0x0c);
	// regmap_write(lt->regmap[1], 0x2f, 0x0c);

	regmap_write(lt->regmap[1], 0x34, (u8)(htotal%256));
	regmap_write(lt->regmap[1], 0x35, (u8)(htotal/256));
	regmap_write(lt->regmap[1], 0x36, (u8)(vtotal%256));
	regmap_write(lt->regmap[1], 0x37, (u8)(vtotal/256));
	regmap_write(lt->regmap[1], 0x38, (u8)(vbp%256));
	regmap_write(lt->regmap[1], 0x39, (u8)(vbp/256));
	regmap_write(lt->regmap[1], 0x3a, (u8)(vfp % 256));
	regmap_write(lt->regmap[1], 0x3b, (u8)(vfp /256));
	regmap_write(lt->regmap[1], 0x3c, (u8)(hbp % 256));
	regmap_write(lt->regmap[1], 0x3d, (u8)(hbp /256));
	regmap_write(lt->regmap[1], 0x3e, (u8)(hfp % 256));
	regmap_write(lt->regmap[1], 0x3f, (u8)(hfp /256));
}

/* DDSConfig */
static void lt8912_dds_config( struct lt8912 *lt )
{
	regmap_write(lt->regmap[1], 0x4e, 0x52 );
	regmap_write(lt->regmap[1], 0x4f, 0xde );
	regmap_write(lt->regmap[1], 0x50, 0xc0 );
	regmap_write(lt->regmap[1], 0x51, 0x80 );
	regmap_write(lt->regmap[1], 0x51, 0x00 );

	regmap_write(lt->regmap[1], 0x1e, 0x4f );
	regmap_write(lt->regmap[1], 0x1f, 0x5e );
	regmap_write(lt->regmap[1], 0x20, 0x01 );
	regmap_write(lt->regmap[1], 0x21, 0x2c );
	regmap_write(lt->regmap[1], 0x22, 0x01 );
	regmap_write(lt->regmap[1], 0x23, 0xfa );
	regmap_write(lt->regmap[1], 0x24, 0x00 );
	regmap_write(lt->regmap[1], 0x25, 0xc8 );
	regmap_write(lt->regmap[1], 0x26, 0x00 );

	regmap_write(lt->regmap[1], 0x27, 0x5e );
	regmap_write(lt->regmap[1], 0x28, 0x01 );
	regmap_write(lt->regmap[1], 0x29, 0x2c );
	regmap_write(lt->regmap[1], 0x2a, 0x01 );
	regmap_write(lt->regmap[1], 0x2b, 0xfa );
	regmap_write(lt->regmap[1], 0x2c, 0x00 );
	regmap_write(lt->regmap[1], 0x2d, 0xc8 );
	regmap_write(lt->regmap[1], 0x2e, 0x00 );

	regmap_write(lt->regmap[1], 0x42, 0x64 );
	regmap_write(lt->regmap[1], 0x43, 0x00 );
	regmap_write(lt->regmap[1], 0x44, 0x04 );
	regmap_write(lt->regmap[1], 0x45, 0x00 );
	regmap_write(lt->regmap[1], 0x46, 0x59 );
	regmap_write(lt->regmap[1], 0x47, 0x00 );
	regmap_write(lt->regmap[1], 0x48, 0xf2 );
	regmap_write(lt->regmap[1], 0x49, 0x06 );
	regmap_write(lt->regmap[1], 0x4a, 0x00 );
	regmap_write(lt->regmap[1], 0x4b, 0x72 );
	regmap_write(lt->regmap[1], 0x4c, 0x45 );
	regmap_write(lt->regmap[1], 0x4d, 0x00 );
	regmap_write(lt->regmap[1], 0x52, 0x08 );
	regmap_write(lt->regmap[1], 0x53, 0x00 );
	regmap_write(lt->regmap[1], 0x54, 0xb2 );
	regmap_write(lt->regmap[1], 0x55, 0x00 );
	regmap_write(lt->regmap[1], 0x56, 0xe4 );
	regmap_write(lt->regmap[1], 0x57, 0x0d );
	regmap_write(lt->regmap[1], 0x58, 0x00 );
	regmap_write(lt->regmap[1], 0x59, 0xe4 );
	regmap_write(lt->regmap[1], 0x5a, 0x8a );
	regmap_write(lt->regmap[1], 0x5b, 0x00 );
	regmap_write(lt->regmap[1], 0x5c, 0x34 );

	regmap_write(lt->regmap[1], 0x51, 0x00 );
}

void lt8912_lvds_scaler_config( struct lt8912 *lt )
{
	union Temp	Core_PLL_Ratio;
	u32  ColorDeepth;
	u8 temp;
	u32 lvds_hactive, lvds_hfp, lvds_hsync, lvds_hbp, lvds_vfp, lvds_vsync, lvds_vbp, lvds_htotal, lvds_vtotal,lvds_vactive;
	u32 lvds_clock ;

	int  hactive = lt->mode.hdisplay;
	int  vactive = lt->mode.vdisplay;
	// u32 hactive = lt->mode.hactive;

	lvds_hactive = lt->lvds_vm.hactive;
	lvds_vactive = lt->lvds_vm.vactive;
	lvds_hfp = lt->lvds_vm.hfront_porch;
	lvds_hsync = lt->lvds_vm.hsync_len;
	lvds_hbp = lt->lvds_vm.hback_porch;
	lvds_vfp = lt->lvds_vm.vfront_porch;
	lvds_vsync = lt->lvds_vm.vsync_len;
	lvds_vbp = lt->lvds_vm.vback_porch;
	lvds_htotal = lvds_hactive + lvds_hfp + lvds_hsync + lvds_hbp;
	lvds_vtotal = lvds_vactive + lvds_vfp + lvds_vsync + lvds_vbp;
	lvds_clock = lt->lvds_vm.pixelclock/1000;
#if 1
	//  LVDS Output寄存器设置
	//  void LVDS_Scale_Ratio(void)
	//  I2CADR = 0x90;
	regmap_write(lt->regmap[0], 0x80, 0x00 );
	regmap_write(lt->regmap[0], 0x81, 0xff );
	regmap_write(lt->regmap[0], 0x82, 0x03 );

	regmap_write(lt->regmap[0], 0x83, (u8)( hactive % 256 ) );
	regmap_write(lt->regmap[0], 0x84, (u8)( hactive / 256 ) );

	regmap_write(lt->regmap[0], 0x85, 0x80 );
	regmap_write(lt->regmap[0], 0x86, 0x10 );

	regmap_write(lt->regmap[0], 0x87,  (u8)(lvds_htotal % 256	)) ;
	regmap_write(lt->regmap[0], 0x88,  (u8)(lvds_htotal / 256	)) ;
	regmap_write(lt->regmap[0], 0x89,  (u8)(lvds_hsync % 256  )) ;
	regmap_write(lt->regmap[0], 0x8a,  (u8)(lvds_hbp % 256  ) ) ;
	regmap_write(lt->regmap[0], 0x8b,  (u8)((lvds_hbp/256)*0x80+(lvds_vsync%256 ))) ;
	regmap_write(lt->regmap[0], 0x8c,  (u8)( lvds_hactive % 256)) ;
	regmap_write(lt->regmap[0], 0x8d,  (u8)( lvds_vactive % 256)) ;
	regmap_write(lt->regmap[0], 0x8e,  (u8)((lvds_vactive/256)*0x10+(lvds_hactive/256 ))) ;

	Core_PLL_Ratio.Temp32 =  ( ( 100000*( hactive - 1 ) ) / ( lvds_hactive - 1 ) ) * 4096;
	Core_PLL_Ratio.Temp32  = (Core_PLL_Ratio.Temp32 + 50000)/100000;
	temp = (u8)(Core_PLL_Ratio.Temp32&0xff);
	regmap_write(lt->regmap[0], 0x8f, temp) ;
	temp = (u8)((Core_PLL_Ratio.Temp32>>8) & 0xff);
	regmap_write(lt->regmap[0], 0x90, temp) ;

	Core_PLL_Ratio.Temp32 =  ( ( 100000*( vactive - 1 ) ) / ( lvds_vactive - 1 ) ) * 4096;
	Core_PLL_Ratio.Temp32  = (Core_PLL_Ratio.Temp32 + 50000)/100000;
	temp = (u8)(Core_PLL_Ratio.Temp32&0xff);
	regmap_write(lt->regmap[0], 0x91, temp) ;
	temp = (u8)((Core_PLL_Ratio.Temp32>>8) & 0xff);
	regmap_write(lt->regmap[0], 0x92, temp) ;
#endif 

	if(lt->bit_color)
		ColorDeepth = 0x17;  // 18 bit
	else
		ColorDeepth = 0x13;  // 24 bit

	regmap_write(lt->regmap[0], 0x7f, 0x9c) ;//0x9c enable 0x00 disable
	regmap_write(lt->regmap[0], 0xa8, _VesaJeidaMode + _DE_Sync_mode + ColorDeepth );
   
	regmap_write(lt->regmap[0], 0x44, 0x30); // Turn on LVDS output
	usleep_range(300000, 400000);
}

void lt8912_lvds_bypass_config( struct lt8912 *lt )
{
	//lvds power up
	// I2CADR = 0x90;                      //0x90;
	//regmap_write(lt->regmap[0], 0x51, 0x05 );
	u32  ColorDeepth;
	// union Temp  Core_PLL_Ratio;

	//core pll bypass
	regmap_write(lt->regmap[0], 0x50, 0x24 );   //cp=50uA
	regmap_write(lt->regmap[0], 0x51, 0x2d );   //Pix_clk as reference,second order passive LPF PLL
	regmap_write(lt->regmap[0], 0x52, 0x04 );   //loopdiv=0;use second-order PLL
	//PLL CLK
	regmap_write(lt->regmap[0], 0x69, 0x0e );
	regmap_write(lt->regmap[0], 0x69, 0x8e );
	regmap_write(lt->regmap[0], 0x6a, 0x00 );
	regmap_write(lt->regmap[0], 0x6c, 0xb8 );
	regmap_write(lt->regmap[0], 0x6b, 0x51 );

	regmap_write(lt->regmap[0], 0x04, 0xfb ); //core pll reset
	regmap_write(lt->regmap[0], 0x04, 0xff );

	if(lt->bit_color)
		ColorDeepth = 0x17;  // 18 bit
	else
		ColorDeepth = 0x13;  // 24 bit
	//scaler bypass
	regmap_write(lt->regmap[0], 0x7f, 0x00 );  //disable scaler
	regmap_write(lt->regmap[0], 0xa8, _VesaJeidaMode + _DE_Sync_mode + ColorDeepth );
	mdelay( 100 );
	regmap_write(lt->regmap[0], 0x02, 0xf7 );   //lvds pll reset
	regmap_write(lt->regmap[0], 0x02, 0xff );
	regmap_write(lt->regmap[0], 0x03, 0xcb );   //scaler module reset
	regmap_write(lt->regmap[0], 0x03, 0xfb );   //lvds tx module reset
	regmap_write(lt->regmap[0], 0x03, 0xff );
}

static void lt8912_avi_config(struct lt8912 *lt)
{
	unsigned short HDMI_VIC,AVI_PB1,AVI_PB2,AVI_PB0;
	// I2CADR = 0x94;
	regmap_write(lt->regmap[2], 0x3e, 0x0A );

	// 0x43寄存器是checksums，改变了0x45或者0x47 寄存器的值，0x43寄存器的值也要跟着变，
	// 0x43，0x44，0x45，0x47四个寄存器值的总和是0x6F

	//	HDMI_VIC = 0x04; // 720P 60; Corresponding to the resolution to be output
	HDMI_VIC = 0x10;                        // 1080P 60
	//	HDMI_VIC = 0x1F; // 1080P 50
	//	HDMI_VIC = 0x00; // If the resolution is non-standard, set to 0x00

	AVI_PB1 = 0x10;                         // PB1,color space: YUV444 0x70;YUV422 0x30; RGB 0x10
	AVI_PB2 = 0x2A;                         // PB2; picture aspect rate: 0x19:4:3 ;     0x2A : 16:9

	/********************************************************************************
	   The 0x43 register is checksums,
	   changing the value of the 0x45 or 0x47 register,
	   and the value of the 0x43 register is also changed.
	   0x43, 0x44, 0x45, and 0x47 are the sum of the four register values is 0x6F.
	 *********************************************************************************/
	AVI_PB0 = ( ( AVI_PB1 + AVI_PB2 + HDMI_VIC ) <= 0x6f ) ? ( 0x6f - AVI_PB1 - AVI_PB2 - HDMI_VIC ) : ( 0x16f - AVI_PB1 - AVI_PB2 - HDMI_VIC );

	// regmap_write(lt->regmap[2], 0x43, AVI_PB0 );    //avi packet checksum ,avi_pb0
	// regmap_write(lt->regmap[2], 0x44, AVI_PB1 );    //avi packet output RGB 0x10
	// regmap_write(lt->regmap[2], 0x45, AVI_PB2 );    //0x19:4:3 ; 0x2A : 16:9
	// regmap_write(lt->regmap[2], 0x47, HDMI_VIC );   //VIC(as below);1080P60 : 0x10

	regmap_write(lt->regmap[2], 0x3e, 0x0A);
	regmap_write(lt->regmap[2], 0x43, 0x46-HDMI_VIC);
	regmap_write(lt->regmap[2], 0x44, 0x10);
	regmap_write(lt->regmap[2], 0x45, 0x2A); // 0x19 4:3 0x2A:16:9    0x43,0x44,0x45 0x47 the sum of the four register value is 0x6f
	regmap_write(lt->regmap[2], 0x47, 0x00+HDMI_VIC);
}

static void lt8912_audio_config(struct lt8912 *lt)
{
    regmap_write(lt->regmap[0], 0xb2, 0x01); // 0x01:HDMI; 0x00: DVI lt->sink_is_hdmi
	switch(lt->audio_mode) {
		case LT8912_AUDIO_NONE:
			regmap_write(lt->regmap[2], 0x06, 0x00);
			regmap_write(lt->regmap[2], 0x07, 0x00);
			regmap_write(lt->regmap[2], 0x34, 0xd2);
			regmap_write(lt->regmap[2], 0x3c, 0x41);
			break;
		case LT8912_AUDIO_SPDIF:
			regmap_write(lt->regmap[2], 0x06,0x0e);
			regmap_write(lt->regmap[2], 0x07,0x00);
			regmap_write(lt->regmap[2], 0x34,0xD2);
			break;
		case LT8912_AUDIO_I2S:
			regmap_write(lt->regmap[2], 0x06, 0x08);
			regmap_write(lt->regmap[2], 0x07, 0xf0);
			regmap_write(lt->regmap[2], 0x09, 0x00);
			break;
	}

	regmap_write( lt->regmap[2], 0x0f, 0x0b + Sample_Freq[_48KHz]);
	regmap_write( lt->regmap[2],0x37, (u8)( IIS_N[_48KHz] / 0x10000 ) );
	regmap_write( lt->regmap[2],0x36, (u8)( ( IIS_N[_48KHz] & 0x00FFFF ) / 0x100 ) );
	regmap_write( lt->regmap[2],0x35, (u8)( IIS_N[_48KHz] & 0x0000FF ) );

	regmap_write(lt->regmap[2], 0x34, 0xD2);  // D2: 32BIT  E2:16BIT
	regmap_write(lt->regmap[2], 0x3c, 0x41);

//	regmap_write(lt->regmap[2], 0x08, 0x00);
//	regmap_write(lt->regmap[2], 0x07, 0xf0);
//	regmap_write(lt->regmap[2], 0x0f, 0x28); //Audio 16bit, 48K
//	regmap_write(lt->regmap[2], 0x34, 0xe2); //sclk = 64fs, 0xd2; sclk = 32fs, 0xe2.
}

static void lt8912_init(struct lt8912 *lt)
{
	const struct drm_display_mode *mode = &lt->mode;
	u32 hactive, hfp, hsync, hbp, vfp, vsync, vbp, htotal, vtotal,vactive;
	// u32 lvds_hactive, lvds_hfp, lvds_hsync, lvds_hbp, lvds_vfp, lvds_vsync, lvds_vbp, lvds_htotal, lvds_vtotal,lvds_vactive;
	u32 lvds_clock ;

	u8 temp=0;
	union Temp  Core_PLL_Ratio;
	dev_info(lt->dev, DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));

	hactive = mode->hdisplay;
	vactive = mode->vdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	htotal = mode->htotal;
	vtotal = mode->vtotal;

	lvds_clock = lt->lvds_vm.pixelclock/1000;

	// regmap_write(lt->regmap[0], 0x08, 0xff);
	// regmap_write(lt->regmap[0], 0x09, 0xff);
	// regmap_write(lt->regmap[0], 0x0a, 0xff);
	// regmap_write(lt->regmap[0], 0x0b, 0x7c);
	// regmap_write(lt->regmap[0], 0x0c, 0xff);
	// regmap_write(lt->regmap[0], 0x51, 0x15);

	/* DigitalClockEn */
	if(lt->lvds_ctr){
		regmap_write(lt->regmap[0], 0x08, 0xff);
		regmap_write(lt->regmap[0], 0x09, 0xff);
		regmap_write(lt->regmap[0], 0x0a, 0xff);
		regmap_write(lt->regmap[0], 0x0b, 0x7c);
		regmap_write(lt->regmap[0], 0x0c, 0xff);
		regmap_write(lt->regmap[0], 0x51, 0x15);
	}else{
		regmap_write(lt->regmap[0], 0x08, 0xff);
		regmap_write(lt->regmap[0], 0x09, 0x81);
		regmap_write(lt->regmap[0], 0x0a, 0xff);
		regmap_write(lt->regmap[0], 0x0b, 0x64);
		regmap_write(lt->regmap[0], 0x0c, 0xff);
		regmap_write(lt->regmap[0], 0x44, 0x31 );   // Close LVDS ouput
		regmap_write(lt->regmap[0], 0x51, 0x1f);
	}

	/* TxAnalog */
	// if(!lt->lvds_mode) {
		regmap_write(lt->regmap[0], 0x31, 0xa1);
		regmap_write(lt->regmap[0], 0x32, 0xbf);
		regmap_write(lt->regmap[0], 0x33, 0x17);
		regmap_write(lt->regmap[0], 0x37, 0x00);
		regmap_write(lt->regmap[0], 0x38, 0x22);
		regmap_write(lt->regmap[0], 0x60, 0x82);
	// }

	/* CbusAnalog */
	regmap_write(lt->regmap[0], 0x39, 0x45);
	regmap_write(lt->regmap[0], 0x3a, 0x00);
	regmap_write(lt->regmap[0], 0x3b, 0x00);

	/* MIPIAnalog */
	// regmap_write(lt->regmap[0], 0x3e, 0xc6);
	// regmap_write(lt->regmap[0], 0x41, 0x7c);
	regmap_write(lt->regmap[0], 0x3e, 0xc6);
	regmap_write(lt->regmap[0], 0x3f, 0xd4);
	regmap_write(lt->regmap[0], 0x41, 0x7c);

	/* HDMIPllAnalog */
	regmap_write(lt->regmap[0], 0x44, 0x31);
	regmap_write(lt->regmap[0], 0x55, 0x44);
	regmap_write(lt->regmap[0], 0x57, 0x01);
	regmap_write(lt->regmap[0], 0x5a, 0x02);

	/* MipiBasicSet */
	regmap_write(lt->regmap[1], 0x10, 0x01 );               // 0x05
	regmap_write(lt->regmap[1], 0x11, 0x08 );               // 0x12
	regmap_write(lt->regmap[1], 0x12, 0x04 );
	regmap_write(lt->regmap[1], 0x13, lt->num_dsi_lanes % 0x04 );   // 00 4 lane  // 01 lane // 02 2lane                                            //03 3 lane
	regmap_write(lt->regmap[1], 0x14, 0x00 );

	regmap_write(lt->regmap[1], 0x15, 0x00 );
	regmap_write(lt->regmap[1], 0x1a, 0x03 );
	regmap_write(lt->regmap[1], 0x1b, 0x03 );

	/* MIPIDig */
    lt8912_mipi_timing_config(lt);
	
	lt8912_dds_config(lt);

	// regmap_write(lt->regmap[0], 0xb2, 0x01); // 0x01:HDMI; 0x00: DVI lt->sink_is_hdmi

	lt8912_audio_config(lt);

	lt8912_avi_config(lt);

	regmap_write(lt->regmap[0], 0x03, 0x7f );       // mipi rx reset
	mdelay(10);
	regmap_write(lt->regmap[0], 0x03, 0xff );

	// regmap_write(lt->regmap[0], 0x05, 0xfb );       // DDS reset
	// mdelay(10);
	// regmap_write(lt->regmap[0], 0x05, 0xff );

	regmap_write(lt->regmap[1], 0x51, 0x80);
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[1], 0x51, 0x00);

	if(lt->lvds_ctr){
		regmap_write(lt->regmap[0], 0x50, 0x24);
		regmap_write(lt->regmap[0], 0x51, 0x05);
		regmap_write(lt->regmap[0], 0x52, 0x14);
		Core_PLL_Ratio.Temp32 = lvds_clock; //MHZ
		Core_PLL_Ratio.Temp32 = Core_PLL_Ratio.Temp32 * 7 / 25 /1000; // c2
		temp = Core_PLL_Ratio.Temp8[0];
		regmap_write(lt->regmap[0], 0x69, temp) ;
		regmap_write(lt->regmap[0], 0x69,  0x80 + temp) ;
		Core_PLL_Ratio.Temp32 = lvds_clock *7/25;
		Core_PLL_Ratio.Temp32 = (Core_PLL_Ratio.Temp32 - (Core_PLL_Ratio.Temp32/1000)*1000 ) *16384; //E2*1000
		Core_PLL_Ratio.Temp32 = Core_PLL_Ratio.Temp32 /1000;// E2
		temp = Core_PLL_Ratio.Temp32 /256 + 128;
		regmap_write(lt->regmap[0], 0x6c, temp) ;
		temp = Core_PLL_Ratio.Temp32 - (Core_PLL_Ratio.Temp32 /256)*256;
		regmap_write(lt->regmap[0], 0x6b, temp) ;
		regmap_write(lt->regmap[0], 0x04,  0xfb) ;
		regmap_write(lt->regmap[0], 0x04,  0xff) ;
	}
	
	if(lt->bypass_scaler){
		lt8912_lvds_scaler_config(lt);
	}else{
		lt8912_lvds_bypass_config(lt);
	}

	regmap_write(lt->regmap[0], 0x44, 0x30 ); // Turn on LVDS output

}

static void lt8912_wakeup(struct lt8912 *lt)
{
	gpiod_direction_output(lt->reset_n, 1);
	msleep(120);
	gpiod_direction_output(lt->reset_n, 0);

	regmap_write(lt->regmap[0], 0x08,0xff); /* enable clk gating */
	regmap_write(lt->regmap[0], 0x41,0x3c); /* MIPI Rx Power On */
	regmap_write(lt->regmap[0], 0x05,0xfb); /* DDS logical reset */
	regmap_write(lt->regmap[0], 0x05,0xff);
	regmap_write(lt->regmap[0], 0x03,0x7f); /* MIPI RX logical reset */
	usleep_range(10000, 20000);
	regmap_write(lt->regmap[0], 0x03,0xff);
	regmap_write(lt->regmap[0], 0x32,0xa1);
	regmap_write(lt->regmap[0], 0x33,0x03);
}

static void lt8912_sleep(struct lt8912 *lt)
{
	regmap_write(lt->regmap[0], 0x32,0xa0);
	regmap_write(lt->regmap[0], 0x33,0x00); /* Disable HDMI output. */
	regmap_write(lt->regmap[0], 0x41,0x3d); /* MIPI Rx Power Down. */
	regmap_write(lt->regmap[0], 0x08,0x00); /* diable DDS clk. */

	gpiod_direction_output(lt->reset_n, 1);
}

static enum drm_connector_status
lt8912_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt8912 *lt = connector_to_lt8912(connector);
	enum drm_connector_status hpd, hpd_last;
	int timeout = 0;

	if (lt->lvds_mode==0) {
		hpd = connector_status_connected;
	} else {
		do {
			hpd_last = hpd;
			hpd = gpiod_get_value_cansleep(lt->hpd_gpio) ?
				connector_status_connected : connector_status_disconnected;
			msleep(20);
			timeout += 20;
		} while((hpd_last != hpd) && (timeout < 500));
		//dev_info(lt->dev, "lt8912_connector_detect(): %u\n", hpd);
	}
	return hpd;
}

static const struct drm_connector_funcs lt8912_connector_funcs = {
	.detect = lt8912_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static irqreturn_t lt8912_hpd_irq_thread(int irq, void *arg)
{
	struct lt8912 *lt = arg;
	struct drm_connector *connector = &lt->connector;
	//lt8912_init(lt);
	return IRQ_HANDLED;
}

static struct drm_encoder *
lt8912_connector_best_encoder(struct drm_connector *connector)
{
	struct lt8912 *lt = connector_to_lt8912(connector);

	return lt->bridge.encoder;
}

static int lt8912_connector_get_modes(struct drm_connector *connector)
{
	struct lt8912 *lt = connector_to_lt8912(connector);
	// struct edid *edid;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int  ret, num_modes = 0;
	struct display_timings *timings;
	int i;

	if(lt->lvds_mode==0){
		timings = of_get_display_timings(lt->dev->of_node);
		if (timings->num_timings == 0) {
			dev_err(lt->dev, "failed to get display timings from dtb\n");
			return 0;
		}

		for (i = 0; i < timings->num_timings; i++) {
			struct drm_display_mode *mode;
			struct videomode vm;

			if (videomode_from_timings(timings, &vm, i)) {
				continue;
			}

			mode = drm_mode_create(connector->dev);
			drm_display_mode_from_videomode(&vm, mode);
			mode->type = DRM_MODE_TYPE_DRIVER;

			if (timings->native_mode == i)
				mode->type |= DRM_MODE_TYPE_PREFERRED;

			drm_mode_set_name(mode);
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
		if (num_modes == 0) {
			dev_err(lt->dev, "failed to get display modes from dtb\n");
			return 0;
		}
	}else{
		/* hdmi mode*/
		if( num_modes == 0) { /* if not EDID, use dtb timings */

			struct drm_display_mode *mode;
			mode = drm_mode_create(connector->dev);
			if (lt->hdmi_mode > sizeof(default_mode)-1){
				drm_mode_copy(mode,default_mode+0);  //invalid use 1080p
			}else
				drm_mode_copy(mode,default_mode+lt->hdmi_mode);

			mode->type |= DRM_MODE_TYPE_PREFERRED;
			
			drm_mode_set_name(mode);
			drm_mode_probed_add(connector, mode);
			num_modes++;

			if (num_modes == 0) {
				dev_err(lt->dev, "failed to get display modes from dtb\n");
				return 0;
			}
		}
	}

	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_LOW |
					    DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);

	if (ret)
		return ret;

	return num_modes;
}

static enum drm_mode_status lt8912_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;

	if (mode->hdisplay > 1920)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > 1080)
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs lt8912_connector_helper_funcs = {
	.get_modes = lt8912_connector_get_modes,
	.best_encoder = lt8912_connector_best_encoder,
	.mode_valid = lt8912_connector_mode_valid,
};

static void lt8912_bridge_post_disable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_sleep(lt);
}

static void lt8912_bridge_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_init(lt);
}

static void lt8912_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	lt8912_wakeup(lt);
}

static void lt8912_bridge_mode_set(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  const struct drm_display_mode *adj)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);

	drm_mode_copy(&lt->mode, adj);
}

static int lt8912_bridge_attach(struct drm_bridge *bridge,enum drm_bridge_attach_flags flags)
{
	struct lt8912 *lt = bridge_to_lt8912(bridge);
	struct drm_connector *connector = &lt->connector;
	int ret;

	// int type = lt->lvds_mode?DRM_MODE_CONNECTOR_LVDS:DRM_MODE_CONNECTOR_HDMIA;
	
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	ret = drm_connector_init(bridge->dev, connector,
				 &lt8912_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(lt->dev, "failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(connector, &lt8912_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	ret = lt8912_attach_dsi(lt);

	//if (!lt->lvds_mode) {
	//	enable_irq(lt->i2c_main->irq);
	//}
	return ret;
}

static const struct drm_bridge_funcs lt8912_bridge_funcs = {
	.attach = lt8912_bridge_attach,
	.mode_set = lt8912_bridge_mode_set,
//	.pre_enable = lt8912_bridge_pre_enable,
	.enable = lt8912_bridge_enable,
	.post_disable = lt8912_bridge_post_disable,
};

static const struct regmap_config lt8912_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int lt8912_i2c_init(struct lt8912 *lt,
			   struct i2c_client *client)
{
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("lt8912p0", 0x48), },
		{ I2C_BOARD_INFO("lt8912p1", 0x49), },
		{ I2C_BOARD_INFO("lt8912p2", 0x4a), }
	};
	struct regmap *regmap;
	unsigned int i;
	int ret;

	if (!lt || !client)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(info); i++) {
		if (i > 0 ) {
			client = i2c_new_dummy_device(client->adapter, info[i].addr);
			if (!client)
				return -ENODEV;
		}
		regmap = devm_regmap_init_i2c(client, &lt8912_regmap_config);
		if (IS_ERR(regmap)) {
			ret = PTR_ERR(regmap);
			dev_err(lt->dev,
				"Failed to initialize regmap: %d\n", ret);
			return ret;
		}

		lt->regmap[i] = regmap;
	}

	return 0;
}

int lt8912_attach_dsi(struct lt8912 *lt)
{
	struct device *dev = lt->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret = 0;
	const struct mipi_dsi_device_info info = { .type = "lt8912",
						   .channel = lt->channel_id,
						   .node = NULL,
						 };

	host = of_find_mipi_dsi_host_by_node(lt->host_node);
	if (!host) {
		dev_err(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		goto err_dsi_device;
	}

	lt->dsi = dsi;

	dsi->lanes = lt->num_dsi_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		goto err_dsi_attach;
	}

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
err_dsi_device:
	return ret;
}

void lt8912_detach_dsi(struct lt8912 *lt)
{
	mipi_dsi_detach(lt->dsi);
	mipi_dsi_device_unregister(lt->dsi);
}


static int lt8912_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct lt8912 *lt;
	struct device_node *ddc_phandle;
	struct device_node *endpoint;
	struct device_node *time_display;
	unsigned int irq_flags;

	int ret,dsi_lanes=0,bit_color=0;
	u32 lvds_mode=0,audio_mode=0;
	u32 hdmi_mode = 0;
	static int initialize_it = 1;

	if(!initialize_it) {
		initialize_it = 1;
		return -EPROBE_DEFER;
	}

	lt = devm_kzalloc(dev, sizeof(*lt), GFP_KERNEL);
	if (!lt)
		return -ENOMEM;

	lt->i2c_main = i2c;
	lt->dev = dev;
    lt->lvds_ctr = 0;	
	lt->bypass_scaler = 0;
	lt->bit_color = 0;

	of_property_read_u32(dev->of_node, "dsi-lanes", &dsi_lanes);
	of_property_read_u32(dev->of_node, "hdmi_mode", &hdmi_mode);
	of_property_read_u32(dev->of_node, "bit_color", &bit_color);
	of_property_read_u32(dev->of_node, "lvds_mode", &lvds_mode);
	of_property_read_u32(dev->of_node, "audio_mode", &audio_mode);

	lt->num_dsi_lanes = dsi_lanes;
	lt->channel_id = 1;
	lt->hdmi_mode = hdmi_mode;
	lt->bit_color =  bit_color;
	lt->lvds_mode =  lvds_mode;
	lt->audio_mode = audio_mode;

	switch (lvds_mode)
	{
	case 0:
		//lvds
		lt->lvds_ctr=1;
		lt->bypass_scaler=0;
		break;
	case 1:
		//hdmi
		lt->lvds_ctr=0;
		lt->bypass_scaler=0;
		break;
	case 2:
		//lvds and hdmi
		lt->lvds_ctr=1;
		lt->bypass_scaler=1;
		break;
	default:
		break;
	}
	/* get optional regular DDC I2C bus */
	ddc_phandle = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc_phandle) {
		lt->ddc = of_get_i2c_adapter_by_node(ddc_phandle);
		if (!(lt->ddc))
			ret = -EPROBE_DEFER;
		of_node_put(ddc_phandle);
	}

	if (lt->lvds_ctr){
		time_display = of_get_child_by_name(dev->of_node, "display-timings");
		if (time_display) {
			of_node_put(time_display);
			ret = of_get_videomode(dev->of_node, &lt->lvds_vm, 0);
		} 
		if (ret < 0){
			dev_err(dev, "can not found lvds display-timings\n");
			return ret;
		}
	}
#if 0
	if (i2c->irq) {
		ret = devm_request_threaded_irq(dev, i2c->irq, NULL,
						lt8912_hpd_irq_thread,
						IRQF_ONESHOT, dev_name(dev),
						lt);
		if (ret) {
			dev_err(dev, "failed to request irq\n");
			return -ENODEV;
		}
	}

	disable_irq(i2c->irq);
#else
	lt->hpd_gpio = devm_gpiod_get(dev, "hpd", GPIOD_IN);
	if (IS_ERR(lt->hpd_gpio)) {
		dev_err(dev, "failed to get hpd gpio\n");
		return ret;
	}

	lt->irq = gpiod_to_irq(lt->hpd_gpio);
	if (lt->irq == -ENXIO) {
		dev_err(dev, "failed to get hpd irq\n");
		return -ENODEV;
	}
	if (lt->irq < 0) {
		dev_err(dev, "failed to get hpd irq, %i\n", lt->irq);
		return lt->irq;
	}

	irq_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
//	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	ret = devm_request_threaded_irq(dev, lt->irq,
					NULL,
					lt8912_hpd_irq_thread,
					irq_flags, "lt8912_hpd", lt);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return -ENODEV;
	}

	disable_irq(lt->irq);
#endif

	lt->reset_n = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(lt->reset_n)) {
		ret = PTR_ERR(lt->reset_n);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	/*reset lt8912*/
	gpiod_direction_output(lt->reset_n, 0);
	msleep(100);
	gpiod_direction_output(lt->reset_n, 1);
	msleep(100);

	ret = lt8912_i2c_init(lt, i2c);
	if (ret)
		return ret;
		
	printk("hdmi_mode[%d]\n",hdmi_mode);
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	lt->host_node = of_graph_get_remote_port_parent(endpoint);
	if (!lt->host_node) {
		of_node_put(endpoint);
		return -ENODEV;
	}

	of_node_put(endpoint);
	of_node_put(lt->host_node);

	lt->bridge.funcs = &lt8912_bridge_funcs;
	lt->bridge.of_node = dev->of_node;
	drm_bridge_add(&lt->bridge);

	lt8912_audio_init(dev, lt);

	return 0;
}

static int lt8912_remove(struct i2c_client *i2c)
{
	struct lt8912 *lt = i2c_get_clientdata(i2c);

	lt8912_sleep(lt);
	mipi_dsi_detach(lt->dsi);
	drm_bridge_remove(&lt->bridge);

	return 0;
}

static const struct i2c_device_id lt8912_i2c_ids[] = {
	{ "lt8912", 0 },
	{ }
};

static const struct of_device_id lt8912_of_match[] = {
	{ .compatible = "lontium,lt8912" },
	{}
};
MODULE_DEVICE_TABLE(of, lt8912_of_match);

static struct mipi_dsi_driver lt8912_driver = {
	.driver.name = "lt8912",
};

static struct i2c_driver lt8912_i2c_driver = {
	.driver = {
		.name = "lt8912",
		.of_match_table = lt8912_of_match,
	},
	.id_table = lt8912_i2c_ids,
	.probe = lt8912_probe,
	.remove = lt8912_remove,
};

static int __init lt8912_i2c_drv_init(void)
{
	mipi_dsi_driver_register(&lt8912_driver);

	return i2c_add_driver(&lt8912_i2c_driver);
}
module_init(lt8912_i2c_drv_init);

static void __exit lt8912_i2c_exit(void)
{
	i2c_del_driver(&lt8912_i2c_driver);

	mipi_dsi_driver_unregister(&lt8912_driver);
}
module_exit(lt8912_i2c_exit);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Lontium LT8912 MIPI-DSI to LVDS and HDMI/MHL bridge");
MODULE_LICENSE("GPL v2");
