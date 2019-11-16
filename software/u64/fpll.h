/*
 * fpll.h
 *
 *  Created on: Dec 15, 2017
 *      Author: gideon
 */

#ifndef FPLL_H_
#define FPLL_H_

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
extern "C" void SetScanMode(int modeIndex);
extern "C" void pllOffsetHz(int Hz);
extern "C" void pllOffsetPpm(int ppm);
extern "C" void SetVideoPll(int mode);
extern "C" void SetHdmiPll(int mode);
extern "C" void SetVideoMode(int mode);
extern "C" void ResetHdmiPll(void);

#endif /* FPLL_H_ */
