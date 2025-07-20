/*
 * u64_config.cc
 *
 *  Created on: Oct 6, 2017
 *      Author: Gideon
 */

#include "integer.h"
extern "C" {
    #include "itu.h"
	#include "dump_hex.h"
    #include "small_printf.h"
    #include "sid_coeff.h"
}
#include <string.h>
#include "flash.h"
#include "product.h"
#include "userinterface.h"
#include "u64_config.h"
#include "audio_select.h"
#include "fpll.h"
#include "i2c_drv.h"
#include "overlay.h"
#include "u2p.h"
//#include "sys/alt_irq.h"
#include "c64.h"
#include "esp32.h"
#include "sid_editor.h"
#include "sid_device_fpgasid.h"
#include "sid_device_swinsid.h"
#include "sid_device_armsid.h"
#include "init_function.h"
#include "color_timings.h"
#include "hdmi_scan.h"

const uint8_t default_colors[16][3] = {
    { 0x00, 0x00, 0x00 },
    { 0xF7, 0xF7, 0xF7 },
    { 0x8D, 0x2F, 0x34 },
    { 0x6A, 0xD4, 0xCD },
    { 0x98, 0x35, 0xA4 },
    { 0x4C, 0xB4, 0x42 },
    { 0x2C, 0x29, 0xB1 },
    { 0xEF, 0xEF, 0x5D },
    { 0x98, 0x4E, 0x20 },
    { 0x5B, 0x38, 0x00 },
    { 0xD1, 0x67, 0x6D },
    { 0x4A, 0x4A, 0x4A },
    { 0x7B, 0x7B, 0x7B },
    { 0x9F, 0xEF, 0x93 },
    { 0x6D, 0x6A, 0xEF },
    { 0xB2, 0xB2, 0xB2 } };

// static pointer
U64Config u64_configurator;

// Semaphore set by interrupt
static SemaphoreHandle_t resetSemaphore;

#define STORE_PAGE_ID 0x55363443

#define CFG_SCANLINES         0x01
#define CFG_SID1_ADDRESS      0x02
#define CFG_SID2_ADDRESS      0x03
#define CFG_EMUSID1_ADDRESS   0x04
#define CFG_EMUSID2_ADDRESS   0x05
#define CFG_AUDIO_LEFT_OUT	  0x06
#define CFG_AUDIO_RIGHT_OUT	  0x07
#define CFG_COLOR_CLOCK_ADJ   0x08
#define CFG_ANALOG_OUT_SELECT 0x09
#define CFG_CHROMA_DELAY      0x0A
#define CFG_COLOR_CODING      0x0B
#define CFG_HDMI_ENABLE       0x0C
#define CFG_SID1_TYPE		  0x0D
#define CFG_SID2_TYPE		  0x0E
#define CFG_PADDLE_EN		  0x0F
#define CFG_STEREO_DIFF	      0x10
#define CFG_PARCABLE_ENABLE   0x11
#define CFG_PLAYER_AUTOCONFIG 0x12
#define CFG_ALLOW_EMUSID      0x13
#define CFG_EMUSID1_FILTER    0x14
#define CFG_EMUSID2_FILTER    0x15
#define CFG_EMUSID1_WAVES     0x16
#define CFG_EMUSID2_WAVES     0x17
#define CFG_EMUSID1_RESONANCE 0x18
#define CFG_EMUSID2_RESONANCE 0x19
#define CFG_SID1_SHUNT        0x1A
#define CFG_SID2_SHUNT        0x1B
#define CFG_SID1_CAPS         0x1C
#define CFG_SID2_CAPS         0x1D
#define CFG_EMUSID1_DIGI      0x1E
#define CFG_EMUSID2_DIGI      0x1F

#define CFG_MIXER0_VOL        0x20
#define CFG_MIXER1_VOL        0x21
#define CFG_MIXER2_VOL        0x22
#define CFG_MIXER3_VOL        0x23
#define CFG_MIXER4_VOL        0x24
#define CFG_MIXER5_VOL        0x25
#define CFG_MIXER6_VOL        0x26
#define CFG_MIXER7_VOL        0x27
#define CFG_MIXER8_VOL        0x28
#define CFG_MIXER9_VOL        0x29

#define CFG_MIXER0_PAN        0x30
#define CFG_MIXER1_PAN        0x31
#define CFG_MIXER2_PAN        0x32
#define CFG_MIXER3_PAN        0x33
#define CFG_MIXER4_PAN        0x34
#define CFG_MIXER5_PAN        0x35
#define CFG_MIXER6_PAN        0x36
#define CFG_MIXER7_PAN        0x37
#define CFG_MIXER8_PAN        0x38
#define CFG_MIXER9_PAN        0x39

#define CFG_SPEAKER_EN        0x40
#define CFG_SYSTEM_MODE       0x41
#define CFG_LED_SELECT_0      0x42
#define CFG_LED_SELECT_1      0x43
#define CFG_SPEAKER_VOL       0x44
#define CFG_SOCKET1_ENABLE    0x45
#define CFG_SOCKET2_ENABLE    0x46
#define CFG_EMUSID_SPLIT      0x47
#define CFG_SHOW_SID_ADDR     0x48
#define CFG_JOYSWAP           0x49
#define CFG_AUTO_MIRRORING    0x4A
// #define CFG_CART_PREFERENCE   0x4B // moved to C64 for user experience consistency
#define CFG_SPEED_REGS        0x4C
#define CFG_IEC_BURST_EN      0x4D
#define CFG_PALETTE           0x4E
#define CFG_IEC_BUS_MODE      0x4F
#define CFG_USERPORT_EN       0x50

#define CFG_SPEED_PREF        0x52
#define CFG_BADLINES_EN       0x53
#define CFG_SUPERCPU_DET      0x54

#define CFG_SCAN_MODE_TEST    0xA8
#define CFG_VIC_TEST          0xA9

uint8_t C64_EMUSID1_BASE_BAK;
uint8_t C64_EMUSID2_BASE_BAK;
uint8_t C64_SID1_BASE_BAK;
uint8_t C64_SID2_BASE_BAK;

uint8_t C64_EMUSID1_MASK_BAK;
uint8_t C64_EMUSID2_MASK_BAK;
uint8_t C64_SID1_MASK_BAK;
uint8_t C64_SID2_MASK_BAK;
uint8_t C64_EMUSID_SPLIT_BAK;

uint8_t C64_STEREO_ADDRSEL_BAK;
uint8_t C64_SID1_EN_BAK;
uint8_t C64_SID2_EN_BAK;

#define UNMAPPED_BASE 0x01
#define UNMAPPED_MASK 0xFE

#define SID_TYPE_NONE    0
#define SID_TYPE_6581    1
#define SID_TYPE_8580    2
#define SID_TYPE_FPGASID 3
#define SID_TYPE_SWINSID 4
#define SID_TYPE_ARMSID  5
#define SID_TYPE_ARM2SID 6
#define SID_TYPE_SIDFX   7
#define SID_TYPE_FPGASID_DUKESTAH 8

const char *u64_sid_base[] = { "Unmapped",
                               "$D400", "$D420", "$D440", "$D460", "$D480", "$D4A0", "$D4C0", "$D4E0",
                               "$D500", "$D520", "$D540", "$D560", "$D580", "$D5A0", "$D5C0", "$D5E0",
                               "$D600", "$D620", "$D640", "$D660", "$D680", "$D6A0", "$D6C0", "$D6E0",
                               "$D700", "$D720", "$D740", "$D760", "$D780", "$D7A0", "$D7C0", "$D7E0",
                               "$DE00", "$DE20", "$DE40", "$DE60",
                               "$DE80", "$DEA0", "$DEC0", "$DEE0",
                               "$DF00", "$DF20", "$DF40", "$DF60",
                               "$DF80", "$DFA0", "$DFC0", "$DFE0" };

uint8_t u64_sid_offsets[] = { UNMAPPED_BASE,
                              0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E,
                              0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E,
                              0x60, 0x62, 0x64, 0x66, 0x68, 0x6A, 0x6C, 0x6E,
                              0x70, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C, 0x7E,
                              0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
                              0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE,
                            };


const char *scan_modes[] = {
	"VGA 60",
	"VESA 768x576@60Hz",
	"VESA 800x600@60Hz",
	"VESA 1024x768@60Hz",
	"720x480@60Hz",
	"720x576@50Hz (625)",
	"720x576@50.12Hz (625)",
	"720x576@50Hz (624)",
	"720x576@50.12Hz (624)",
	"1440x288 @ 50Hz (312)",
	"1440x288 @ 50Hz (313)",
	"Commodore, Extd Blnk",
	"Commodore, Detuned",
	"Commodore, Wide brd",
	"720(1440)x480 54MHz",
};

const char *stereo_addr[] = { "Off", "A5", "A6", "A7", "A8", "A9" };
const char *sid_split[] = { "Off", "1/2 (A5)", "1/2 (A6)", "1/2 (A7)", "1/2 (A8)", "1/4 (A5,A6)", "1/4 (A5,A8)", "1/4 (A7,A8)" };

static const char *iec_modes[] = { "All Connected", "C64<->Ultimate", "DIN<->Ultimate", "C64<->DIN" };
static const char *joyswaps[] = { "Normal", "Swapped" };
static const char *en_dis5[] = { "Disabled", "Enabled", "Transp. Border" };
static const char *digi_levels[] = { "Off", "Low", "Medium", "High" };
static const char *burst_modes[] = { "Off", "CIA1", "CIA2" };
static const char *yes_no[] = { "No", "Yes" };
static const char *dvi_hdmi[] = { "Auto", "HDMI", "DVI" };
static const char *video_sel[] = { "CVBS + SVideo", "RGB" };
static const char *color_sel[] = { "PAL", "NTSC", "PAL-60", "NTSC-50", "PAL-60/L", "NTSC-50/L" };

static const char *sid_types[] = { "None", "6581", "8580", "FPGASID", "SwinSID Ultimate", "ARMSID", "ARM2SID", "SidFx", "FPGASID Dukestah" };
static const char *sid_shunt[] = { "Off", "On" };
static const char *sid_caps[] = { "470 pF", "22 nF" };
static const char *filter_sel[] = { "8580 Lo", "8580 Hi", "6581", "6581 Alt", "U2 Low", "U2 Mid", "U2 High" };
static const char *filter_res[] = { "Low", "High" };
static const char *comb_wave[] = { "6581", "8580" };
static const char *ledselects[] = { "On", "Off", "Drive A Pwr", "DrvAPwr + DrvBPwr", "Drive A Act", "DrvAAct + DrvBAct",
                                    "DrvAPwr ^ DrvAAct", "USB Activity", "Any Activity", "!(DrvAAct)", "!(DrvAAct+DrvBAct)",
                                    "!(USB Act)", "!(Any Act)", "IRQ Line", "!(IRQ Line)", "Drive B Act" };

const char *speaker_vol[] = { "Disabled", "Vol 1", "Vol 2", "Vol 3", "Vol 4", "Vol 5", "Vol 6", "Vol 7", "Vol 8", "Vol 9", "Vol 10", "Vol 11", "Vol 12", "Vol 13", "Vol 14", "Vol 15" };

static const char *volumes[] = { "OFF", "+6 dB", "+5 dB", "+4 dB", "+3 dB", "+2 dB", "+1 dB", " 0 dB", "-1 dB",
                                 "-2 dB", "-3 dB", "-4 dB", "-5 dB", "-6 dB", "-7 dB", "-8 dB", "-9 dB",
                                 "-10 dB","-11 dB","-12 dB","-13 dB","-14 dB","-15 dB","-16 dB","-17 dB",
                                 "-18 dB","-24 dB","-27 dB","-30 dB","-36 dB","-42 dB"  }; // 31 settings

static const char *pannings[] = { "Left 5", "Left 4", "Left 3", "Left 2", "Left 1", "Center",
                                  "Right 1", "Right 2", "Right 3", "Right 4", "Right 5" }; // 11 settings

static const uint8_t volume_ctrl[] = { 0x00, 0xff, 0xe4, 0xcb, 0xb5, 0xa1, 0x90, 0x80, 0x72,
                                       0x66, 0x5b, 0x51, 0x48, 0x40, 0x39, 0x33, 0x2d,
                                       0x28, 0x24, 0x20, 0x1d, 0x1a, 0x17, 0x14, 0x12,
                                       0x10, 0x08, 0x06, 0x04, 0x02, 0x01 };

static const uint16_t pan_ctrl[] = { 0, 40, 79, 116, 150, 181, 207, 228, 243, 253, 256 };

static const uint8_t stereo_bits[] = { 0x00, 0x02, 0x04, 0x08, 0x10, 0x20 };
static const uint8_t split_bits[] = { 0x00, 0x02, 0x04, 0x08, 0x10, 0x06, 0x12, 0x18 };
static const char *speeds_u64[]   = { " 1", " 2", " 3", " 4", " 5", " 6", " 8", "10", "12", "14", "16", "20", "24", "32", "40", "48" };
static const char *speeds_u64ii[] = { " 1", " 2", " 3", " 4", " 6", " 8", "10", "12", "14", "16", "20", "24", "32", "40", "48", "64" };
static const char *speed_regs[] = { "Off", "Manual", "U64 Turbo Registers", "TurboEnable Bit", "a", "b" };
static const uint8_t speedregs_regvalues[] = { 0x00, 0x00, 0x01, 0x05, 0x00, 0x00 }; // removed 3 and 7

/*
00 ff ff ff ff ff ff 00  09 d1 db 78 45 54 00 00
0b 1b 01 03 80 30 1b 78  2e 34 55 a7 55 52 a0 27
11 50 54 a5 6b 80 d1 c0  81 c0 81 00 81 80 a9 c0
b3 00 01 01 01 01 02 3a  80 18 71 38 2d 40 58 2c
45 00 dc 0c 11 00 00 1e  00 00 00 ff 00 36 33 48
30 35 30 37 36 53 4c 30  0a 20 00 00 00 fd 00 32
4c 1e 53 11 00 0a 20 20  20 20 20 20 00 00 00 fc
00 42 65 6e 51 20 47 57  32 32 37 30 0a 20 01 fc
02 03 22 f1 4f 90 1f 05  14 04 13 03 12 07 16 15
01 06 11 02 23 09 07 07  83 01 00 00 65 03 0c 00
10 00 02 3a 80 18 71 38  2d 40 58 2c 45 00 dc 0c
11 00 00 1e 01 1d 80 18  71 1c 16 20 58 2c 25 00
dc 0c 11 00 00 9e 01 1d  00 72 51 d0 1e 20 6e 28
55 00 dc 0c 11 00 00 1e  8c 0a d0 8a 20 e0 2d 10
10 3e 96 00 dc 0c 11 00  00 18 00 00 00 00 00 00
00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 81
*/

struct t_cfg_definition u64_cfg[] = {
    { CFG_SYSTEM_MODE,          CFG_TYPE_ENUM, "System Mode",                  "%s", color_sel,    0,  5, 1 },
    { CFG_JOYSWAP,              CFG_TYPE_ENUM, "Joystick Swapper",             "%s", joyswaps,     0,  1, 0 },
    { CFG_USERPORT_EN,          CFG_TYPE_ENUM, "UserPort Power Enable",        "%s", en_dis,       0,  1, 1 },
//    { CFG_CART_PREFERENCE,      CFG_TYPE_ENUM, "Cartridge Preference",         "%s", cartmodes,    0,  2, 0 }, // moved to C64 for user consistency
    { CFG_PALETTE,              CFG_TYPE_STRFUNC, "Palette Definition",        "%s", (const char **)U64Config :: list_palettes, 0, 30, (int)"" },
    { CFG_COLOR_CLOCK_ADJ,      CFG_TYPE_VALUE, "Adjust Color Clock",      "%d ppm", NULL,      -100,100, 0 },
    { CFG_ANALOG_OUT_SELECT,    CFG_TYPE_ENUM, "Analog Video Mode",            "%s", video_sel,    0,  1, 0 },
//    { CFG_CHROMA_DELAY,         CFG_TYPE_VALUE, "Chroma Delay",                "%d", NULL,        -3,  3, 0 },
    { CFG_HDMI_ENABLE,          CFG_TYPE_ENUM, "Digital Video Mode",           "%s", dvi_hdmi,     0,  2, 0 },
    { CFG_SCANLINES,            CFG_TYPE_ENUM, "HDMI Scan lines",              "%s", en_dis,       0,  1, 1 },
    { CFG_IEC_BUS_MODE,         CFG_TYPE_ENUM, "Serial Bus Mode",              "%s", iec_modes,    0,  3, 0 },
    { CFG_PARCABLE_ENABLE,      CFG_TYPE_ENUM, "SpeedDOS Parallel Cable",      "%s", en_dis,       0,  1, 0 },
    { CFG_IEC_BURST_EN,         CFG_TYPE_ENUM, "Burst Mode Patch",             "%s", burst_modes,  0,  2, 0 },
    { CFG_LED_SELECT_0,         CFG_TYPE_ENUM, "LED Select Top",               "%s", ledselects,   0, 15, 0 },
    { CFG_LED_SELECT_1,         CFG_TYPE_ENUM, "LED Select Bot",               "%s", ledselects,   0, 15, 4 },
#if U64 != 2
    { CFG_SPEAKER_VOL,          CFG_TYPE_ENUM, "Speaker Volume (SpkDat)",      "%s", speaker_vol,  0, 10, 5 },
#endif
    { CFG_PLAYER_AUTOCONFIG,    CFG_TYPE_ENUM, "SID Player Autoconfig",        "%s", en_dis,       0,  1, 1 },
    { CFG_ALLOW_EMUSID,         CFG_TYPE_ENUM, "Allow Autoconfig uses UltiSid","%s", yes_no,       0,  1, 1 },
#if DEVELOPER
    //    { CFG_COLOR_CODING,         CFG_TYPE_ENUM, "Color Coding (not Timing!)",   "%s", color_sel,    0,  1, 0 },
    { CFG_VIC_TEST,             CFG_TYPE_ENUM, "VIC Test Colors",              "%s", en_dis5,      0,  2, 0 },
#endif
    { CFG_SPEED_REGS,           CFG_TYPE_ENUM, "Turbo Control",                "%s", speed_regs,   0,  3, 0 },
#if U64 == 2
    { CFG_SPEED_PREF,           CFG_TYPE_ENUM, "CPU Speed",                "%s MHz", speeds_u64ii, 0, 15, 0 },
#else
    { CFG_SPEED_PREF,           CFG_TYPE_ENUM, "CPU Speed",                "%s MHz", speeds_u64,   0, 15, 0 },
#endif
    { CFG_BADLINES_EN,          CFG_TYPE_ENUM, "Badline Timing",               "%s", en_dis,       0,  1, 1 },
    { CFG_SUPERCPU_DET,         CFG_TYPE_ENUM, "SuperCPU Detect (D0BC)",       "%s", en_dis,       0,  1, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition u64_sid_detection_cfg[] = {
    { CFG_SOCKET1_ENABLE,       CFG_TYPE_ENUM, "SID Socket 1",                 "%s", en_dis,       0,  1, 0 },
    { CFG_SOCKET2_ENABLE,       CFG_TYPE_ENUM, "SID Socket 2",                 "%s", en_dis,       0,  1, 0 },
    { CFG_SID1_TYPE,			CFG_TYPE_ENUM, "SID Detected Socket 1",        "%s", sid_types,    0,  8, 0 },
    { CFG_SID2_TYPE,			CFG_TYPE_ENUM, "SID Detected Socket 2",        "%s", sid_types,    0,  8, 0 },
    { CFG_SID1_SHUNT,           CFG_TYPE_ENUM, "SID Socket 1 1K Ohm Resistor", "%s", sid_shunt,    0,  1, 0 },
    { CFG_SID2_SHUNT,           CFG_TYPE_ENUM, "SID Socket 2 1K Ohm Resistor", "%s", sid_shunt,    0,  1, 0 },
    { CFG_SID1_CAPS,            CFG_TYPE_ENUM, "SID Socket 1 Capacitors",      "%s", sid_caps,     0,  1, 0 },
    { CFG_SID2_CAPS,            CFG_TYPE_ENUM, "SID Socket 2 Capacitors",      "%s", sid_caps,     0,  1, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition u64_sid_addressing_cfg[] = {
    { CFG_SID1_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 1 Address",         "%s", u64_sid_base, 0, 48, 1 },
    { CFG_SID2_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 2 Address",         "%s", u64_sid_base, 0, 48, 1 },
    { CFG_STEREO_DIFF,			CFG_TYPE_ENUM, "Ext DualSID Range Split",      "%s", stereo_addr,  0,  5, 0 },
    { CFG_EMUSID1_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 1 Address",            "%s", u64_sid_base, 0, 48, 1 },
    { CFG_EMUSID2_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 2 Address",            "%s", u64_sid_base, 0, 48, 1 },
    { CFG_EMUSID_SPLIT,         CFG_TYPE_ENUM, "UltiSID Range Split",          "%s", sid_split,    0,  7, 0 },
    { CFG_PADDLE_EN,            CFG_TYPE_ENUM, "Paddle Override",              "%s", en_dis,       0,  1, 1 },
    { CFG_AUTO_MIRRORING,       CFG_TYPE_ENUM, "Auto Address Mirroring",       "%s", en_dis,       0,  1, 1 },
    { CFG_SHOW_SID_ADDR,        CFG_TYPE_FUNC, "Visual SID Address Editor",   "-->", (const char **)U64Config :: show_sid_addr, 0, 0, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition u64_ultisid_cfg[] = {
    { CFG_EMUSID1_FILTER,       CFG_TYPE_ENUM, "UltiSID 1 Filter Curve",       "%s", filter_sel,   0,  6, 0 },
    { CFG_EMUSID2_FILTER,       CFG_TYPE_ENUM, "UltiSID 2 Filter Curve",       "%s", filter_sel,   0,  6, 0 },
    { CFG_EMUSID1_RESONANCE,    CFG_TYPE_ENUM, "UltiSID 1 Filter Resonance",   "%s", filter_res,   0,  1, 0 },
    { CFG_EMUSID2_RESONANCE,    CFG_TYPE_ENUM, "UltiSID 2 Filter Resonance",   "%s", filter_res,   0,  1, 0 },
    { CFG_EMUSID1_WAVES,        CFG_TYPE_ENUM, "UltiSID 1 Combined Waveforms", "%s", comb_wave,    0,  1, 0 },
    { CFG_EMUSID2_WAVES,        CFG_TYPE_ENUM, "UltiSID 2 Combined Waveforms", "%s", comb_wave,    0,  1, 0 },
    { CFG_EMUSID1_DIGI,         CFG_TYPE_ENUM, "UltiSID 1 Digis Level",        "%s", digi_levels,  0,  3, 2 },
    { CFG_EMUSID2_DIGI,         CFG_TYPE_ENUM, "UltiSID 2 Digis Level",        "%s", digi_levels,  0,  3, 2 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition u64_mixer_cfg[] = {
    { CFG_MIXER0_VOL,           CFG_TYPE_ENUM, "Vol UltiSid 1",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER1_VOL,           CFG_TYPE_ENUM, "Vol UltiSid 2",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER2_VOL,           CFG_TYPE_ENUM, "Vol Socket 1",                 "%s", volumes,      0, 30, 7 },
    { CFG_MIXER3_VOL,           CFG_TYPE_ENUM, "Vol Socket 2",                 "%s", volumes,      0, 30, 7 },
    { CFG_MIXER4_VOL,           CFG_TYPE_ENUM, "Vol Sampler L",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER5_VOL,           CFG_TYPE_ENUM, "Vol Sampler R",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER6_VOL,           CFG_TYPE_ENUM, "Vol Drive 1",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER7_VOL,           CFG_TYPE_ENUM, "Vol Drive 2",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER8_VOL,           CFG_TYPE_ENUM, "Vol Tape Read",                "%s", volumes,      0, 30, 29 },
    { CFG_MIXER9_VOL,           CFG_TYPE_ENUM, "Vol Tape Write",               "%s", volumes,      0, 30, 29 },
    { CFG_MIXER0_PAN,           CFG_TYPE_ENUM, "Pan UltiSID 1",                "%s", pannings,     0, 10, 5 },
    { CFG_MIXER1_PAN,           CFG_TYPE_ENUM, "Pan UltiSID 2",                "%s", pannings,     0, 10, 5 },
    { CFG_MIXER2_PAN,           CFG_TYPE_ENUM, "Pan Socket 1",                 "%s", pannings,     0, 10, 2 },
    { CFG_MIXER3_PAN,           CFG_TYPE_ENUM, "Pan Socket 2",                 "%s", pannings,     0, 10, 8 },
    { CFG_MIXER4_PAN,           CFG_TYPE_ENUM, "Pan Sampler L",                "%s", pannings,     0, 10, 2 },
    { CFG_MIXER5_PAN,           CFG_TYPE_ENUM, "Pan Sampler R",                "%s", pannings,     0, 10, 8 },
    { CFG_MIXER6_PAN,           CFG_TYPE_ENUM, "Pan Drive 1",                  "%s", pannings,     0, 10, 3 },
    { CFG_MIXER7_PAN,           CFG_TYPE_ENUM, "Pan Drive 2",                  "%s", pannings,     0, 10, 7 },
    { CFG_MIXER8_PAN,           CFG_TYPE_ENUM, "Pan Tape Read",                "%s", pannings,     0, 10, 5 },
    { CFG_MIXER9_PAN,           CFG_TYPE_ENUM, "Pan Tape Write",               "%s", pannings,     0, 10, 5 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition u64_speaker_mixer_cfg[] = {
    { CFG_SPEAKER_EN,           CFG_TYPE_ENUM, "Speaker Enable",               "%s", en_dis,       0,  1, 1 },
    { CFG_MIXER0_VOL,           CFG_TYPE_ENUM, "Vol UltiSid 1",                "%s", volumes,      0, 30, 16 },
    { CFG_MIXER1_VOL,           CFG_TYPE_ENUM, "Vol UltiSid 2",                "%s", volumes,      0, 30, 16 },
    { CFG_MIXER2_VOL,           CFG_TYPE_ENUM, "Vol Socket 1",                 "%s", volumes,      0, 30, 16 },
    { CFG_MIXER3_VOL,           CFG_TYPE_ENUM, "Vol Socket 2",                 "%s", volumes,      0, 30, 16 },
    { CFG_MIXER4_VOL,           CFG_TYPE_ENUM, "Vol Sampler L",                "%s", volumes,      0, 30, 16 },
    { CFG_MIXER5_VOL,           CFG_TYPE_ENUM, "Vol Sampler R",                "%s", volumes,      0, 30, 16 },
    { CFG_MIXER6_VOL,           CFG_TYPE_ENUM, "Vol Drive 1",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER7_VOL,           CFG_TYPE_ENUM, "Vol Drive 2",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER8_VOL,           CFG_TYPE_ENUM, "Vol Tape Read",                "%s", volumes,      0, 30, 29 },
    { CFG_MIXER9_VOL,           CFG_TYPE_ENUM, "Vol Tape Write",               "%s", volumes,      0, 30, 29 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0,  0 } };

extern Overlay *overlay;


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
U64Config :: U64Mixer :: U64Mixer()
{
    register_store(STORE_PAGE_ID, "Audio Mixer", u64_mixer_cfg);
    // enable "hot" updates for mixer
    for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
        cfg->set_change_hook(b, U64Config::setMixer);
    }
    for (uint8_t b = CFG_MIXER0_PAN; b <= CFG_MIXER9_PAN; b++) {
        cfg->set_change_hook(b, U64Config::setMixer);
    }
}

void U64Config :: U64Mixer :: effectuate_settings()
{
    //printf("U64Mixer :: effectuate_settings()\n");
    setMixer(cfg->items[0]);
}

#if U64 == 2
U64Config :: U64SpeakerMixer :: U64SpeakerMixer()
{
    register_store(STORE_PAGE_ID+1, "Speaker Mixer", u64_speaker_mixer_cfg);
    for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
        cfg->set_change_hook(b, U64Config::setSpeakerMixer);
    }
    cfg->set_change_hook(CFG_SPEAKER_EN, U64Config::enableDisableSpeaker);
}

void U64Config :: U64SpeakerMixer :: effectuate_settings()
{
    enableDisableSpeaker(cfg->items[0]); // This should be the first!
    setSpeakerMixer(cfg->items[0]);
}

#endif    


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
U64Config :: U64SidSockets :: U64SidSockets()
{
    register_store(STORE_PAGE_ID, "SID Sockets Configuration", u64_sid_detection_cfg);

    // This field shows what was detected and cannot be changed
    cfg->disable(CFG_SID1_TYPE);
    cfg->disable(CFG_SID2_TYPE);
    cfg->disable(CFG_SID1_CAPS);
    cfg->disable(CFG_SID2_CAPS);

    if (!isEliteBoard()) {
        cfg->disable(CFG_SID1_SHUNT);
        cfg->disable(CFG_SID2_SHUNT);
    }
}

int U64Config :: detectDukestahAdapter()
{
    volatile uint8_t *base1 = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD400); // D400
    volatile uint8_t *base2 = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD500); // D500

    C64 :: hard_stop();
    
    base1[25] = 0x81; // Enter config mode
    base1[26] = 0x65;
    base1[30] = 4;   // Set FPGASID to stereo

    base1[25] = 0x82;  // Swap mode
    base1[26] = 0x65;
    base1[28] = 3;

    base1[25] = 0x81; // Enter config mode
    base1[26] = 0x65;

    base1[28] = 5;
    base2[28] = 13;
    
    base1[25] = 0x82;   // Swap mode
    base1[26] = 0x65;
    uint8_t tmp1 = base1[28] & 15;   // Read SID2 register 28
    base1[25] = 0x00;   // normal mode
    base1[26] = 0x00;

    if (tmp1 == 13) ((SidDeviceFpgaSid*)(sidDevice[0]))->setDukestahAdapterPresent();
    ((SidDeviceFpgaSid*)(sidDevice[0]))->effectuate_settings();

    if (tmp1 == 13) return SID_TYPE_FPGASID_DUKESTAH;
    
    return SID_TYPE_FPGASID;
}

int U64Config :: detectFPGASID(int socket)
{
    volatile uint8_t *base = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD400 + 256 * socket); // D400 or D500

    C64 :: hard_stop();

    // For FPGASID: Switch to DIAG mode
    base[25] = 0xEE;
    base[26] = 0xAB;

    uint8_t id1 = base[0];
    uint8_t id2 = base[1];

    printf("FPGASID Detection: %b %b\n", id1, id2);
    // Read Identification
    if ((id1 == 0x1D) && (id2 == 0xF5)) {
        // FPGASID found
        sidDevice[socket] = new SidDeviceFpgaSid(socket, base);
        return SID_TYPE_FPGASID;
    }
    return SID_TYPE_NONE;
}

int U64Config :: detectRemakes(int socket)
{
    volatile uint8_t *base = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD400 + 256 * socket); // D400 or D500

    base[29] = 'S';
    wait_10us(1);
    base[30] = 'I';
    wait_10us(1);
    base[31] = 'D';
    wait_10us(1);

    wait_ms(10);

    uint8_t id1 = base[27];
    wait_10us(1);
    uint8_t id2 = base[28];
    wait_10us(1);

    printf("ARMSID Detect: %b %b\n", id1, id2);

    if ((id1 == 'S') && (id2 == 'W')) {
        base[29] = 0;
        sidDevice[socket] = new SidDeviceSwinSid(socket, base);
        return SID_TYPE_SWINSID; // SwinSid Ultimate
    }

    if ((id1 == 'N') && (id2 == 'O')) {
        base[31] = 'I';
        wait_10us(1);
        base[30] = 'I';
        wait_10us(1);
        wait_ms(10);
        uint8_t id1 = base[27];
        wait_10us(1);
        base[29] = 0;
        wait_10us(1);
        sidDevice[socket] = new SidDeviceArmSid(socket, base);

        if ((id1 == 'L') || (id1 == 'R')) {
            return SID_TYPE_ARM2SID; // ARM2SID
        }
        return SID_TYPE_ARMSID; // ARMSID
    }

    return 0;
}

void U64Config :: U64SidSockets :: detect(void)
{
    int sid1 = 0, sid2 = 0, realsid1, realsid2;

    // we know that the function below initializes the cartridge correctly
    C64 :: getMachine();
    C64_MODE = C64_MODE_UNRESET;
    while (C64 :: c64_reset_detect())
        ;

    S_SetupDetectionAddresses();
    sid1 = u64_configurator.detectFPGASID(0);

    S_SetupDetectionAddresses();
    sid2 = u64_configurator.detectFPGASID(1);

    if ((sid1 == SID_TYPE_NONE) || (sid2 == SID_TYPE_NONE)) {
        wait_ms(100);

        S_SetupDetectionAddresses();
        if (sid1 == SID_TYPE_NONE) {
            sid1 = u64_configurator.detectRemakes(0);
        }

        S_SetupDetectionAddresses();
        if (sid2 == SID_TYPE_NONE) {
            sid2 = u64_configurator.detectRemakes(1);
        }
    }

    if ((sid1 == SID_TYPE_NONE) || (sid2 == SID_TYPE_NONE)) {
        S_SidDetector(realsid1, realsid2);
        if (realsid1 && sid1 == SID_TYPE_NONE) {
            sid1 = realsid1;
        }
        if (realsid2 && sid2 == SID_TYPE_NONE) {
            sid2 = realsid2;
        }
    }

    if ((sid1 == SID_TYPE_FPGASID) && (sid2 == SID_TYPE_NONE)) {
    	S_SetupDetectionAddresses();
    	int tmp = u64_configurator.detectDukestahAdapter();
    	if (tmp == SID_TYPE_FPGASID_DUKESTAH)
           sid1 = sid2 = tmp;
    }

    printf("$$ SID1 = %d. SID2 = %d\n", sid1, sid2);

    // Configuration has changed? Then disable the sockets until the user
    // has approved the detection. We only do this for 12V. We simply enable
    // for 9V supply.
    if (sid1 != cfg->get_value(CFG_SID1_TYPE)) {
        cfg->set_value(CFG_SID1_TYPE, sid1);
        switch(sid1) {
        case 1: // 6581
            cfg->set_value(CFG_SOCKET1_ENABLE, 0);
            cfg->set_value(CFG_SID1_CAPS, 0);
            cfg->set_value(CFG_SID1_SHUNT, 1);
            break;
        case 2: // 8580
            cfg->set_value(CFG_SOCKET1_ENABLE, 1);
            cfg->set_value(CFG_SID1_CAPS, 1);
            cfg->set_value(CFG_SID1_SHUNT, 0);
            break;
        case 0:
            // Leave the socket enabled if it is already was enabled
            break;
        default: // other than None, 6581 or 8580
            cfg->set_value(CFG_SOCKET1_ENABLE, 1);
        }
    }

    if (sid2 != cfg->get_value(CFG_SID2_TYPE)) {
        cfg->set_value(CFG_SID2_TYPE, sid2);
        switch(sid2) {
        case 1: // 6581
            cfg->set_value(CFG_SOCKET2_ENABLE, 0);
            cfg->set_value(CFG_SID2_CAPS, 0);
            cfg->set_value(CFG_SID2_SHUNT, 1);
            break;
        case 2: // 8580
            cfg->set_value(CFG_SOCKET2_ENABLE, 1);
            cfg->set_value(CFG_SID2_CAPS, 1);
            cfg->set_value(CFG_SID2_SHUNT, 0);
            break;
        case 0:
            // Leave the socket enabled if it is already was enabled
            break;
        default: // other than None, 6581 or 8580
            cfg->set_value(CFG_SOCKET2_ENABLE, 1);
        }
    }
    if (cfg->is_flash_stale()) {
        cfg->write();
        UserInterface :: postMessage("SID changed. Please review settings");
    }
}

void U64Config :: U64SidSockets :: effectuate_settings()
{
    uint8_t sid_ctrl_value = 0;

    //printf("U64Sockets :: effectuate_settings()\n");
    {
        uint8_t typ = cfg->get_value(CFG_SID1_TYPE); // 0 = none, 1 = 6581, 2 = 8580
        uint8_t shu = cfg->get_value(CFG_SID1_SHUNT);
        uint8_t cap = 1-cfg->get_value(CFG_SID1_CAPS);
        uint8_t reg = 0;
        if (cfg->get_value(CFG_SOCKET1_ENABLE)) {
            switch (typ) {
                case 1: reg = 3; break;
                case 2: reg = 2; break;
                default: reg = 2; break;
            }
        }
        // bit 0 = voltage
        // bit 1 = regulator enable
        // bit 2 = shunt
        // bit 3 = caps
        uint8_t value = reg | (shu << 2) | (cap << 3);
        sid_ctrl_value = value;
#if U64 == 1
        C64_PLD_SIDCTRL1 = value | 0x50;
#endif
    }

    {
        uint8_t typ = cfg->get_value(CFG_SID2_TYPE); // 0 = none, 1 = 6581, 2 = 8580
        uint8_t shu = cfg->get_value(CFG_SID2_SHUNT);
        uint8_t cap = 1-cfg->get_value(CFG_SID2_CAPS);
        uint8_t reg = 0;
        if (cfg->get_value(CFG_SOCKET2_ENABLE)) {
            switch (typ) {
                case 1: reg = 3; break;
                case 2: reg = 2; break;
                default: reg = 2; break;
            }
        }
        // bit 0 = voltage
        // bit 1 = regulator enable
        // bit 2 = shunt
        // bit 3 = caps
        uint8_t value = reg | (shu << 2) | (cap << 3);
        sid_ctrl_value |= (value << 4);
#if U64 == 1
        C64_PLD_SIDCTRL2 = value | 0xB0;
#endif
    }

#if U64 == 2
    if(i2c) {
        i2c->i2c_lock("SID Configuration");
        i2c->set_channel(I2C_CHANNEL_1V8);
        i2c->i2c_write_byte(0x40, 0x01, sid_ctrl_value);
        i2c->i2c_write_byte(0x40, 0x03, 0x00);
        i2c->i2c_unlock();
        printf("Written %02X to I2C\n", sid_ctrl_value);
    }
#endif

    C64_SID1_EN_BAK  = cfg->get_value(CFG_SOCKET1_ENABLE);
    C64_SID2_EN_BAK  = cfg->get_value(CFG_SOCKET2_ENABLE);
    C64_SID1_EN      = cfg->get_value(CFG_SOCKET1_ENABLE) ? 1 : 0;
    C64_SID2_EN      = cfg->get_value(CFG_SOCKET2_ENABLE) ? 1 : 0;
}
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
U64Config :: U64UltiSids :: U64UltiSids()
{
    register_store(STORE_PAGE_ID, "UltiSID Configuration", u64_ultisid_cfg);

    cfg->set_change_hook(CFG_EMUSID1_FILTER, U64Config::setFilter);
    cfg->set_change_hook(CFG_EMUSID2_FILTER, U64Config::setFilter);
    cfg->set_change_hook(CFG_EMUSID1_RESONANCE, U64Config::setSidEmuParams);
    cfg->set_change_hook(CFG_EMUSID2_RESONANCE, U64Config::setSidEmuParams);
    cfg->set_change_hook(CFG_EMUSID1_WAVES, U64Config::setSidEmuParams);
    cfg->set_change_hook(CFG_EMUSID2_WAVES, U64Config::setSidEmuParams);
    cfg->set_change_hook(CFG_EMUSID1_DIGI, U64Config::setSidEmuParams);
    cfg->set_change_hook(CFG_EMUSID2_DIGI, U64Config::setSidEmuParams);
}

void U64Config :: U64UltiSids :: effectuate_settings()
{
    //printf("U64UltiSids :: effectuate_settings()\n");

    setFilter(cfg->find_item(CFG_EMUSID1_FILTER));
    setFilter(cfg->find_item(CFG_EMUSID2_FILTER));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_RESONANCE));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_RESONANCE));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_WAVES));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_WAVES));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_DIGI));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_DIGI));
}
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
U64Config :: U64SidAddressing :: U64SidAddressing()
{
    register_store(STORE_PAGE_ID, "SID Addressing", u64_sid_addressing_cfg);
}

void U64Config :: U64SidAddressing :: effectuate_settings()
{
    //printf("U64SidAddressing :: effectuate_settings()\n");
    uint8_t base[4];
    uint8_t mask[4];
    uint8_t split[4];

    get_sid_addresses(cfg, base, mask, split);
    fix_splits(base, mask, split);
    if (cfg->get_value(CFG_AUTO_MIRRORING)) {
        auto_mirror(base, mask, split, 4);
    }

    C64_SID1_BASE    = C64_SID1_BASE_BAK = base[0];
    C64_SID2_BASE    = C64_SID2_BASE_BAK = base[1];
    C64_EMUSID1_BASE = C64_EMUSID1_BASE_BAK =  base[2];
    C64_EMUSID2_BASE = C64_EMUSID2_BASE_BAK =  base[3];

    C64_SID1_MASK    = C64_SID1_MASK_BAK = mask[0];
    C64_SID2_MASK    = C64_SID2_MASK_BAK = mask[1];
    C64_EMUSID1_MASK = C64_EMUSID1_MASK_BAK = mask[2];
    C64_EMUSID2_MASK = C64_EMUSID2_MASK_BAK = mask[3];

    C64_STEREO_ADDRSEL = C64_STEREO_ADDRSEL_BAK = cfg->get_value(CFG_STEREO_DIFF);
    C64_EMUSID_SPLIT =  C64_EMUSID_SPLIT_BAK = cfg->get_value(CFG_EMUSID_SPLIT);

    printf("Resulting address map: Slot1: %02X/%02X (%s) Slot2: %02X/%02X (%s) SlotSplit: %02X.  Emu1: %02X/%02X  Emu2: %02X/%02X  Emu Split: %02X\n",
            C64_SID1_BASE_BAK, C64_SID1_MASK_BAK, en_dis[C64_SID1_EN_BAK],
            C64_SID2_BASE_BAK, C64_SID2_MASK_BAK, en_dis[C64_SID2_EN_BAK], C64_STEREO_ADDRSEL,
            C64_EMUSID1_BASE_BAK, C64_EMUSID1_MASK_BAK,
            C64_EMUSID2_BASE_BAK, C64_EMUSID2_MASK_BAK, C64_EMUSID_SPLIT_BAK );
}
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

U64Config :: U64Config() : SubSystem(SUBSYSID_U64)
{
    systemMode = e_NOT_SET;
    U64_ETHSTREAM_ENA = 0;
	skipReset = false;

    fm = FileManager::getFileManager();
    resetSemaphore = xSemaphoreCreateBinary();

    if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		struct t_cfg_definition *def = u64_cfg;
		register_store(STORE_PAGE_ID, "U64 Specific Settings", def);

		// Tweak: This has to be done first in order to make sure that the correct cart is started
		// at cold boot.
        C64 *machine = C64 :: getMachine();
		machine->ConfigureU64SystemBus();
		if (!(machine-> is_accessible())) {
		    printf("*** WARNING: The C64 should be stopped at this time for the SID Detection to work!\n");
		    machine->hard_stop();
		}

		sidDevice[0] = NULL;
        sidDevice[1] = NULL;

        cfg->set_change_hook(CFG_SCAN_MODE_TEST, U64Config::setScanMode);
        cfg->set_change_hook(CFG_COLOR_CLOCK_ADJ, U64Config::setPllOffset);
        cfg->set_change_hook(CFG_LED_SELECT_0, U64Config::setLedSelector);
        cfg->set_change_hook(CFG_LED_SELECT_1, U64Config::setLedSelector);
        cfg->set_change_hook(CFG_SPEED_REGS, U64Config::setCpuSpeed);
        cfg->set_change_hook(CFG_SPEED_PREF, U64Config::setCpuSpeed);
        cfg->set_change_hook(CFG_BADLINES_EN, U64Config::setCpuSpeed);
        cfg->set_change_hook(CFG_SUPERCPU_DET, U64Config::setCpuSpeed);

        if (!isEliteBoard()) {
            cfg->disable(CFG_JOYSWAP);
        }

        InitFunction *init_u64 = new InitFunction("U64 Configurator", [](void *obj, void *_param) {
            printf("*** Init U64 Configurator\n");
            u64_configurator.sockets.detect();
            u64_configurator.clear_ram();
            u64_configurator.hdmiMonitor = u64_configurator.IsMonitorHDMI(); // requires I2C
            u64_configurator.effectuate_settings(); // requires I2C
            u64_configurator.sockets.effectuate_settings();
            u64_configurator.mixercfg.effectuate_settings();
            u64_configurator.ultisids.effectuate_settings();
            u64_configurator.sidaddressing.effectuate_settings();
#if U64 == 2
            u64_configurator.speakercfg.effectuate_settings();
#endif
            xTaskCreate( U64Config :: reset_task, "U64 Reset Task", configMINIMAL_STACK_SIZE, &u64_configurator, PRIO_REALTIME, &u64_configurator.resetTaskHandle );
            printf("*** U64 Configurator Done\n");
        }, NULL, NULL, 3); // early
    }
}

void U64Config :: ResetHandler()
{
    BaseType_t woken;
    xSemaphoreGiveFromISR(resetSemaphore, &woken);
}

void U64Config :: reset_task(void *a)
{
    U64Config *configurator = (U64Config *)a;
    configurator->run_reset_task();
}

void U64Config :: run_reset_task()
{
    while(1) {
        xSemaphoreTake(resetSemaphore, portMAX_DELAY);

        printf("U64 reset handler. ");
        if (! skipReset) {
            printf("Resetting Settings\n");
            effectuate_settings();
            sockets.effectuate_settings();
            mixercfg.effectuate_settings();
            ultisids.effectuate_settings();
            sidaddressing.effectuate_settings();
        } else {
            printf("SKIP\n");
        }
        skipReset = false;
    }
}

void U64Config :: effectuate_settings()
{
    if(!cfg)
        return;

    C64_PADDLE_EN    = cfg->get_value(CFG_PADDLE_EN);
    C64_PLD_JOYCTRL  = cfg->get_value(CFG_JOYSWAP) ^ 1;
    C64_PADDLE_SWAP  = cfg->get_value(CFG_JOYSWAP);
    U64II_KEYB_JOY   = cfg->get_value(CFG_JOYSWAP);
    U64_USERPORT_EN  = cfg->get_value(CFG_USERPORT_EN) ? 3 : 0;
    printf("USERPORT_EN = %d\n", U64_USERPORT_EN);
    
    //C64_TURBOREGS_EN = 0;
    //C64_SPEED_PREFER = 0;

    setCpuSpeed(cfg->find_item(CFG_SPEED_REGS));

    //printf("U64Config :: effectuate_settings()\n");
    uint8_t sp_vol = cfg->get_value(CFG_SPEAKER_VOL);

    U2PIO_SPEAKER_EN = sp_vol ? (sp_vol << 1) | 0x01 : 0;
    C64_SCANLINES    = cfg->get_value(CFG_SCANLINES);
    uint8_t hdmiSetting = cfg->get_value(CFG_HDMI_ENABLE);

    if (!hdmiSetting) { // Auto = 0
        U64_HDMI_ENABLE = hdmiMonitor ? 1 : 0;
    } else {
        U64_HDMI_ENABLE = (hdmiSetting == 1) ? 1 : 0; // 1 = HDMI, 2 = DVI
    }

    // "All Connected", "C64<->Ultimate", "DIN<->Ultimate", "C64<->DIN"
    const uint8_t c_iec_connectors[] = { 0x70, 0x60, 0x30, 0x50 }; // 4: ext, 5: ult, 6: cia
    uint8_t iec_connections = c_iec_connectors[cfg->get_value(CFG_IEC_BUS_MODE)];
    U64_INT_CONNECTORS = cfg->get_value(CFG_PARCABLE_ENABLE) | (cfg->get_value(CFG_IEC_BURST_EN) << 1) | iec_connections;

    uint8_t format = 0;
    if (cfg->get_value(CFG_ANALOG_OUT_SELECT)) {
        format |= VIDEO_FMT_RGB_OUTPUT;
    }

    bool doPll = false;
    if (cfg->get_value(CFG_SYSTEM_MODE) != (int)systemMode) {
        systemMode = (t_video_mode)cfg->get_value(CFG_SYSTEM_MODE);
        doPll = true;
    }

    const char *palette = cfg->get_string(CFG_PALETTE);
    if (strlen(palette)) {
        load_palette_vpl(DATA_DIRECTORY, palette);
    } else {
        set_palette_rgb(default_colors);
    }

    const t_video_color_timing *ct = color_timings[(int)systemMode];
    C64_PHASE_INCR  = ct->phase_inc;
#if U64 == 2
//    printf("config waiting...\n");
//    vTaskDelay(4000);
    if (doPll) {
        printf("config doing plls...\n");
        SetVideoPll(systemMode, cfg->get_value(CFG_COLOR_CLOCK_ADJ));
        SetHdmiPll(systemMode, ct->mode_bits | format);
        SetVideoMode1080p(systemMode);
        ResetHdmiPll();
        SetResampleFilter(systemMode);
    } else {
        C64_VIDEOFORMAT = ct->mode_bits | format;
    }
#else
    if (doPll) {
        C64_VIDEOFORMAT = ct->mode_bits | format;
        SetVideoPll(systemMode, cfg->get_value(CFG_COLOR_CLOCK_ADJ));
        SetHdmiPll(systemMode, ct->mode_bits | format);
        SetVideoMode(systemMode);
        ResetHdmiPll();
        SetResampleFilter(systemMode);
    }
#endif

#if DEVELOPER
    C64_VIC_TEST = cfg->get_value(CFG_VIC_TEST);
#endif
    setPllOffset(cfg->find_item(CFG_COLOR_CLOCK_ADJ));
    setScanMode(cfg->find_item(CFG_SCAN_MODE_TEST));
    setLedSelector(cfg->find_item(CFG_LED_SELECT_0)); // does both anyway

}

void U64Config :: get_sid_addresses(ConfigStore *cfg, uint8_t *base, uint8_t *mask, uint8_t *split)
{
    base[0] = u64_sid_offsets[cfg->get_value(CFG_SID1_ADDRESS)];
    base[1] = u64_sid_offsets[cfg->get_value(CFG_SID2_ADDRESS)];
    base[2] = u64_sid_offsets[cfg->get_value(CFG_EMUSID1_ADDRESS)];
    base[3] = u64_sid_offsets[cfg->get_value(CFG_EMUSID2_ADDRESS)];

    mask[0] = 0xFE; // & ~stereo_bits[cfg->get_value(CFG_STEREO_DIFF)];
    mask[1] = 0xFE; // & ~stereo_bits[cfg->get_value(CFG_STEREO_DIFF)];
    mask[2] = 0xFE; // & ~split_bits[cfg->get_value(CFG_EMUSID_SPLIT)];
    mask[3] = 0xFE; // & ~split_bits[cfg->get_value(CFG_EMUSID_SPLIT)];

    split[0] = stereo_bits[cfg->get_value(CFG_STEREO_DIFF)];
    split[1] = stereo_bits[cfg->get_value(CFG_STEREO_DIFF)];
    split[2] = split_bits[cfg->get_value(CFG_EMUSID_SPLIT)];
    split[3] = split_bits[cfg->get_value(CFG_EMUSID_SPLIT)];
}

void U64Config :: fix_splits(uint8_t *base, uint8_t *mask, uint8_t *split)
{
    for (int i=0; i<4; i++) {
        base[i] &= ~split[i];
        mask[i] &= ~split[i];
    }
}

int U64Config :: setFilter(ConfigItem *it)
{
    volatile uint8_t *base = (volatile uint8_t *)(C64_SID_BASE + 0x1000);
    if (it->definition->id == CFG_EMUSID2_FILTER) {
        base += 0x800;
    }
    const uint16_t *coef = sid8580_filter_coefficients;
    int mul = 1;
    int div = 4;
    switch(it->getValue()) {
    case 0:
        coef = sid8580_filter_coefficients;
        mul = 7;
        div = 32;
        break;
    case 1:
        coef = sid8580_filter_coefficients;
        mul = 2;
        div = 7;
        break;
    case 2:
        coef = sid6581_filter_coefficients;
        break;
    case 3:
        coef = sid6581_filter_coefficients_temp;
        break;
    case 4:
        coef = sid_curve_original;
        mul = 2;
        div = 3;
        break;
    case 5:
        coef = sid_curve_original;
        div = 1;
        break;
    case 6:
        coef = sid_curve_original;
        mul = 3;
        div = 2;
        break;
    }
    set_sid_coefficients(base, coef, mul, div);
    return 0;
}

int U64Config :: setMixer(ConfigItem *it)
{
    // Now, configure the mixer
    volatile uint8_t *mixer = (volatile uint8_t *)U64_AUDIO_MIXER;
    ConfigStore *cfg = it->store;

    for(int i=0; i<10; i++) {
        uint8_t vol = volume_ctrl[cfg->get_value(CFG_MIXER0_VOL + i)];
        uint8_t pan = cfg->get_value(CFG_MIXER0_PAN + i);
        uint16_t panL = pan_ctrl[pan];
        uint16_t panR = pan_ctrl[10 - pan];
        uint8_t vol_left = (panL * vol) >> 8;
        uint8_t vol_right = (panR * vol) >> 8;
        *(mixer++) = vol_right;
        *(mixer++) = vol_left;
    }
    return 0;
}

int U64Config :: enableDisableSpeaker(ConfigItem *it)
{
    ConfigStore *cfg = it->store;
    if (it->getValue()) {
        for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
            cfg->enable(b);
        }
    } else {
        for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
            cfg->disable(b);
        }
    }
    setSpeakerMixer(it);
    return 1;
}

int U64Config :: setSpeakerMixer(ConfigItem *it)
{
    // Now, configure the mixer
    volatile uint8_t *mixer = (volatile uint8_t *)U64_SPEAKER_MIXER;
    ConfigStore *cfg = it->store;

    if (cfg->get_value(CFG_SPEAKER_EN) == 0) {
        // Disable speaker mixer
        for(int i=0; i<20; i++) {
            *(mixer++) = 0;
        }
        return 0;
    }
    for(int i=0; i<10; i++) {
        uint8_t vol = volume_ctrl[cfg->get_value(CFG_MIXER0_VOL + i)];
        *(mixer++) = vol;
        *(mixer++) = vol;
    }
    return 0;
}

int U64Config :: setPllOffset(ConfigItem *it)
{
	if(it) {
//		pllOffsetPpm(it->getValue()); // Set correct mfrac
	}
    return 0;
}

int U64Config :: setScanMode(ConfigItem *it)
{
	if(it) {
//		SetScanMode(it->value);
	}
    return 0;
}

int U64Config :: setLedSelector(ConfigItem *it)
{
    if(it) {
        ConfigStore *cfg = it->store;
        uint8_t sel0 = (uint8_t)cfg->get_value(CFG_LED_SELECT_0);
        uint8_t sel1 = (uint8_t)cfg->get_value(CFG_LED_SELECT_1);
        U64_CASELED_SELECT = (sel1 << 4) | sel0;
    }
    return 0;
}

int U64Config :: setCpuSpeed(ConfigItem *it)
{
    int ret = 0;

    if(it) {
        ConfigStore *cfg = it->store;
        switch(it->definition->id) {
        case CFG_SPEED_REGS:
            ret = 1;
            switch(it->getValue()) {
            case 0: // Off
                cfg->disable(CFG_SPEED_PREF);
                cfg->disable(CFG_BADLINES_EN);
                cfg->disable(CFG_SUPERCPU_DET);
                break;
            default:
                cfg->enable(CFG_SPEED_PREF);
                cfg->enable(CFG_BADLINES_EN);
                cfg->enable(CFG_SUPERCPU_DET);
                break;
            }
        }
        uint8_t prefRegValue, turboRegs;
        if (cfg->get_value(CFG_SPEED_REGS) == 0) { // Off
            prefRegValue = 0x80;
            turboRegs = 0;
        } else {
            prefRegValue = cfg->get_value(CFG_SPEED_PREF) | (cfg->get_value(CFG_BADLINES_EN) << 7);
            turboRegs = speedregs_regvalues[cfg->get_value(CFG_SPEED_REGS)] + (cfg->get_value(CFG_SUPERCPU_DET) ? 0x02 : 0x00);
        }
        printf("Speed regs: %b %b\n", turboRegs, prefRegValue);
        C64_TURBOREGS_EN = turboRegs;
        C64_SPEED_PREFER = prefRegValue;
        C64_SPEED_UPDATE = 1;
    }
    return ret;
}

int U64Config :: setSidEmuParams(ConfigItem *it)
{
    int value = it->getValue();
    switch(it->definition->id) {
    case CFG_EMUSID1_RESONANCE:
        C64_EMUSID1_RES = value;
        break;
    case CFG_EMUSID2_RESONANCE:
        C64_EMUSID2_RES = value;
        break;
    case CFG_EMUSID1_WAVES:
        C64_EMUSID1_WAVES = value;
        break;
    case CFG_EMUSID2_WAVES:
        C64_EMUSID2_WAVES = value;
        break;
    case CFG_EMUSID1_DIGI:
        C64_EMUSID1_DIGI = value;
        break;
    case CFG_EMUSID2_DIGI:
        C64_EMUSID2_DIGI = value;
        break;
    }
    return 0;
}

#define MENU_U64_SAVEEDID 1
#define MENU_U64_WIFI_DISABLE 3
#define MENU_U64_WIFI_ENABLE 4
#define MENU_U64_WIFI_BOOT 5
#define MENU_U64_DETECT_SIDS 6
#define MENU_U64_WIFI_DOWNLOAD 7
#define MENU_U64_POKE 8
#define MENU_U64_WIFI_ECHO 9
#define MENU_U64_UART_ECHO 10

void U64Config :: create_task_items(void)
{
    TaskCategory *dev = TasksCollection :: getCategory("Developer", SORT_ORDER_DEVELOPER);
    myActions.poke      = new Action("Poke", SUBSYSID_U64, MENU_U64_POKE);
    myActions.saveedid  = new Action("Save EDID to file", SUBSYSID_U64, MENU_U64_SAVEEDID);
    myActions.siddetect = new Action("Detect SIDs", SUBSYSID_U64, MENU_U64_DETECT_SIDS);
    myActions.esp32off  = new Action("Disable ESP32", SUBSYSID_U64, MENU_U64_WIFI_DISABLE);
    myActions.esp32on   = new Action("Enable ESP32",  SUBSYSID_U64, MENU_U64_WIFI_ENABLE);
    myActions.esp32boot = new Action("Enable ESP32 Boot", SUBSYSID_U64, MENU_U64_WIFI_BOOT);

    dev->append(myActions.saveedid );
#if DEVELOPER > 0
    dev->append(myActions.poke      );
    dev->append(myActions.siddetect );
    dev->append(myActions.esp32off  );
    dev->append(myActions.esp32on   );
    dev->append(myActions.esp32boot );
#endif
}

void U64Config :: update_task_items(bool writablePath, Path *p)
{
    myActions.saveedid->setDisabled(!writablePath);
}

SubsysResultCode_e U64Config :: executeCommand(SubsysCommand *cmd)
{
	File *f = 0;
	FRESULT res;
	uint8_t edid[256];
	char name[32];
	FRESULT fres;
	uint32_t trans;
	name[0] = 0;
	int sid1, sid2;
	char sidString[40];
	C64 *machine;
	static char poke_buffer[16];
	uint32_t addr, value;

	switch(cmd->functionID) {
    case MENU_U64_SAVEEDID:
    	// Try to read EDID, just a hardware test
    	if ((getFpgaCapabilities() & CAPAB_ULTIMATE64) && (i2c)) {
    		U64_HDMI_REG = U64_HDMI_HPD_RESET;

    		if (U64_HDMI_REG & U64_HDMI_HPD_CURRENT) {
    			U64_HDMI_REG = U64_HDMI_DDC_ENABLE;
    			printf("Monitor detected, now reading EDID.\n");
                i2c->i2c_lock("EDID Read");
                i2c->set_channel(I2C_CHANNEL_HDMI);
    			if (i2c->i2c_read_block(0xA0, 0x00, edid, 256) == 0) {
    				if (cmd->user_interface->string_box("Reading EDID OK. Save to:", name, 31) > 0) {
    					set_extension(name, ".bin", 32);
    			        fres = fm->fopen(cmd->path.c_str(), name, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
    			        if (fres == FR_OK) {
    			        	f->write(edid, 256, &trans);
    			        	fm->fclose(f);
    			        }
    				}
    			} else {
    			    cmd->user_interface->popup("Failed to read EDID", BUTTON_OK);
    			}
    			U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
                i2c->i2c_unlock();
    		}
    	}
    	break;

    case MENU_U64_POKE:
        if (cmd->user_interface->string_box("Poke AAAA,DD", poke_buffer, 16)) {

            sscanf(poke_buffer, "%x,%x", &addr, &value);

            C64 *machine = C64 :: getMachine();
            portENTER_CRITICAL();

            if (!(C64_STOP & C64_HAS_STOPPED)) {
                machine->stop(false);

                C64_POKE(addr, value);

                machine->resume();
            } else {
                C64_POKE(addr, value);
            }
            portEXIT_CRITICAL();
        }
        break;

    case MENU_U64_DETECT_SIDS:
        machine = C64 :: getMachine();
        machine->stop(false);
        S_SidDetector(sid1, sid2);
        machine->resume();
        sprintf(sidString, "Socket1: %s  Socket2: %s", sid_types[sid1], sid_types[sid2]);
        cmd->user_interface->popup(sidString, BUTTON_OK);
        effectuate_settings();
        break;

    case 0xFFFE: // dummy
        DetectSidImpl(edid);
        break;

    default:
    	printf("U64 does not know this command\n");
        return SSRET_NOT_IMPLEMENTED;
    }
    return SSRET_OK;
}


uint8_t U64Config :: GetSidType(int slot)
{
    uint8_t val;

    switch(slot) {
    case 0: // slot 1A
        if (!(sockets.cfg->get_value(CFG_SOCKET1_ENABLE))) {
            return 0;
        }
        val = sockets.cfg->get_value(CFG_SID1_TYPE);
        switch (val) {
        case SID_TYPE_NONE:
            return 0;
        case SID_TYPE_6581:
            return 1;
        case SID_TYPE_8580:
            return 2;
        case SID_TYPE_FPGASID:
        case SID_TYPE_ARMSID:
        case SID_TYPE_ARM2SID:
        case SID_TYPE_SWINSID:
        case SID_TYPE_FPGASID_DUKESTAH:
            return 3; // either type, we can TELL the fpgaSID to configure itself in the right mode
        default:
            return 0;
        }
        break;

    case 1: // slot 2A
        if (!(sockets.cfg->get_value(CFG_SOCKET2_ENABLE))) {
            return 0;
        }
        val = sockets.cfg->get_value(CFG_SID2_TYPE);
        switch (val) {
        case SID_TYPE_NONE:
            return 0;
        case SID_TYPE_6581:
            return 1;
        case SID_TYPE_8580:
            return 2;
        case SID_TYPE_FPGASID:
        case SID_TYPE_ARMSID:
        case SID_TYPE_ARM2SID:
        case SID_TYPE_SWINSID:
        case SID_TYPE_FPGASID_DUKESTAH:
            return 3; // either type, we can TELL the fpgaSID to configure itself in the right mode
        default:
            return 0;
        }
        break;

    case 2:
    case 3:
    case 6:
    case 7:
        if (cfg->get_value(CFG_ALLOW_EMUSID)) {
            return 3;
        } else {
            return 0;
        }

    case 4: // slot 1B
        val = sockets.cfg->get_value(CFG_SID1_TYPE);
        if (val > 2) {
            return 3;
        }
        return 0; // no "other" SID available in slot 1.

    case 5: // slot 2B
        val = sockets.cfg->get_value(CFG_SID2_TYPE);
        if (val > 2) {
            return 3;
        }
        return 0; // no "other" SID available in slot 2.

    }
    return 0;
}

bool U64Config :: SetSidAddress(int slot, bool single, uint8_t actualType, uint8_t base)
{
    uint8_t other = 0x00;
    if (actualType >= 3) {
        other = stereo_bits[C64_STEREO_ADDRSEL_BAK];
    }

    // Kludge: If this is the only SID, just enable all address bits
    if (single) {
        other = 0x3F;
    }

    if (slot < 4) { // for the first four SIDs, we can set the base address
        // if the base address already has this "other" bit set, we cannot map it.
        if (base & other) {
            return false;
        }

        switch(slot) {
        case 0: // Socket 1 address
            C64_SID1_BASE = C64_SID1_BASE_BAK = base;
            C64_SID1_MASK = C64_SID1_MASK_BAK = 0xFE & ~other;
            return true;
        case 1: // Socket 2 address
            C64_SID2_BASE = C64_SID2_BASE_BAK = base;
            C64_SID2_MASK = C64_SID2_MASK_BAK = 0xFE & ~other;
            return true;
        case 2: // EmuSID 1 address
            C64_EMUSID1_BASE = C64_EMUSID1_BASE_BAK = base;
            C64_EMUSID1_MASK = C64_EMUSID1_MASK_BAK = 0xFE & ~other;
            return true;
        case 3: // EmuSID 2 address
            C64_EMUSID2_BASE = C64_EMUSID2_BASE_BAK = base;
            C64_EMUSID2_MASK = C64_EMUSID2_MASK_BAK = 0xFE & ~other;
            return true;
        }
        return false;
    }
    // ok, so now we are ODD.. Let's see if we can use the existing mapping of the other channel

    switch(slot) {
    case 4:
        return ((C64_SID1_BASE_BAK | other) == base);
    case 5:
        return ((C64_SID2_BASE_BAK | other) == base);
    case 6:
        return ((C64_EMUSID1_BASE_BAK | other) == base);
    case 7:
        return ((C64_EMUSID2_BASE_BAK | other) == base);
    }
    return false;
}

void U64Config :: SetSidType(int slot, uint8_t sidType)
{
    printf("Set SID type of logical SID %d to %d.\n", slot, sidType);
    if (slot < 2) {
        SidDevice *dev = sidDevice[slot];
        if (dev) {
            dev->SetSidType(sidType);
        } else {
            printf("Null pointer.\n");
        }
    }
}

bool U64Config :: MapSid(int index, int totalCount, uint16_t& mappedSids, uint8_t *mappedOnSlot, t_sid_definition *requested, bool any)
{
    // definition of SID slots:
    // 0 : Socket 1
    // 1 : Socket 2
    // 2/3: Internally emulated SIDs.
    // 4...7: "B" (other channel of a socket, addressed with address select pin)

    bool found = false;

    static const char *sidTypes[] = { "None", "6581", "8580", "Either" };

    for (int i=0; i < 8; i++) {
        if (mappedSids & (1 << i)) {
            continue;
        }
        uint8_t actualType = GetSidType(i);
        if ((actualType & requested->sidType) || (any && actualType)) { //  bit mask != 0
            if (SetSidAddress(i, (totalCount == 1), actualType, requested->baseAddress)) {
                mappedSids |= (1 << i);
                mappedOnSlot[index] = i;
                printf("Trying to map SID %d (type %s) on logical SID %d (type %s), at address $D%02x0\n", index,
                        sidTypes[requested->sidType & 3], i, sidTypes[actualType], requested->baseAddress);
                found = true;
                if (actualType == 3) {
                    SetSidType(i, requested->sidType);
                }
                break;
            }
        }
    }
    return found;
}

void U64Config :: SetMixerAutoSid(uint8_t *slots, int count)
{
    static const uint8_t channelMap[4] = { 4, 6, 0, 2 };

    uint8_t selectedVolumes[4];
    ConfigStore *cs = this->mixercfg.cfg;
    selectedVolumes[0] = volume_ctrl[cs->get_value(CFG_MIXER2_VOL)];
    selectedVolumes[1] = volume_ctrl[cs->get_value(CFG_MIXER3_VOL)];
    selectedVolumes[2] = volume_ctrl[cs->get_value(CFG_MIXER0_VOL)];
    selectedVolumes[3] = volume_ctrl[cs->get_value(CFG_MIXER1_VOL)];

    // first mute the SIDs
    volatile uint8_t *mixer = (volatile uint8_t *)U64_AUDIO_MIXER;
    for (int i=0;i<8;i++) {
        mixer[i] = 0;
    }

    if (count > 4) {
        printf("I don't know how to map %d inputs on the mixer.\n", count);
        count = 4;
    }

    static const int channelPanning[20] = { 0, 0, 0, 0, 5, 0, 0, 0, 3, 7, 0, 0, 5, 2, 8, 0, 3, 7, 1, 9 };
    //                                      -  -  -  -|CNT -  -  - |L2 R2 -  - |CN L3 R3 - |L2 R2 L4 R4

    for (int i=0;i<count;i++) {
        uint8_t volume = selectedVolumes[slots[i]];
        if (!volume) { // setting was OFF => default to 0 dB
            volume = 0x80;
        }
        int pan = channelPanning[4*count + i];
        printf("Sid %d was mapped to slot %d, which has volume setting %02x. Setting pan to %s.\n", i, slots[i], volume, pannings[pan]);
        mixer[0 + channelMap[slots[i]]] = (pan_ctrl[10-pan] * volume) >> 8;
        mixer[1 + channelMap[slots[i]]] = (pan_ctrl[pan] * volume) >> 8;
    }
}

void U64Config :: unmapAllSids(void)
{
    C64_SID1_BASE = C64_SID1_BASE_BAK = UNMAPPED_BASE;
    C64_SID1_MASK = C64_SID1_MASK_BAK = UNMAPPED_MASK;
    C64_SID2_BASE = C64_SID2_BASE_BAK = UNMAPPED_BASE;
    C64_SID2_MASK = C64_SID2_MASK_BAK = UNMAPPED_MASK;
    C64_EMUSID1_BASE = C64_EMUSID1_BASE_BAK = UNMAPPED_BASE;
    C64_EMUSID1_MASK = C64_EMUSID1_MASK_BAK = UNMAPPED_MASK;
    C64_EMUSID2_BASE = C64_EMUSID2_BASE_BAK = UNMAPPED_BASE;
    C64_EMUSID2_MASK = C64_EMUSID2_MASK_BAK = UNMAPPED_MASK;
}

bool U64Config :: SidAutoConfig(int count, t_sid_definition *requested)
{
    // the first reset of the machine should not re-initialize the settings from the config
    skipReset = true;

    if (!(cfg->get_value(CFG_PLAYER_AUTOCONFIG))) {
        printf("SID Player Autoconfig Disabled, not configuring.\n");
        return true;
    }

    uint16_t mappedSids;
    uint8_t mappedOnSlot[8];

    if (count > 8) {
        count = 8;
    }

    unmapAllSids();
    memset(mappedOnSlot, 0, 8);
    mappedSids = 0;
    bool failed = false;

    for (int i=0; i < count; i++) {
        if (!MapSid(i, count, mappedSids, mappedOnSlot, &requested[i], false)) {
            failed = true;
        }
    }
    if (failed) {
        unmapAllSids();
        mappedSids = 0;
        memset(mappedOnSlot, 0, 8);
        failed = false;
        for (int i=count-1; i >= 0; i--) {
            if (!MapSid(i, count, mappedSids, mappedOnSlot, &requested[i], false)) {
                failed = true;
            }
        }
    }
    if (failed) {
        unmapAllSids();
        mappedSids = 0;
        memset(mappedOnSlot, 0, 8);
        failed = false;
        for (int i=0; i < count; i++) {
            if (!MapSid(i, count, mappedSids, mappedOnSlot, &requested[i], true)) {
                failed = true;
            }
        }
    }
    if (failed) {
        printf("No valid mapping found.\n");
        return false;
    }

    printf("Resulting address map: Slot1: %02X/%02X (%s) Slot2: %02X/%02X (%s)  Emu1: %02X/%02X  Emu2: %02X/%02X\n",
            C64_SID1_BASE_BAK, C64_SID1_MASK_BAK, en_dis[C64_SID1_EN_BAK],
            C64_SID2_BASE_BAK, C64_SID2_MASK_BAK, en_dis[C64_SID2_EN_BAK],
            C64_EMUSID1_BASE_BAK, C64_EMUSID1_MASK_BAK,
            C64_EMUSID2_BASE_BAK, C64_EMUSID2_MASK_BAK );

    SetMixerAutoSid(mappedOnSlot, count);
    return true;
}

// this function overrides the weak function in FiletypeSID.. ;-)
bool SidAutoConfig(int count, t_sid_definition *requested)
{
    return u64_configurator.SidAutoConfig(count, requested);
}

extern "C" {
    void ResetInterruptHandlerU64()
    {
        u64_configurator.ResetHandler();
    }
}


const int32_t c_filter2_coef_pal_alt[] = {
    5, 4, 5, 6, 8, 10, 13, 16, 19, 22, 26, 31, 36, 41, 47, 54, 61, 68, 75, 84, 92, 101,
    110, 119, 128, 137, 146, 155, 163, 171, 179, 186, 192, 197, 201, 203, 204, 204, 202,
    198, 193, 185, 176, 164, 150, 134, 116, 96, 73, 49, 22, -6, -36, -68, -100, -134,
    -168, -203, -238, -273, -308, -341, -374, -405, -434, -460, -484, -505, -522, -536,
    -546, -552, -553, -549, -541, -528, -511, -488, -461, -429, -392, -352, -307, -259,
    -208, -155, -99, -42, 16, 74, 132, 189, 243, 296, 345, 390, 430, 466, 495, 518, 535,
    544, 545, 539, 525, 502, 472, 433, 388, 334, 274, 208, 135, 58, -23, -108, -194,
    -282, -370, -457, -542, -623, -700, -771, -836, -892, -940, -978, -1005, -1021,
    -1026, -1018, -997, -964, -918, -859, -788, -705, -611, -507, -394, -273, -145,
    -12, 125, 264, 404, 542, 677, 806, 929, 1044, 1147, 1239, 1317, 1380, 1427, 1456,
    1467, 1459, 1432, 1384, 1318, 1231, 1126, 1003, 863, 707, 537, 355, 162, -38,
    -244, -453, -663, -871, -1073, -1268, -1452, -1622, -1777, -1913, -2028, -2121,
    -2189, -2230, -2244, -2229, -2184, -2110, -2007, -1874, -1714, -1526, -1314,
    -1078, -822, -547, -258, 42, 350, 663, 975, 1283, 1583, 1871, 2142, 2393, 2619,
    2817, 2984, 3116, 3211, 3266, 3279, 3250, 3177, 3059, 2898, 2694, 2449, 2164,
    1842, 1487, 1101, 690, 258, -191, -650, -1115, -1578, -2034, -2477, -2900, -3298,
    -3664, -3993, -4278, -4516, -4701, -4830, -4899, -4905, -4846, -4722, -4531, -4274,
    -3953, -3569, -3126, -2628, -2079, -1485, -852, -187, 501, 1206, 1917, 2627, 3325,
    4003, 4650, 5257, 5816, 6316, 6749, 7108, 7385, 7574, 7668, 7664, 7557, 7345, 7028,
    6604, 6076, 5447, 4720, 3901, 2997, 2016, 967, -139, -1289, -2472, -3673, -4879,
    -6073, -7241, -8366, -9432, -10422, -11321, -12113, -12782, -13314, -13695, -13912,
    -13953, -13809, -13470, -12929, -12181, -11221, -10048, -8661, -7063, -5258, -3251,
    -1051, 1332, 3887, 6600, 9456, 12437, 15525, 18701, 21943, 25230, 28538, 31845,
    35126, 38358, 41517, 44580, 47522, 50323, 52959, 55412, 57662, 59690, 61482, 63023,
    64301, 65306, 66029, 66466, 66612, 66466, 66029, 65306, 64301, 63023, 61482, 59690,
    57662, 55412, 52959, 50323, 47522, 44580, 41517, 38358, 35126, 31845, 28538, 25230,
    21943, 18701, 15525, 12437, 9456, 6600, 3887, 1332, -1051, -3251, -5258, -7063,
    -8661, -10048, -11221, -12181, -12929, -13470, -13809, -13953, -13912, -13695,
    -13314, -12782, -12113, -11321, -10422, -9432, -8366, -7241, -6073, -4879, -3673,
    -2472, -1289, -139, 967, 2016, 2997, 3901, 4720, 5447, 6076, 6604, 7028, 7345,
    7557, 7664, 7668, 7574, 7385, 7108, 6749, 6316, 5816, 5257, 4650, 4003, 3325,
    2627, 1917, 1206, 501, -187, -852, -1485, -2079, -2628, -3126, -3569, -3953,
    -4274, -4531, -4722, -4846, -4905, -4899, -4830, -4701, -4516, -4278, -3993,
    -3664, -3298, -2900, -2477, -2034, -1578, -1115, -650, -191, 258, 690, 1101,
    1487, 1842, 2164, 2449, 2694, 2898, 3059, 3177, 3250, 3279, 3266, 3211, 3116,
    2984, 2817, 2619, 2393, 2142, 1871, 1583, 1283, 975, 663, 350, 42, -258, -547,
    -822, -1078, -1314, -1526, -1714, -1874, -2007, -2110, -2184, -2229, -2244,
    -2230, -2189, -2121, -2028, -1913, -1777, -1622, -1452, -1268, -1073, -871,
    -663, -453, -244, -38, 162, 355, 537, 707, 863, 1003, 1126, 1231, 1318, 1384,
    1432, 1459, 1467, 1456, 1427, 1380, 1317, 1239, 1147, 1044, 929, 806, 677,
    542, 404, 264, 125, -12, -145, -273, -394, -507, -611, -705, -788, -859, -918,
    -964, -997, -1018, -1026, -1021, -1005, -978, -940, -892, -836, -771, -700,
    -623, -542, -457, -370, -282, -194, -108, -23, 58, 135, 208, 274, 334, 388,
    433, 472, 502, 525, 539, 545, 544, 535, 518, 495, 466, 430, 390, 345, 296, 243,
    189, 132, 74, 16, -42, -99, -155, -208, -259, -307, -352, -392, -429, -461,
    -488, -511, -528, -541, -549, -553, -552, -546, -536, -522, -505, -484, -460,
    -434, -405, -374, -341, -308, -273, -238, -203, -168, -134, -100, -68, -36,
    -6, 22, 49, 73, 96, 116, 134, 150, 164, 176, 185, 193, 198, 202, 204, 204,
    203, 201, 197, 192, 186, 179, 171, 163, 155, 146, 137, 128, 119, 110, 101,
    92, 84, 75, 68, 61, 54, 47, 41, 36, 31, 26, 22, 19, 16, 13, 10, 8, 6, 5, 4, 5 };


/*
-- Generated with paramters: Gain 2.8  (remember: Pal had 14 while oversampling factor was 15, thus here we use 14/15 * 3 = 2.8)
-- Fs = 383400 Hz (3 * 127800). Pass : 0-19000, Stopband: 23000 - 191700, 0.4 dB / -80 dB
-- Taps = 507 (510 did not work)
-- Result = 0.01 dB ripple, -108 dB atten!  The perfect filter ;)
*/

const int32_t c_filter2_coef_ntsc[] = {
    -1, -1, -1, -2, -3, -4, -5, -6, -7, -8, -8, -8, -7, -5, -3, 0, 4, 7, 11, 15, 18, 20, 21,
    20, 18, 15, 11, 6, 0, -5, -10, -14, -16, -16, -14, -9, -4, 4, 11, 18, 24, 28, 30, 29, 24,
    17, 7, -4, -15, -25, -33, -38, -39, -35, -27, -16, -1, 15, 30, 43, 53, 57, 55, 47, 33, 14,
    -7, -29, -49, -65, -75, -77, -70, -55, -32, -5, 25, 54, 79, 96, 104, 100, 85, 59, 25, -14,
    -54, -90, -118, -134, -136, -122, -94, -53, -4, 48, 98, 139, 167, 178, 168, 139, 93, 33,
    -34, -100, -159, -202, -225, -224, -196, -145, -75, 8, 94, 174, 238, 278, 288, 266, 212,
    131, 32, -77, -182, -271, -334, -361, -349, -296, -206, -89, 45, 179, 300, 392, 443, 446,
    398, 302, 167, 6, -163, -320, -449, -531, -557, -520, -421, -270, -80, 127, 329, 502, 625,
    682, 664, 568, 403, 184, -66, -320, -548, -723, -823, -833, -747, -572, -323, -26, 287,
    582, 822, 979, 1030, 965, 785, 508, 159, -224, -598, -921, -1152, -1261, -1229, -1055,
    -751, -346, 118, 590, 1016, 1345, 1534, 1556, 1399, 1072, 606, 47, -546, -1105, -1565,
    -1867, -1969, -1848, -1507, -973, -298, 449, 1185, 1826, 2291, 2517, 2464, 2121, 1511,
    687, -269, -1255, -2160, -2873, -3301, -3372, -3055, -2356, -1328, -63, 1312, 2647, 3785,
    4579, 4906, 4683, 3883, 2537, 740, -1353, -3540, -5585, -7236, -8252, -8424, -7592, -5669,
    -2651, 1379, 6251, 11720, 17477, 23177, 28462, 32991, 36464, 38646, 39390, 38646, 36464,
    32991, 28462, 23177, 17477, 11720, 6251, 1379, -2651, -5669, -7592, -8424, -8252, -7236,
    -5585, -3540, -1353, 740, 2537, 3883, 4683, 4906, 4579, 3785, 2647, 1312, -63, -1328,
    -2356, -3055, -3372, -3301, -2873, -2160, -1255, -269, 687, 1511, 2121, 2464, 2517,
    2291, 1826, 1185, 449, -298, -973, -1507, -1848, -1969, -1867, -1565, -1105, -546, 47,
    606, 1072, 1399, 1556, 1534, 1345, 1016, 590, 118, -346, -751, -1055, -1229, -1261,
    -1152, -921, -598, -224, 159, 508, 785, 965, 1030, 979, 822, 582, 287, -26, -323, -572,
    -747, -833, -823, -723, -548, -320, -66, 184, 403, 568, 664, 682, 625, 502, 329, 127,
    -80, -270, -421, -520, -557, -531, -449, -320, -163, 6, 167, 302, 398, 446, 443, 392,
    300, 179, 45, -89, -206, -296, -349, -361, -334, -271, -182, -77, 32, 131, 212, 266,
    288, 278, 238, 174, 94, 8, -75, -145, -196, -224, -225, -202, -159, -100, -34, 33, 93,
    139, 168, 178, 167, 139, 98, 48, -4, -53, -94, -122, -136, -134, -118, -90, -54, -14,
    25, 59, 85, 100, 104, 96, 79, 54, 25, -5, -32, -55, -70, -77, -75, -65, -49, -29, -7,
    14, 33, 47, 55, 57, 53, 43, 30, 15, -1, -16, -27, -35, -39, -38, -33, -25, -15, -4, 7,
    17, 24, 29, 30, 28, 24, 18, 11, 4, -4, -9, -14, -16, -16, -14, -10, -5, 0, 6, 11, 15,
    18, 20, 21, 20, 18, 15, 11, 7, 4, 0, -3, -5, -7, -8, -8, -8, -7, -6, -5, -4, -3, -2,
    -1, -1, -1, 0, 0, 0, 0, 0 };


void U64Config :: SetResampleFilter(t_video_mode m)
{
    const t_video_color_timing *ct = color_timings[(int)m];
    int mode = (ct->audio_div == 77) ? 0 : 1;

    const uint32_t *pulFilter = (mode == 0) ? (uint32_t*)c_filter2_coef_pal_alt : (uint32_t*)c_filter2_coef_ntsc;
    int size = (mode == 0) ? 675 : 512;

    U64_RESAMPLE_RESET = 1;
    U64_RESAMPLE_FLUSH = 1;
    for(int i=0; i < size; i++) {
        U64_RESAMPLE_DATA = *(pulFilter ++);
    }
    U64_RESAMPLE_LABOR = (mode == 0) ? 45 : 169; // 675 / 15 and 510 / 3
    U64_RESAMPLE_FLUSH = 0;
}

#pragma GCC push_options
#pragma GCC optimize ("O1")

void U64Config :: DetectSidImpl(uint8_t *buffer)
{
    uint8_t result1, result2, result3, result4;

    while (C64_PEEK(0xD012) != 0xFF)
        ;

    for(int x = 0; x < 16; x++) {

        C64_POKE(0xD412, 0x48);
        C64_POKE(0xD40F, 0x48);
        C64_POKE(0xD412, 0x24);
        C64_POKE(0xD41F, 0x00);
        C64_POKE(0xD41F, 0x00);
        C64_POKE(0xD41F, 0x00);
        result1 = C64_PEEK(0xD41B);
        C64_POKE(0xD41F, 0x00);
        C64_POKE(0xD41F, 0x00);
        C64_POKE(0xD41F, 0x00);
        C64_POKE(0xD41F, 0x00);
        result2 = C64_PEEK(0xD41B);

        C64_POKE(0xD512, 0x48);
        C64_POKE(0xD50F, 0x48);
        C64_POKE(0xD512, 0x24);
        C64_POKE(0xD51F, 0x00);
        C64_POKE(0xD51F, 0x00);
        C64_POKE(0xD51F, 0x00);
        result3 = C64_PEEK(0xD51B);
        C64_POKE(0xD51F, 0x00);
        C64_POKE(0xD51F, 0x00);
        C64_POKE(0xD51F, 0x00);
        C64_POKE(0xD51F, 0x00);
        result4 = C64_PEEK(0xD51B);

        buffer[x + 0] = result1;
        buffer[x + 16] = result2;
        buffer[x + 32] = result3;
        buffer[x + 48] = result4;
    }


    /*
    detectSidModel  lda #$ff        ; make sure the check is not done on a bad line
    -               cmp $d012
                    bne -
                    lda #$48        ; test bit should be set
                    sta $d412
                    sta $d40f
                    lsr             ; activate sawtooth waveform
                    sta $d412
                    lda $d41b
                    tax
                    and #$fe
                    bne unknownSid  ; unknown SID chip, most likely emulated or no SID in socket
                    lda $d41b       ; try to read another time where the value should always be $03 on a real SID for all SID models
                    cmp #$03
                    beq +
    unknownSid      ldx #$02
    +               txa
                    rts             ; output 0 = 8580, 1 = 6581, 2 = unknown
*/


}

#pragma GCC pop_options

void U64Config :: S_SetupDetectionAddresses()
{
    // Configure Socket 1 to be at $D400 and Socket 2 to be at $D500
    // UltiSid is set to $D600 to make sure it doesn't trigger
    C64_SID1_BASE = 0x40;
    C64_SID2_BASE = 0x50;
    C64_SID1_MASK = 0xF0; // only check upper 8 bits, these bits may be faster through DMA
    C64_SID2_MASK = 0xF0; // only check upper 8 bits
    C64_EMUSID1_BASE = 0x60;
    C64_EMUSID2_BASE = 0x60;
    C64_EMUSID1_MASK = 0xFE;
    C64_EMUSID2_MASK = 0xFE;
    C64_SID1_EN = 1;
    C64_SID2_EN = 1;
    C64_STEREO_ADDRSEL = 0;
    wait_ms(1);
}

void U64Config :: S_RestoreDetectionAddresses()
{
    C64_SID1_BASE    = C64_SID1_BASE_BAK;
    C64_SID2_BASE    = C64_SID2_BASE_BAK;
    C64_SID1_MASK    = C64_SID1_MASK_BAK;
    C64_SID2_MASK    = C64_SID2_MASK_BAK;
    C64_EMUSID1_BASE = C64_EMUSID1_BASE_BAK;
    C64_EMUSID2_BASE = C64_EMUSID2_BASE_BAK;
    C64_EMUSID1_MASK = C64_EMUSID1_MASK_BAK;
    C64_EMUSID2_MASK = C64_EMUSID2_MASK_BAK;
    C64_SID1_EN = C64_SID1_EN_BAK;
    C64_SID2_EN = C64_SID2_EN_BAK;
    C64_STEREO_ADDRSEL = C64_STEREO_ADDRSEL_BAK;
}

int U64Config :: S_SidDetector(int &sid1, int &sid2)
{
    S_SetupDetectionAddresses();

    C64 :: hard_stop();

    uint8_t buffer[64];

    sid1 = sid2 = 0;

    for (int attempt = 0; attempt < 3; attempt++) {
        // Prepare the machine to execute the detection code

    	portENTER_CRITICAL();
        DetectSidImpl(buffer);
        portEXIT_CRITICAL();

        // Now analyze the data
        if ((buffer[17] == 2) && (buffer[1] == 0))  {
            sid1 = 2;
        } else if ((buffer[17] == 2) && (buffer[1] == 1)) {
            sid1 = 1;
        }

        if ((buffer[49] == 2) && (buffer[33] == 0))  {
            sid2 = 2;
        } else if ((buffer[49] == 2) && (buffer[33] == 1)) {
            sid2 = 1;
        }

        // check consistency. If not consistent, invalidate. If consistent, break
        bool consistent = true;
        for(int i=2;i<16;i++) {
            if (sid1) {
                if (buffer[i]    != buffer[1])  { consistent = false; sid1 = 0; break; }
                if (buffer[i+16] != buffer[17]) { consistent = false; sid1 = 0; break; }
            }
            if (sid2) {
                if (buffer[i+32] != buffer[33]) { consistent = false; sid2 = 0; break; }
                if (buffer[i+48] != buffer[49]) { consistent = false; sid2 = 0; break; }
            }
        }

        if (consistent) {
            break;
        }
    }
    return 1;
}

int swap_joystick()
{
    if (!isEliteBoard()) {
        return MENU_NOP;
    }

    ConfigItem *item = u64_configurator.cfg->find_item(CFG_JOYSWAP);
    int swap = item->getValue();
    swap ^= 1;
    item->setValue(swap);
    U64II_KEYB_JOY  = (uint8_t)swap;
    C64_PLD_JOYCTRL = (uint8_t)(swap ^ 1);
    C64_PADDLE_SWAP = (uint8_t)swap;

    printf("*S%d*", swap);

    // swap performed, now exit menu
    return MENU_HIDE;
}

void U64Config :: auto_mirror(uint8_t *base, uint8_t *mask, uint8_t *split, int count)
{
    // this function sets 'don't care' bits in the mask for A5..A9 if
    // all of the address bits are the same for all decodes, OR when they
    // are already don't care. This fills up the address space with
    // mirrors without introducing overlaps that were not already there.

    //printf("Before:\n");
    //show_mapping(base, mask, split, count);

    for (int i=0; i<4; i++) {
        base[i] &= ~split[i];
        mask[i] &= ~split[i];
    }

    for (int a = 5; a <= 9; a++) {
        bool same = true;
        bool set = false;
        int bit = 0, temp;

        for (int i = 0; i < count; i++) {
            // only consider SIDs in D400-D7FF range
            if ((base[i] < 0x40) || (base[i] >= 0x80)) {
                continue;
            }
            // masks bits that are already "don't care" are not considered
//            if (mask[i] & (1 << (a - 4)) == 0) {
//                continue;
//            }
            if (!set) {
                bit = (base[i] >> (a - 4)) & 1;
                bit <<= 1;
                bit |= (mask[i] >> (a - 4)) & 1;
                set = true;
            } else {
                temp = (base[i] >> (a - 4)) & 1;
                temp <<= 1;
                temp |= (mask[i] >> (a - 4)) & 1;

                if (temp != bit) {
                    same = false;
                }
            }
        }
        if (same) {
            for (int i = 0; i < count; i++) {
                // only consider SIDs in D400-D7FF range
                if ((base[i] < 0x40) || (base[i] >= 0x80)) {
                    continue;
                }
                // clear corresponding mask bit
                mask[i] &= ~(1 << (a - 4));
                base[i] &= ~(1 << (a - 4));
            }
        }
    }

    //printf("After:\n");
    //show_mapping(base, mask, split, count);
}

void U64Config :: show_mapping(uint8_t *base, uint8_t *mask, uint8_t *split, int count)
{
    // split can be single bit, in which it is easy to determine whether it is A or B, just check for 0
    // In case split is two bits, we need to see if it is A, B, C, or D.
    // In case split is 06 => 00, 02, 04, 06 => divide by 2
    // In case split is 12 => 00, 02, 10, 12 => divide by 2, if >= 8: do -6.
    // In case split is 18 => 00, 08, 10, 18 => divide by 8
/*
    printf("    DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD\n");
    printf("    44444444555555556666666677777777\n");
    printf("    02468ACE02468ACE02468ACE02468ACE\n");
    printf("    00000000000000000000000000000000\n");
    char m;

    for(int i=0;i<count;i++) {
        printf("%2d: ", i);
        for (int a = 0x40; a < 0x80; a+=2) {
            if ((a & mask[i]) == base[i]) {
                switch (split[i]) {
                case 0:
                    m = '*';
                    break;
                case 0x06:
                    m = 'A' + ((a & split[i]) >> 1);
                    break;
                case 0x18:
                    m = 'A' + ((a & split[i]) >> 3);
                    break;
                case 0x12:
                    m = 'A' + ((a & split[i] & 2) >> 1) + ((a & split[i] & 0x10) >> 3);
                    break;
                default:
                    m = (a & split[i]) ? 'B' : 'A';
                }
                printf("%c", m);
            } else {
                printf(" ");
            }
        }
        printf("\n");
    }
*/
    printf("   A9 A8 A7 A6 A5\n");
    for (int i=0; i < count; i++) {
        printf("%2d:", i);
        for(int a = 9-4; a >= 5-4; a--) {
            int m = (mask[i] >> a) & 1;
            int b = (base[i] >> a) & 1;
            if ((m == 1) && (b == 0)) {
                printf(" 0 ");
            } else if ((m == 1) && (b == 1)) {
                printf(" 1 ");
            } else if ((m == 0) && (b == 0)) {
                printf(" - ");
            } else {
                printf(" #!");
            }
        }
        printf("\n");
    }
}

void U64Config :: show_sid_addr(UserInterface *intf, ConfigItem *it)
{
    SidEditor *se = new SidEditor(intf, u64_configurator.sidaddressing.cfg);
    se->init(intf->screen, intf->keyboard);
    intf->activate_uiobject(se);
}

volatile uint8_t *U64Config :: access_socket_pre(int socket)
{
    C64 *machine = C64 :: getMachine();

    portENTER_CRITICAL();
    S_SetupDetectionAddresses();

    if (!(C64_STOP & C64_HAS_STOPPED)) {
        machine->stop(false);
        temporary_stop = true;
    } else {
        temporary_stop = false;
    }

    volatile uint8_t *base;
    if (socket) {
        base = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD500);
    } else {
        base = (volatile uint8_t *)(C64_MEMORY_BASE + 0xD400);
    }
    return base;
}

void U64Config :: access_socket_post(int socket)
{
    S_RestoreDetectionAddresses();

    C64 *machine = C64 :: getMachine();
    if (temporary_stop) {
        machine->resume();
    }

    portEXIT_CRITICAL();
}

void U64Config :: clear_ram()
{
    C64 *machine = C64 :: getMachine();
    machine->clear_ram();
}

bool U64Config :: IsMonitorHDMI()
{
    const uint8_t header_expected[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    uint8_t header[8];
    uint8_t extension[8];


    if ((U64_HDMI_REG & U64_HDMI_HPD_CURRENT) == 0) {
        printf("No HPD - no digital monitor attached.\n");
        return false;
    }

    if(!i2c) {
        printf("No I2C interface available.\n");
        return false;
    }

    U64_HDMI_REG = U64_HDMI_DDC_ENABLE;

    i2c->i2c_lock("HDMI check");
    i2c->set_channel(I2C_CHANNEL_HDMI);
    if (i2c->i2c_read_block(0xA0, 0x00, header, 8) != 0) {
        printf("EDID Read FAILED.\n");
        U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
        i2c->i2c_unlock();
        return false;
    }
    if (i2c->i2c_read_block(0xA0, 0x80, extension, 8) != 0) {
        printf("EDID Read FAILED.\n");
        U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
        i2c->i2c_unlock();
        return false;
    }
    U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
    i2c->i2c_unlock();

    if (memcmp(header, header_expected, 8) != 0) {
        printf("EDID Header incorrect.\n");
        return false;
    }
    if (extension[0] != 0x02) {
        printf("EDID Extension not of CEA type.\n");
        return false;
    }
    if ((extension[3] & 0x70) == 0) {
        printf("EDID no support for any HDMI specific stuff.\n");
        return false;
    }
    printf("EDID: Monitor is HDMI!\n");
    return true;
}

void U64Config :: list_palettes(ConfigItem *it, IndexedList<char *>& strings)
{
    // Always return at least the empty string
    char *empty = new char[1];
    *empty = 0;
    strings.append(empty);

    Path p;
    p.cd(DATA_DIRECTORY);
    IndexedList<FileInfo *>infos(16, NULL);
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->get_directory(&p, infos, NULL);
    if (fres != FR_OK) {
        return;
    }

    for(int i=0;i<infos.get_elements();i++) {
        FileInfo *inf = infos[i];
        if (strcmp(inf->extension, "VPL") == 0) {
            char *s = new char[1+strlen(inf->lfname)];
            strcpy(s, inf->lfname);
            strings.append(s);
        }
        delete inf;
    }
}

void U64Config :: set_palette_rgb(const uint8_t rgb[16][3])
{
#if U64 == 2
    volatile uint8_t *rgb_registers = (volatile uint8_t *)U64II_HDMI_PALETTE;
#else
    volatile uint8_t *rgb_registers = (volatile uint8_t *)C64_PALETTE;
#endif
    uint8_t yuv[16][3];
    for(int i=0; i<16; i++) {
        *(rgb_registers++) = rgb[i][0];
        *(rgb_registers++) = rgb[i][1];
        *(rgb_registers++) = rgb[i][2];
        rgb_registers++;
        rgb_to_yuv(rgb[i], yuv[i], false);
    }
//    dump_hex_relative(rgb, 48);
//    dump_hex_relative(yuv, 48);
    set_palette_yuv(yuv);
}

void U64Config :: set_palette_yuv(const uint8_t yuv[16][3])
{
    volatile uint8_t *yuv_registers = (volatile uint8_t *)(C64_PALETTE + 0x400);
    for(int i=0; i<16; i++) {
        *(yuv_registers++) = yuv[i][0];
        *(yuv_registers++) = yuv[i][1];
        *(yuv_registers++) = yuv[i][2];
        yuv_registers++;
    }
}

void U64Config :: rgb_to_yuv(const uint8_t rgb[3], uint8_t yuv[3], bool ntsc)
{
    // Y =  0.299R + 0.587G + 0.114B
    // U = -0.147R - 0.289G + 0.436B
    // V =  0.615R - 0.515G - 0.100B
    // Matrix in 0.14 format. Values for Y are scaled with 0xB6/0xFF for the correct white values
    //const int Y[3] = {  4899,  9617,  1868 };
    const int Y[3] = {  3496,  6864,  1333 };
    const int U[3] = { -2408, -4735,  7143 };
    const int V[3] = { 10076, -8438, -1638 };

    int y = Y[0] * rgb[0] + Y[1] * rgb[1] + Y[2] * rgb[2];
    int u = U[0] * rgb[0] + U[1] * rgb[1] + U[2] * rgb[2];
    int v = V[0] * rgb[0] + V[1] * rgb[1] + V[2] * rgb[2];

    y >>= 14;
    u >>= 14;
    v >>= 14;

    if (u > 127) {
        v *= 127;
        v /= u;
        u = 127;
    }
    if (u < -127) {
        v *= -127;
        v /= u;
        u = -127;
    }
    if (v > 127) {
        u *= 127;
        u /= v;
        v = 127;
    }
    if (v < -127) {
        u *= -127;
        u /= v;
        v = -127;
    }

    yuv[0] = (uint8_t)y;
    yuv[1] = (uint8_t)u;
    yuv[2] = (uint8_t)v;
}

bool U64Config :: load_palette_vpl(const char *path, const char *filename)
{
    File *file = NULL;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(path, filename, FA_READ, &file);

    uint8_t rgb[16][3];

    bool success = false;
    if(file) {
        success = FileTypePalette :: parseVplFile(file, rgb);
        if (success) {
            set_palette_rgb(rgb);
        }
        fm->fclose(file);
    }
    return success;
}

void U64Config :: set_palette_filename(const char *filename)
{
    cfg->set_string(CFG_PALETTE, filename);
    cfg->write();
    cfg->effectuate();
}

// Because the first configurator call to 'effectuate settings' takes place before
// the flash disk has been initialized, it has to be done again after.
// The flash disk initializes also with an init function, so make sure the ordering
// number here is higher than the ordering number in blockdev_flash.

InitFunction init_palette("U64 Palette", U64Config :: late_init_palette, NULL, NULL, 9);

void U64Config :: late_init_palette(void *obj, void *param)
{
    const char *palette = u64_configurator.cfg->get_string(CFG_PALETTE);
    if (strlen(palette)) {
        load_palette_vpl(DATA_DIRECTORY, palette);
    } else {
        set_palette_rgb(default_colors);
    }
}
