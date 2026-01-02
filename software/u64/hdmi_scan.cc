#include "u64.h"
#include "hdmi_scan.h"
#include "color_timings.h"
#include <stdio.h>

extern "C" void SetScanModeRegisters(volatile t_video_timing_regs *regs, const TVideoMode *mode)
{
    regs->VID_HSYNCPOL     = mode->hsyncpol;
    regs->VID_HSYNCTIME    = mode->hsync >> 1;
    regs->VID_HBACKPORCH   = mode->hbackporch >> 2;
    regs->VID_HACTIVE      = mode->hactive >> 3;
    regs->VID_HFRONTPORCH  = mode->hfrontporch >> 2;
    regs->VID_VSYNCPOL     = mode->vsyncpol;
    regs->VID_VSYNCTIME    = mode->vsync;
    regs->VID_VBACKPORCH   = mode->vbackporch;
    regs->VID_VACTIVE      = mode->vactive >> 3;
    regs->VID_VFRONTPORCH  = mode->vfrontporch;
    regs->VID_VIC          = mode->vic;
    regs->VID_REPCN        = mode->pixel_repetition - 1;
    regs->VID_Y            = mode->color_mode;

    uint8_t rep = (mode->pixel_repetition > 1) ? 1 : 0;
    rep |= uint8_t(mode->hsync & 1) << 1;
    rep |= uint8_t(mode->hactive & 6) << 1;
    rep |= uint8_t(mode->hbackporch & 3) << 4;
    rep |= uint8_t(mode->hfrontporch & 3) << 6;
    regs->VID_HREPETITION  = rep;
}

extern "C" void SetVideoMode(t_video_mode mode)
{
    volatile t_video_timing_regs *regs = (volatile t_video_timing_regs *)VID_IO_BASE;

    const TVideoMode pal  =  { 17, 27023962,  720, 12,  64,  68, 0,   576,  4, 5, 39, 0, 1, 0 };  // VIC 17/18 720x576 @ 50.12Hz (624 lines) (!) // detuned + one line less
    const TVideoMode ntsc =  { 03, 27000000,  720, 16,  62,  60, 0,   480,  9, 6, 31, 0, 1, 0 };  // VIC 2/3 720x480 @ 60Hz

    const t_video_color_timing *ct = color_timings[(int)mode];
    if (ct->audio_div == 77) {
        SetScanModeRegisters(regs, &pal);
    } else {
        SetScanModeRegisters(regs, &ntsc);
    }
}

typedef struct {
    uint8_t offset_x;
    uint8_t offset_y;
    uint8_t size_x;
    uint8_t size_y;
} t_vic_crop_regs;

extern "C" void SetVicCrop(int x, int y, int x_size, int y_size)
{
    volatile t_vic_crop_regs *crop = (volatile t_vic_crop_regs *)U64II_CROPPER_BASE;

    crop->offset_x = x;
    crop->offset_y = y;
    crop->size_x   = x_size >> 1;
    crop->size_y   = y_size >> 1;
}

void SetInternalPLL(const t_intpll_mode mode);
void SetExternalPLL(const uint8_t *blob);

extern "C" void SetVideoMode1080p(t_video_mode mode, t_hdmi_mode hdmimode)
{
    volatile t_video_timing_regs *regs = (volatile t_video_timing_regs *)U64II_HDMI_REGS;
    
    const t_video_color_timing *ct = color_timings[(int)mode];
    bool pal = (ct->audio_div == 77);

    const TVideoMode sm_1080p50 =  { 31, 148869856, 1920, 528, 44, 148, 1,  1080,  4, 5, 36, 1, 1, 0 }; // 16:9
    const TVideoMode sm_1080p60 =  { 16, 148069608, 1920, 88,  44, 148, 1,  1080,  4, 5, 36, 1, 1, 0 }; // 16:9
    const TVideoMode sm_576p50  =  { 17,  27067270,  720, 12,  64,  68, 0,   576,  5, 5, 39, 0, 1, 0 }; // 4:3
    const TVideoMode sm_480p60  =  {  1,  25126964,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 }; // 4:3
    const TVideoMode sm_720p50  =  { 19,  74434928, 1280, 440, 40, 220, 1,   720,  5, 5, 20, 1, 1, 0 }; // 16:9
    const TVideoMode sm_720p60  =  {  4,  74034804, 1280, 110, 40, 220, 1,   720,  5, 5, 20, 1, 1, 0 }; // 16:9

    const TVideoMode sm_800x600x50  =  {  0,  34944000,  800, 40, 112,  88, 1,   600, 30, 5, 37, 1, 1, 0 }; // 4:3  (1040 x 672)
    const TVideoMode sm_800x600x60  =  {  0,  41028000,  800, 40, 124,  88, 1,   600, 20, 5, 25, 1, 1, 0 }; // 4:3  (1052 x 650)

    const TVideoMode sm_1024x768x50 =  {  0,  54163200,  1024, 24, 136, 160, 0, 768,  3, 6, 29, 0, 1, 0 }; // 4:3  (1344 x 806)
    const TVideoMode sm_1024x768x60 =  {  0,  64995840,  1024, 24, 136, 160, 0, 768,  3, 6, 29, 0, 1, 0 }; // 4:3  (1344 x 806)

    const TVideoMode sm_1280x1024x50 = {  0,  92164800,  1280, 48, 112, 248, 1, 1024, 27, 3, 38, 1, 1, 0 }; // 5:4 Frontporch 1 → 27 lines
    const TVideoMode sm_1280x1024x60 = {  0, 106546560,  1280, 48, 112, 248, 1, 1024,  1, 3, 24, 1, 1, 0 }; // 5:4 Backporch 38 → 24 lines

    const uint8_t ext_pll_640x480[]      = { 0x57, 0x00, 0x01, 0x01, 0x20, 0xd7, 0xbb, 0xf1 }; // 525 / 263 (gear 5)
    const uint8_t ext_pll_720x576[]      = { 0x57, 0x00, 0x01, 0x01, 0x07, 0xd1, 0x82, 0x2d }; // 125 / 56 (gear 5)
    const uint8_t ext_pll_800x600x60[]   = { 0x57, 0x00, 0x01, 0x01, 0x00, 0x50, 0x02, 0x8c }; // 5/2 (gear 2)
    const uint8_t ext_pll_800x600x50[]   = { 0x57, 0x00, 0x02, 0x02, 0x02, 0x80, 0x3a, 0x29 }; // 20/9 (gear 2)

    // 57 00 03 03 51 66 9a 6b
    // const uint8_t ext_pll_1024x768x60[]  = { 0x57, 0x00, 0x02, 0x02, 0x44, 0x45, 0x02, 0x09 }; // 546 / 263
    const uint8_t ext_pll_1024x768x60[]  = { 0x57, 0x00, 0x03, 0x03, 0x51, 0x66, 0x9a, 0x6b }; // 434 / 263 
    const uint8_t ext_pll_1024x768x50[]  = { 0x57, 0x00, 0x01, 0x01, 0x01, 0xf0, 0x2b, 0x6c }; // 31 / 9 (gear 2)

    const uint8_t ext_pll_1280x1024x60[] = { 0x57, 0x00, 0x01, 0x01, 0x0d, 0x31, 0xfb, 0x2c }; // 211 / 65
    const uint8_t ext_pll_1280x1024x50[] = { 0x57, 0x00, 0x01, 0x01, 0x0d, 0x31, 0x02, 0xec }; // 211 / 72
    const uint8_t ext_pll_1920x1280x50[] = { 0x57, 0x00, 0x01, 0x01, 0x0F, 0xA2, 0xCA, 0xAD }; // 250 / 91
    const uint8_t ext_pll_1920x1280x60[] = { 0x57, 0x00, 0x01, 0x01, 0x55, 0xf7, 0x82, 0x89 }; // 1375 / 263

    // Horizontal Scaler:
    //       384	 400
    // -----------------
    // 00	 384	 400
    // 01	 480	 500
    // 02	 512	 533
    // 03	 640	 667
    // 04	 720	 750
    // 08	 768	 800
    // 09	 960	1000
    // 0A	1024	1067
    // 0B	1280	1333
    // 0C	1440	1500

    // Vertical Scaler
    //       240	 270
    // -----------------
    // 00	 480	 540
    // 10	 533	 600
    // 11	 540	 608
    // 12	 600	 675
    // 13	 640	 720
    // 14	 686	 771
    // 15	 720	 810
    // 16	 768	 864
    // 17	 864	 972
    // 08	 960	1080
    // 18	1067	1200
    // 19	1080	1215
    // 1A	1200	1350
    // 1B	1280	1440
    // 1C	1371	1543
    // 1D	1440	1620
    // 1E	1536	1728
    // 1F	1728	1944

#define AVI_4_3   { regs->VID_A = 1; regs->VID_R = 9;  regs->VID_M = 1; regs->VID_Y = 0; }
#define AVI_16_9  { regs->VID_A = 1; regs->VID_R = 10; regs->VID_M = 2; regs->VID_Y = 1; }

    switch(hdmimode) {
    case e_480p_576p:
        AVI_4_3;
        if (pal) {
            SetInternalPLL(e_INTPLL_25_13);
            SetExternalPLL(ext_pll_720x576);
            regs->gearbox = 1;
            SetScanModeRegisters(regs, &sm_576p50);
            SetVicCrop(9, 0, 383, 288);
            regs->hscaler = 0x04;
            regs->vscaler = 0x00;
            regs->x_offset = 0;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_25_13);
            SetExternalPLL(ext_pll_640x480);
            regs->gearbox = 1;
            SetScanModeRegisters(regs, &sm_480p60);
            SetVicCrop(8, 0, 383, 240);
            regs->hscaler = 0x03;
            regs->vscaler = 0x00;
            regs->x_offset = 0;
            regs->resync = 2;
        }
        break;
    case e_800x600:
        AVI_4_3;
        if (pal) {
            SetInternalPLL(e_INTPLL_1x);
            SetExternalPLL(ext_pll_800x600x50);
            regs->gearbox = 2;
            SetScanModeRegisters(regs, &sm_800x600x50);
            SetVicCrop(0, 9, 398, 270);
            regs->hscaler = 0x08;
            regs->vscaler = 0x10;
            regs->x_offset = 0;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_1x);
            SetExternalPLL(ext_pll_800x600x60);
            regs->gearbox = 2;
            SetScanModeRegisters(regs, &sm_800x600x60);
            SetVicCrop(0, 0, 398, 240);
            regs->hscaler = 0x08;
            regs->vscaler = 0x12;
            regs->x_offset = 0;
            regs->resync = 2;
        }
        break;
    case e_1024x768:
        AVI_4_3;
        if (pal) {
            SetInternalPLL(e_INTPLL_1x);
            SetExternalPLL(ext_pll_1024x768x50);
            regs->gearbox = 2;
            SetScanModeRegisters(regs, &sm_1024x768x50);
            SetVicCrop(9, 10, 382, 268);
            regs->hscaler = 0x0A;
            regs->vscaler = 0x14;
            regs->x_offset = 0;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_6_5);
            SetExternalPLL(ext_pll_1024x768x60);
            regs->gearbox = 0;
            SetScanModeRegisters(regs, &sm_1024x768x60);
            SetVicCrop(9, 0, 382, 240);
            regs->hscaler = 0x0A;
            regs->vscaler = 0x16;
            regs->x_offset = 0;
            regs->resync = 2;
        }
        break;
    case e_1280x720:
        AVI_16_9;
        if (pal) {
            SetInternalPLL(e_INTPLL_55_32);
            SetExternalPLL(ext_pll_1920x1280x50);
            regs->gearbox = 2;
            SetScanModeRegisters(regs, &sm_720p50);
            SetVicCrop(8, 9, 384, 270);
            regs->hscaler = 0x09;
            regs->vscaler = 0x13;
            regs->x_offset = 160;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_45_52);
            SetExternalPLL(ext_pll_1920x1280x60);
            regs->gearbox = 2;
            SetScanModeRegisters(regs, &sm_720p60);
            SetVicCrop(8, 0, 384, 240);
            regs->hscaler = 0x09;
            regs->vscaler = 0x15;
            regs->x_offset = 160;
            regs->resync = 2;
        }
        break;
    case e_1280x1024:
        AVI_4_3;
        if (pal) {
            SetInternalPLL(e_INTPLL_1x);
            SetExternalPLL(ext_pll_1280x1024x50);
            regs->gearbox = 0;
            SetScanModeRegisters(regs, &sm_1280x1024x50);
            SetVicCrop(9, 4, 382, 280);
            regs->hscaler = 0x0B;
            regs->vscaler = 0x17;
            regs->x_offset = 0;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_1x);
            SetExternalPLL(ext_pll_1280x1024x60);
            regs->gearbox = 0;
            SetScanModeRegisters(regs, &sm_1280x1024x60);
            SetVicCrop(9, 0, 382, 240);
            regs->hscaler = 0x0B;
            regs->vscaler = 0x08;
            regs->x_offset = 0;
            regs->resync = 2;
        }
        break;
    case e_1920x1080:
        AVI_16_9;
        if (pal) {
            SetInternalPLL(e_INTPLL_55_32);
            SetExternalPLL(ext_pll_1920x1280x50);
            regs->gearbox = 0;
            SetScanModeRegisters(regs, &sm_1080p50);
            SetVicCrop(8, 9, 384, 270);
            regs->hscaler = 0x0C;
            regs->vscaler = 0x08;
            regs->x_offset = 240;
            regs->resync = 2;
        } else {
            SetInternalPLL(e_INTPLL_45_52);
            SetExternalPLL(ext_pll_1920x1280x60);
            regs->gearbox = 0;
            SetScanModeRegisters(regs, &sm_1080p60);
            SetVicCrop(8, 0, 384, 240);
            regs->hscaler = 0x0C;
            regs->vscaler = 0x19;
            regs->x_offset = 240;
            regs->resync = 2;
        }
        break;
    default:
        printf("Unknown Scan mode requested.\n");
        break;
    }
}
