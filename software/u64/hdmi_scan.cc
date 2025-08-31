#include "u64.h"
#include "hdmi_scan.h"
#include "color_timings.h"

extern "C" void SetScanModeRegisters(volatile t_video_timing_regs *regs, const TVideoMode *mode)
{
    regs->VID_HSYNCPOL     = mode->hsyncpol;
    regs->VID_HSYNCTIME    = mode->hsync >> 1;
    regs->VID_HBACKPORCH   = mode->hbackporch >> 1;
    regs->VID_HACTIVE      = mode->hactive >> 3;
    regs->VID_HFRONTPORCH  = mode->hfrontporch >> 1;
    regs->VID_HREPETITION  = 0;
    regs->VID_VSYNCPOL     = mode->vsyncpol;
    regs->VID_VSYNCTIME    = mode->vsync;
    regs->VID_VBACKPORCH   = mode->vbackporch;
    regs->VID_VACTIVE      = mode->vactive >> 3;
    regs->VID_VFRONTPORCH  = mode->vfrontporch;
    regs->VID_VIC          = mode->vic;
    regs->VID_REPCN        = mode->pixel_repetition - 1;
    regs->VID_Y            = mode->color_mode;
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

extern "C" void SetVideoMode1080p(t_video_mode mode)
{
    volatile t_video_timing_regs *regs = (volatile t_video_timing_regs *)U64II_HDMI_REGS;

    const TVideoMode pal  =  { 31, 148869856, 1920, 510, 62, 148, 1,  1080, 4, 5, 36, 1, 1, 0 }; // 528,44
    const TVideoMode ntsc =  { 16, 148069608, 1920, 88,  44, 148, 1,  1080, 4, 5, 36, 1, 1, 0 };

    const t_video_color_timing *ct = color_timings[(int)mode];
    if (ct->audio_div == 77) {
        SetScanModeRegisters(regs, &pal);
        SetVicCrop(4, 9, 392, 270);
        regs->scaler_vstretch = 0x02 + 12; // resync, stretch off
    } else {
        SetScanModeRegisters(regs, &ntsc);
        SetVicCrop(4, 0, 392, 240);
        regs->scaler_vstretch = 0x03 + 12; // resync, stretch on
    }
}
