/*
 * fpll.cc
 *
 *  Created on: Nov 29, 2017
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>
#include "u64.h"

#define FPLL_REFERENCES         1
#define FPLL_N_MAX              2
#define FPLL_C_MAX              31
#define FPLL_MIN_VCO_FREQ       600
#define FPLL_MAX_VCO_FREQ       1300
#define FPLL_MFRAC_BITS         32
#define MFRAC_BASE				0x9C76584C
#define DEFAULT_M				12
#define REFERENCE_CLOCK			630559111 // 50000000

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
	FPLL_RECONFIG_MFRAC_K = frac;
	FPLL_RECONFIG_START = 0;
}

extern "C" void pllOffsetHz(int Hz)
{
	// FVCO = 50 MHz * (12+mfrac)
	// offsetVCO = 50 MHz * mfrac
	// Color clock = (9 / 1280) * FVCO
	// Color clock offset = (9 / 1280) * 50 MHz * mfrac = 450 MHz / 1280 * mfrac = 45/128 MHz * mfrac.
	// 2^32 / (45/128 MHz) => 12216,795864178 = step size
	int offset = (Hz * 781875) / 64; // 2^40 / 50 MHz
	uint32_t mfrac = MFRAC_BASE + offset;
	setMFrac(mfrac);
}

extern "C" void pllOffsetPpm(int ppm)
{
	// Normal setting = 0C.9C76584C = 54164609100 / 1000000 = 54164,6091
	int offset = ppm * 54165;
	uint32_t mfrac = MFRAC_BASE + offset;
	setMFrac(mfrac);
}

uint32_t IntToPll(int value)
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

	VID_HSYNCPOL     = mode->hsyncpol;
	VID_HSYNCTIME    = mode->hsync >> 1;
	VID_HBACKPORCH   = mode->hbackporch >> 1;
	VID_HACTIVE      = mode->hactive >> 3;
	VID_HFRONTPORCH  = mode->hfrontporch >> 1;
	VID_HREPETITION  = 0;
	VID_VSYNCPOL     = mode->vsyncpol;
	VID_VSYNCTIME    = mode->vsync;
	VID_VBACKPORCH   = mode->vbackporch;
	VID_VACTIVE      = mode->vactive >> 3;
	VID_VFRONTPORCH  = mode->vfrontporch;
	VID_VIC			 = mode->vic;
	VID_REPCN        = mode->pixel_repetition - 1;
	VID_Y			 = mode->color_mode;

	SetHdmiClock(mode->frequency);

}
