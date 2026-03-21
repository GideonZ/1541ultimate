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
    uint32_t counter_c1_div[9];   // Bank 1  High byte: high time, low byte: low time
    uint32_t counter_c2_div[9];   // Bank 2
    uint32_t counter_m_div;       // HDMI pll regs: "12": "1110" -- div by 33  M
    uint32_t counter_n_div;       // HDMI pll regs: "13": "0505" -- div by 10  N
    uint32_t counter_bypass_hi;   // HDMI pll regs: "14": "0000" -- bypass CNT4-17
    uint32_t counter_bypass_lo;   // HDMI pll regs: "15": "FF00" -- bypass CNT0-3, N, M
    uint32_t counter_odd_hi;      // HDMI pll regs: "16": "0000" -- odd div CNT4-17  (4, 5, 6, 7, ... 17)
    uint32_t counter_odd_lo;      // HDMI pll regs: "17": "0910" -- odd div CNT0-3, N, M, k dither, k ready (bit 11), vdo_div  bit 3..0:  CNT0,1,2,3 (reversed), M bit 4, N bit 5
    uint32_t k_lo;                // HDMI pll regs: "18": "0001" -- k low
    uint32_t k_hi;                // HDMI pll regs: "19": "0000" -- k high
    uint32_t value_2019;          // HDMI pll regs: "1A": "2019"
    uint32_t counter_c1_phase[9]; // Number of VCO counts + 1

    uint32_t unknown_0;           // HDMI pll regs: "24": "0001"
    uint32_t unknown_1;           // HDMI pll regs: "25": "0A00"
    uint32_t unknown_2;           // HDMI pll regs: "26": "F282"
    uint32_t unknown_3;           // HDMI pll regs: "27": "4B00"
    uint32_t unknown_4;           // HDMI pll regs: "28": "0032"
    uint32_t unknown_5;           // HDMI pll regs: "29": "0000"
    uint32_t unknown_6;           // HDMI pll regs: "2A": "3F9C"
    uint32_t unknown_7;           // HDMI pll regs: "2B": "000C"
    uint32_t unknown_8;           // HDMI pll regs: "2C": "0000"
    uint32_t unknown_9;           // HDMI pll regs: "2D": "0300"
    uint32_t unknown_10;          // HDMI pll regs: "2E": "5000"
    uint32_t unknown_11;          // HDMI pll regs: "2F": "0000"
    uint32_t dsm_sel_bw_ctrl;     // HDMI pll regs: "30": "0006" -- DSM out select, bandwidth control (bit 3..0)
    uint32_t chargepump_ctrl;     // HDMI pll regs: "31": "C019" -- chargepump current (bit 2..0)
    uint32_t unknown_12;          // HDMI pll regs: "32": "3060"
} t_pll_direct;


typedef struct {
    uint8_t div_hi;
    uint8_t div_lo;
    uint8_t phase;
} t_pll_counter_def;

class Fpll
{
    volatile t_pll_direct *pll;
    volatile uint32_t *update;
    int indices[9];
    uint32_t bypassLo;
    uint32_t bypassHi;
    uint32_t oddLo;
    uint32_t oddHi;
public:
    uint32_t mFracBase;
    uint32_t mFracPPM;

    Fpll(uint32_t baseAddr, t_pll_counter_def *counters, int numCounters);
    void setC(int index, int div);
    void setN(int div);
    void setM(int div);
    void setK(uint32_t k);
    void execute(void);
};

//extern "C" void SetScanModeRegisters(volatile t_video_timing_regs *regs, const TVideoMode *mode);
//extern "C" void SetVideoMode(t_video_mode mode);
extern "C" void SetScanMode(int modeIndex);
extern "C" void pllOffsetHz(int Hz);
extern "C" void pllOffsetPpm(int ppm);
extern "C" void SetVideoPll(t_video_mode mode, int ppm);
extern "C" void SetHdmiPll(t_video_mode mode);
extern "C" void ResetHdmiPll(void);

#endif /* FPLL_H_ */
