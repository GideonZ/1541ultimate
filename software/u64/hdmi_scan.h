#ifndef HDMI_SCAN_H
#define HDMI_SCAN_H

#include "u64.h"

typedef struct {
    int vic;
    int frequency;
    int hactive;
    int hfrontporch;
    int hsync;
    int hbackporch;
    int hsyncpol;
    int vactive;
    int vfrontporch;
    int vsync;
    int vbackporch;
    int vsyncpol;
    int pixel_repetition;
    int color_mode;
} TVideoMode;

extern "C" void SetScanModeRegisters(volatile t_video_timing_regs *regs, const TVideoMode *mode);
extern "C" void SetVideoMode(t_video_mode mode); // For U64
extern "C" void SetVideoMode1080p(t_video_mode mode, t_hdmi_mode hdmimode); // For U64II

#endif /* HDMI_SCAN_H_ */
