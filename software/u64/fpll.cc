/*
 * fpll.cc
 *
 *  Created on: Nov 29, 2017
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>
#include "u64.h"
#include "fpll.h"
#include "itu.h"
#include "color_timings.h"

#define RECONFIG_LITE

#define FPLL_REFERENCES         1
#define FPLL_N_MAX              2
#define FPLL_C_MAX              31
#define FPLL_MIN_VCO_FREQ       600
#define FPLL_MAX_VCO_FREQ       1300
#define FPLL_MFRAC_BITS         32

// Normal setting PAL  = 12.EAB16BF3 = 81246907379 / 1000000 = 81246,907379 ==> PPM = 81247  (31.5279555556 MHz Analog clock)  (72/512)
// Normal setting NTSC = 13.A2E89059 = 84337528921 / 1000000 = 84337,528921 ==> PPM = 84338  (32.7272727272 MHz Analog clock)  (56/512)
// Alternative setting PAL60  = (32.8987362319 MHz Analog clock) 69/512 = 13.BD3EF255 = 84779397717 / 1000000 = 84779.397717 ==> PPM = 84779
// Alternative setting NTSC50 = (31.5987460745 MHz Analog clock) 58/512 = 12.F5914100 = 81429348609 / 1000000 = 81429.348609 ==> PPM = 81429


#ifndef RECONFIG_LITE
#define FPLL_RECONFIG_REGS      ((volatile uint32_t*)0x92000000)
#define FPLL_RECONFIG_MODE      FPLL_RECONFIG_REGS[0]
#define FPLL_RECONFIG_STATUS    FPLL_RECONFIG_REGS[1]
#define FPLL_RECONFIG_START     FPLL_RECONFIG_REGS[2]
#define FPLL_RECONFIG_N         FPLL_RECONFIG_REGS[3]
#define FPLL_RECONFIG_M         FPLL_RECONFIG_REGS[4]
#define FPLL_RECONFIG_C         FPLL_RECONFIG_REGS[5]
#define FPLL_RECONFIG_DYNSHIFT  FPLL_RECONFIG_REGS[6]
#define FPLL_RECONFIG_MFRAC_K   FPLL_RECONFIG_REGS[7]
#define FPLL_RECONFIG_BANDW     FPLL_RECONFIG_REGS[8]
#define FPLL_RECONFIG_CHARGEP   FPLL_RECONFIG_REGS[9]
#define FPLL_RECONFIG_C0        FPLL_RECONFIG_REGS[10]
#define FPLL_RECONFIG_C1        FPLL_RECONFIG_REGS[11]
#define FPLL_RECONFIG_C2        FPLL_RECONFIG_REGS[12]
#define FPLL_RECONFIG_C3        FPLL_RECONFIG_REGS[13]
#define FPLL_RECONFIG_C4        FPLL_RECONFIG_REGS[14]
#define FPLL_RECONFIG_C5        FPLL_RECONFIG_REGS[15]
#define FPLL_RECONFIG_VCODIV    FPLL_RECONFIG_REGS[28]

#define HDMIPLL_RECONFIG_REGS      ((volatile uint32_t*)0x93000000)
#define HDMIPLL_RECONFIG_MODE      HDMIPLL_RECONFIG_REGS[0]
#define HDMIPLL_RECONFIG_STATUS    HDMIPLL_RECONFIG_REGS[1]
#define HDMIPLL_RECONFIG_START     HDMIPLL_RECONFIG_REGS[2]
#define HDMIPLL_RECONFIG_N         HDMIPLL_RECONFIG_REGS[3]
#define HDMIPLL_RECONFIG_M         HDMIPLL_RECONFIG_REGS[4]
#define HDMIPLL_RECONFIG_C         HDMIPLL_RECONFIG_REGS[5]
#define HDMIPLL_RECONFIG_DYNSHIFT  HDMIPLL_RECONFIG_REGS[6]
#define HDMIPLL_RECONFIG_MFRAC_K   HDMIPLL_RECONFIG_REGS[7]
#define HDMIPLL_RECONFIG_BANDW     HDMIPLL_RECONFIG_REGS[8]
#define HDMIPLL_RECONFIG_CHARGEP   HDMIPLL_RECONFIG_REGS[9]
#define HDMIPLL_RECONFIG_C0        HDMIPLL_RECONFIG_REGS[10]
#define HDMIPLL_RECONFIG_C1        HDMIPLL_RECONFIG_REGS[11]
#define HDMIPLL_RECONFIG_VCODIV    HDMIPLL_RECONFIG_REGS[28]



extern "C" void dumpPllParams(void)
{
	FPLL_RECONFIG_MODE = 0; // no polling
	printf("N counter: %08x\n", FPLL_RECONFIG_N);
	printf("M counter: %08x\n", FPLL_RECONFIG_M);
	printf("M Frac: %08x\n", FPLL_RECONFIG_MFRAC_K);
	printf("C0 counter: %08x\n", FPLL_RECONFIG_C0);
	printf("C1 counter: %08x\n", FPLL_RECONFIG_C1);
	printf("C2 counter: %08x\n", FPLL_RECONFIG_C2);
	printf("C3 counter: %08x\n", FPLL_RECONFIG_C3);
	printf("C4 counter: %08x\n", FPLL_RECONFIG_C4);
	printf("C5 counter: %08x\n", FPLL_RECONFIG_C5);

	HDMIPLL_RECONFIG_MODE = 0; // no polling
	printf("N counter: %08x\n", HDMIPLL_RECONFIG_N);
	printf("M counter: %08x\n", HDMIPLL_RECONFIG_M);
	printf("M Frac: %08x\n", HDMIPLL_RECONFIG_MFRAC_K);
	printf("C0 counter: %08x\n", HDMIPLL_RECONFIG_C0);
	printf("C1 counter: %08x\n", HDMIPLL_RECONFIG_C1);

}

void setMFrac(uint32_t frac)
{
/*
	FPLL_RECONFIG_MFRAC_K = frac;
	FPLL_RECONFIG_START = 0;
*/
}

extern "C" void pllOffsetHz(int Hz)
{
	// FVCO = 50 MHz * (12+mfrac)
	// offsetVCO = 50 MHz * mfrac
	// Color clock = (9 / 1280) * FVCO
	// Color clock offset = (9 / 1280) * 50 MHz * mfrac = 450 MHz / 1280 * mfrac = 45/128 MHz * mfrac.
	// 2^32 / (45/128 MHz) => 12216,795864178 = step size
	int offset = (Hz * 781875) / 64; // 2^40 / 50 MHz
	uint32_t mfrac = MFRAC_BASE_PAL + offset;
	setMFrac(mfrac);
}

extern "C" void pllOffsetPpm(int ppm)
{
	// Normal setting = 0C.9C76584C = 54164609100 / 1000000 = 54164,6091
    // Normal setting = 12.EAB16BF3 = 81246907379 / 1000000 = 81246,9074
    int offset = ppm * 81246;
	uint32_t mfrac = MFRAC_BASE_PAL + offset;
	setMFrac(mfrac);
}

static uint32_t IntToPll(int value)
{
	if(value < 2) {
		return 0x10000; // bypass
	}
	if(value >  510) {
		value = 510;
	}
	uint32_t result = 0; //(value & 1) ? 0x20000 : 0;
	result += (value / 2) << 8;
	value  -= (value / 2);
	result |= value; // << 8);
	return result;
}


extern "C" void SetHdmiClock(int Hz)
{
	// FVCO = REFCLK * M / N
	// FOUT = FVCO / C
	int N = 12;
	// Min C, Max C
	int minC = (FPLL_MIN_VCO_FREQ * 1000000 + Hz - 1) / Hz;
	int maxC = (FPLL_MAX_VCO_FREQ * 1000000) / Hz;

	int C = minC;

	int Fref = REFERENCE_CLOCK / N;
	int MinFrac = (Fref / 10);
	int MaxFrac = (9*Fref / 10);

	int FVco, M, MFrac = 0;
	while ((MFrac < MinFrac) || (MFrac > MaxFrac) || (C & 1)) {
		C += 1;
		FVco = (C * Hz);
		M = FVco / Fref;
		MFrac = FVco % Fref;
	}
	// MFrac is now in the range of 0 to REF_FREQ, but we'll take it to a 32-bit fraction, without using floating point
	unsigned long long frac = ((unsigned long long)MFrac) << 32;
	uint32_t remainder = (uint32_t)(frac / Fref);

	printf("Result of PLL Setting request:\n");
	printf("    Fvco = %9d\n", FVco);
	printf("    M    = %3d - %08x\n", M, IntToPll(M));
	printf("    Mfrc = %08x\n", remainder);
	printf("    C    = %3d - %08x\n", C, IntToPll(C));
	printf("    N    = %3d - %08x\n", N, IntToPll(N));

	HDMIPLL_RECONFIG_MODE = 0; // no polling
	HDMIPLL_RECONFIG_M = IntToPll(M);
	HDMIPLL_RECONFIG_C = IntToPll(C) | (0 << 18);
	HDMIPLL_RECONFIG_N = IntToPll(N);
	HDMIPLL_RECONFIG_MFRAC_K = remainder;
	HDMIPLL_RECONFIG_START = 1;

	U64_HDMI_PLL_RESET = 1;
	U64_HDMI_PLL_RESET = 0;
}

extern "C" void SetVideoPll(int mode)
{
    FPLL_RECONFIG_MODE = 0; // no polling
    if (mode == 0) { // PAL
        FPLL_RECONFIG_M = IntToPll(M_PAL);
        FPLL_RECONFIG_C = IntToPll(AUDIO_DIV_PAL) | (4 << 18);
        FPLL_RECONFIG_C = IntToPll(120) | (3 << 18);
        FPLL_RECONFIG_MFRAC_K = MFRAC_BASE_PAL;
    } else { // NTSC
        FPLL_RECONFIG_M = IntToPll(M_NTSC);
        FPLL_RECONFIG_C = IntToPll(AUDIO_DIV_NTSC) | (4 << 18);
        FPLL_RECONFIG_C = IntToPll(120) | (3 << 18);
        FPLL_RECONFIG_MFRAC_K = MFRAC_BASE_NTSC;
    }
    FPLL_RECONFIG_START = 1;
}

extern "C" void SetHdmiPll(int mode)
{
    HDMIPLL_RECONFIG_MODE = 0; // no polling
    if (mode == 0) { // PAL
        HDMIPLL_RECONFIG_M = IntToPll(24);
        HDMIPLL_RECONFIG_N = IntToPll(7);
    } else { // NTSC
        HDMIPLL_RECONFIG_M = IntToPll(33);
        HDMIPLL_RECONFIG_N = IntToPll(10);
    }
    HDMIPLL_RECONFIG_START = 1;
}


extern "C" void SetScanMode(int modeIndex)
{
    TVideoMode modes[] = {
            { 01, 25175000,  640, 16,  96,  48, 0,   480, 10, 2, 33, 0, 1, 0 },  // VGA 60
            { 00, 34960000,  768, 24,  80, 104, 0,   576,  1, 3, 17, 1, 1, 0 },  // VESA 768x576 @ 60Hz
            { 00, 40000000,  800, 40, 128,  88, 1,   600,  1, 4, 23, 1, 1, 0 },  // VESA 800x600 @ 60Hz
            { 00, 65000000, 1024, 24, 136, 160, 0,   768,  3, 6, 29, 0, 1, 0 },  // VESA 1024x768 @ 60Hz
            { 03, 27000000,  720, 16,  62,  60, 0,   480,  9, 6, 30, 0, 1, 0 },  // VIC 2/3 720x480 @ 60Hz
            { 17, 27000000,  720, 12,  64,  68, 0,   576,  5, 5, 39, 0, 1, 0 },  // VIC 17/18 720x576 @ 50Hz (625 lines)     // according to standard
            { 17, 27067270,  720, 12,  64,  68, 0,   576,  5, 5, 39, 0, 1, 0 },  // VIC 17/18 720x576 @ 50.12Hz (625 lines)  // detuned only
            { 17, 26956800,  720, 12,  64,  68, 0,   576,  4, 5, 39, 0, 1, 0 },  // VIC 17/18 720x576 @ 50Hz (624 lines) (!) // one line less
            { 17, 27023962,  720, 12,  64,  68, 0,   576,  4, 5, 39, 0, 1, 0 },  // VIC 17/18 720x576 @ 50.12Hz (624 lines) (!) // detuned + one line less
            { 23, 27000000, 1440, 24, 126, 138, 0,   288,  2, 3, 19, 0, 2, 0 },  // VIC 23/24 1440x288 @ 50Hz (312 lines) // according to standard
            { 23, 27000000, 1440, 24, 126, 138, 0,   288,  3, 3, 19, 0, 2, 0 },  // VIC 23/24 1440x288 @ 50Hz (313 lines) // according to standard
            { 00, 31527956,  720, 24,  64, 200, 0,   576,  4, 5, 39, 0, 1, 0 },  // Commodore, extended blanking  // As designed (50.12 Hz)
            { 00, 31449600,  720, 24,  64, 200, 0,   576,  4, 5, 39, 0, 1, 1 },  // Commodore, detuned
            { 00, 31527956,  800, 24,  64, 120, 0,   576,  4, 5, 39, 0, 1, 2 },  // Commodore, wide borders as designed (50.12 Hz)
            { 14, 54000000, 1440, 32, 124, 120, 0,   480,  9, 6, 30, 0, 2, 0 },  // VIC 14, 720x480 with double pixels
    };
    TVideoMode *mode = &modes[modeIndex];
    SetScanModeRegisters((volatile t_video_timing_regs *)VID_IO_BASE, mode);
    SetHdmiClock(mode->frequency);
}

#else

static Fpll *videoPll;
static Fpll *hdmiPll;

t_pll_counter_def videoPll_def[6] = {
        { 0x02, 0x01, 0x01 },
        { 0x0F, 0x0F, 0x01 },
        { 0x1E, 0x1E, 0x01 },
        { 0x3C, 0x3C, 0x01 },
        { 0x28, 0x28, 0x01 },
        { 0x08, 0x07, 0x01 },
};

t_pll_counter_def hdmiPll_def[4] = {
        { 0x02, 0x02, 0x03 },
        { 0x04, 0x24, 0x21 },
        { 0x14, 0x14, 0x27 },
        { 0x05, 0x05, 0x03 },
};

Fpll :: Fpll(uint32_t baseAddr, t_pll_counter_def *counters, int numCounters)
{
    pll = (volatile t_pll_direct *)baseAddr;
    update = (volatile uint32_t *)(baseAddr + 0x100);

    for(int i=0; i<9; i++) {
        indices[i] = -1;
    }

    *update = 2; // flush

    for(int i=0; i<numCounters; i++) {
        uint32_t div = (((uint32_t)counters[i].div_hi) << 8) | counters[i].div_lo;
        uint32_t phs = (uint32_t)counters[i].phase;

        for(int k=0; k<9; k++) {
            if (((pll->counter_c1_div[k] & 0xFFFF) == div) && ((pll->counter_c1_phase[k] & 0xFFFF) == phs)) {
                indices[i] = k;
                break;
            }
        }
    }
    bypassLo = pll->counter_bypass_lo;
    bypassHi = pll->counter_bypass_hi;
    oddLo    = pll->counter_odd_lo;
    oddHi    = pll->counter_odd_hi;

    mFracBase = 0;
    mFracPPM  = 0;
}

void Fpll :: setC(int index, int div)
{
    int physicalIndex = indices[index];
    if (physicalIndex < 0) {
        return;
    }

    uint32_t reg = div / 2;
    uint32_t low = (div - reg);
    reg = (reg << 8) | low;

    *update = 8 + (physicalIndex << 4);
    pll->counter_c1_div[physicalIndex] = reg;
    if (physicalIndex < 4) {
        if (div < 2) {
            bypassLo |= (1 << (3-physicalIndex));
        } else {
            bypassLo &= ~(1 << (3-physicalIndex));
        }
        pll->counter_bypass_lo = bypassLo;
        if (div & 1) {
            oddLo |= (1 << (3-physicalIndex));
        } else {
            oddLo &= ~(1 << (3-physicalIndex));
        }
        pll->counter_odd_lo = oddLo;
    } else { // counter 4 or higher
        if (div < 2) {
            bypassHi |= (1 << (17-physicalIndex));
        } else {
            bypassHi &= ~(1 << (17-physicalIndex));
        }
        pll->counter_bypass_hi = bypassHi;
        if (div & 1) {
            oddHi |= (1 << (17 - physicalIndex));
        } else {
            oddHi &= ~(1 << (17 - physicalIndex));
        }
        pll->counter_odd_hi = oddHi;
    }
}

void Fpll :: setN(int div)
{
    uint32_t reg = div / 2;
    uint32_t low = (div - reg);
    reg = (reg << 8) | low;

    pll->counter_n_div = reg;

    if (div < 2) {
        bypassLo |= (1 << 5);
    } else {
        bypassLo &= ~(1 << 5);
    }
    pll->counter_bypass_lo = bypassLo;

    if (div & 1) {
        oddLo |= (1 << 5);
    } else {
        oddLo &= ~(1 << 5);
    }
    pll->counter_odd_lo = oddLo;
}

void Fpll :: setM(int div)
{
    uint32_t reg = div / 2;
    uint32_t low = (div - reg);
    reg = (reg << 8) | low;

    pll->counter_m_div = reg;
    if (div < 2) {
        bypassLo |= (1 << 4);
    } else {
        bypassLo &= ~(1 << 4);
    }
    pll->counter_bypass_lo = bypassLo;

    if (div & 1) {
        oddLo |= (1 << 4);
    } else {
        oddLo &= ~(1 << 4);
    }
    pll->counter_odd_lo = oddLo;
}

void Fpll :: setK(uint32_t k)
{
    pll->counter_odd_lo = oddLo & ~(1 << 11);
    pll->k_lo = k & 0xFFFF;
    pll->k_lo = k & 0xFFFF;
    pll->k_hi = k >> 16;
    pll->k_hi = k >> 16;
    pll->counter_odd_lo = oddLo | (1 << 11);
}

void Fpll :: execute(void)
{
    *update = 1; // burp
    *update = 8 + (5 << 4); // random

    uint32_t a = pll->k_hi;
    uint32_t b = pll->k_lo;
    printf("RB: %04x%04x\n", a, b);
}

extern "C" void SetVideoPll(t_video_mode mode, int ppm)
{
    if (!videoPll) {
        videoPll = new Fpll(0x92000000, videoPll_def, 6);
    }

    const t_video_color_timing *ct = color_timings[(int)mode];
    videoPll->setM(ct->m);
    videoPll->setK(ct->frac);
    videoPll->setC(4, ct->audio_div);
    videoPll->mFracBase = ct->frac;
    videoPll->mFracPPM = ct->ppm;
    videoPll->execute();
}

extern "C" void SetHdmiPll(t_video_mode mode)
{
    if (!hdmiPll) {
        hdmiPll = new Fpll(0x93000000, hdmiPll_def, 4);
    }

    const t_video_color_timing *ct = color_timings[(int)mode];
    hdmiPll->setM((ct->audio_div == 77) ? 24 : 33);
    hdmiPll->setN((ct->audio_div == 77) ?  7 : 10);
    hdmiPll->execute();
}

extern "C" void pllOffsetPpm(int ppm)
{
    if (!videoPll) {
        return;
    }

    int offset = ppm * videoPll->mFracPPM;
    uint32_t mfrac = videoPll->mFracBase + offset;
    videoPll->setK(mfrac);
    videoPll->execute();
}

#endif // Reconfig Lite

extern "C" void ResetHdmiPll(void)
{
    U64_HDMI_PLL_RESET = 1;
    U64_HDMI_PLL_RESET = 0;
}
