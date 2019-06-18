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
#include "menu.h"
#include "flash.h"
#include "userinterface.h"
#include "u64.h"
#include "u64_config.h"
#include "audio_select.h"
#include "fpll.h"
#include "i2c.h"
#include "ext_i2c.h"
#include "overlay.h"
#include "u2p.h"

// static pointer
U64Config u64_configurator;
// Semaphore set by interrupt
static SemaphoreHandle_t resetSemaphore;

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

#define CFG_SYSTEM_MODE       0x41
#define CFG_LED_SELECT_0      0x42
#define CFG_LED_SELECT_1      0x43
#define CFG_SPEAKER_VOL       0x44

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

uint8_t C64_STEREO_ADDRSEL_BAK;
uint8_t C64_SID1_EN_BAK;
uint8_t C64_SID2_EN_BAK;

const char *u64_sid_base[] = { "$D400-$D7FF", // 10 bits
		                       "$D400-$D5FF", // 9 bits
		                       "$D600-$D7FF", // 9 bits
		                       "$D400-$D4FF", // 8 bits
		                       "$D500-$D5FF", // 8 bits
		                       "$D600-$D6FF", // 8 bits
		                       "$D700-$D7FF", // 8 bits
		                       "$DE00-$DEFF", // 8 bits
		                       "$DF00-$DFFF", // 8 bits
		                   "$D400", "$D420", "$D440", "$D480", "$D500", "$D600", "$D700",
                           "$DE00", "$DE20", "$DE40", "$DE60",
                           "$DE80", "$DEA0", "$DEC0", "$DEE0",
                           "$DF00", "$DF20", "$DF40", "$DF60",
                           "$DF80", "$DFA0", "$DFC0", "$DFE0" };

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

uint8_t u64_sid_offsets[] = { 0x40, 0x40, 0x60, 0x40, 0x50, 0x60, 0x70, 0xE0, 0xF0,
						  0x40, 0x42, 0x44, 0x48, 0x50, 0x60, 0x70,
						  0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
						  0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE,
						  };

uint8_t u64_sid_mask[]    = { 0xC0, 0xE0, 0xE0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  };

static const char *stereo_addr[] = { "A5", "A8" };
static const char *en_dis4[] = { "Disabled", "Enabled" };
static const char *en_dis5[] = { "Disabled", "Enabled", "Transp. Border" };
static const char *digi_levels[] = { "Off", "Low", "Medium", "High" };
static const char *yes_no[] = { "No", "Yes" };
static const char *dvi_hdmi[] = { "DVI", "HDMI" };
static const char *video_sel[] = { "CVBS + SVideo", "RGB" };
static const char *color_sel[] = { "PAL", "NTSC" };
static const char *sid_types[] = { "None", "6581", "8580", "SidFX", "fpgaSID" };
static const char *sid_shunt[] = { "Off", "On" };
static const char *sid_caps[] = { "470 pF", "22 nF" };
static const char *filter_sel[] = { "8580 Lo", "8580 Hi", "6581", "6581 Alt", "U2 Low", "U2 Mid", "U2 High" };
static const char *filter_res[] = { "Low", "High" };
static const char *comb_wave[] = { "6581", "8580" };
static const char *ledselects[] = { "On", "Off", "Drive A Pwr", "DrvAPwr + DrvBPwr", "Drive A Act", "DrvAAct + DrvBAct",
                                    "DrvAPwr ^ DrvAAct", "USB Activity", "Any Activity", "!(DrvAAct)", "!(DrvAAct+DrvBAct)",
                                    "!(USB Act)", "!(Any Act)", "IRQ Line", "!(IRQ Line)" };
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
    { CFG_SCANLINES,    		CFG_TYPE_ENUM, "HDMI Scan lines",          	   "%s", en_dis4,      0,  1, 0 },
    { CFG_SYSTEM_MODE,          CFG_TYPE_ENUM, "System Mode",                  "%s", color_sel,    0,  1, 0 },
//    { CFG_COLOR_CLOCK_ADJ,      CFG_TYPE_VALUE, "Adjust Color Clock",      "%d ppm", NULL,      -100,100, 0 },
    { CFG_ANALOG_OUT_SELECT,    CFG_TYPE_ENUM, "Analog Video",                 "%s", video_sel,    0,  1, 0 },
    { CFG_CHROMA_DELAY,         CFG_TYPE_VALUE, "Chroma Delay",                "%d", NULL,        -3,  3, 0 },
    { CFG_HDMI_ENABLE,          CFG_TYPE_ENUM, "Digital Video Mode",           "%s", dvi_hdmi,     0,  1, 0 },
    { CFG_PARCABLE_ENABLE,      CFG_TYPE_ENUM, "SpeedDOS Parallel Cable",      "%s", en_dis4,      0,  1, 0 },
    { CFG_SID1_TYPE,			CFG_TYPE_ENUM, "SID in Socket 1",              "%s", sid_types,    0,  2, 0 },
    { CFG_SID2_TYPE,			CFG_TYPE_ENUM, "SID in Socket 2",              "%s", sid_types,    0,  2, 0 },
    { CFG_SID1_SHUNT,           CFG_TYPE_ENUM, "SID Socket 1 1K Ohm Resistor", "%s", sid_shunt,    0,  1, 0 },
    { CFG_SID2_SHUNT,           CFG_TYPE_ENUM, "SID Socket 2 1K Ohm Resistor", "%s", sid_shunt,    0,  1, 0 },
    { CFG_SID1_CAPS,            CFG_TYPE_ENUM, "SID Socket 1 Capacitors",      "%s", sid_caps,     0,  1, 0 },
    { CFG_SID2_CAPS,            CFG_TYPE_ENUM, "SID Socket 2 Capacitors",      "%s", sid_caps,     0,  1, 0 },
    { CFG_PLAYER_AUTOCONFIG,    CFG_TYPE_ENUM, "SID Player Autoconfig",        "%s", en_dis4,      0,  1, 1 },
    { CFG_ALLOW_EMUSID,         CFG_TYPE_ENUM, "Allow Autoconfig uses UltiSid","%s", yes_no,       0,  1, 1 },
    { CFG_SID1_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 1 Address",         "%s", u64_sid_base, 0, 31, 0 },
    { CFG_SID2_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 2 Address",         "%s", u64_sid_base, 0, 31, 0 },
    { CFG_PADDLE_EN,			CFG_TYPE_ENUM, "Paddle Override",              "%s", en_dis4,      0,  1, 1 },
    { CFG_STEREO_DIFF,			CFG_TYPE_ENUM, "Ext StereoSID addrline",       "%s", stereo_addr,  0,  1, 0 },
    { CFG_EMUSID1_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 1 Address",            "%s", u64_sid_base, 0, 31, 0 },
    { CFG_EMUSID2_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 2 Address",            "%s", u64_sid_base, 0, 31, 0 },
    { CFG_EMUSID1_FILTER,       CFG_TYPE_ENUM, "UltiSID 1 Filter Curve",       "%s", filter_sel,   0,  6, 0 },
    { CFG_EMUSID2_FILTER,       CFG_TYPE_ENUM, "UltiSID 2 Filter Curve",       "%s", filter_sel,   0,  6, 0 },
    { CFG_EMUSID1_RESONANCE,    CFG_TYPE_ENUM, "UltiSID 1 Filter Resonance",   "%s", filter_res,   0,  1, 0 },
    { CFG_EMUSID2_RESONANCE,    CFG_TYPE_ENUM, "UltiSID 2 Filter Resonance",   "%s", filter_res,   0,  1, 0 },
    { CFG_EMUSID1_WAVES,        CFG_TYPE_ENUM, "UltiSID 1 Combined Waveforms", "%s", comb_wave,    0,  1, 0 },
    { CFG_EMUSID2_WAVES,        CFG_TYPE_ENUM, "UltiSID 2 Combined Waveforms", "%s", comb_wave,    0,  1, 0 },
    { CFG_EMUSID1_DIGI,         CFG_TYPE_ENUM, "UltiSID 1 Digis Level",        "%s", digi_levels,  0,  3, 2 },
    { CFG_EMUSID2_DIGI,         CFG_TYPE_ENUM, "UltiSID 2 Digis Level",        "%s", digi_levels,  0,  3, 2 },
#if DEVELOPER
    { CFG_VIC_TEST,             CFG_TYPE_ENUM, "VIC Test Colors",              "%s", en_dis5,      0,  2, 0 },
#endif
    //    { CFG_COLOR_CODING,         CFG_TYPE_ENUM, "Color Coding (not Timing!)",   "%s", color_sel,    0,  1, 0 },
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
    { CFG_LED_SELECT_0,         CFG_TYPE_ENUM, "LED Select Top",               "%s", ledselects,   0, 14, 0 },
    { CFG_LED_SELECT_1,         CFG_TYPE_ENUM, "LED Select Bot",               "%s", ledselects,   0, 14, 4 },
    { CFG_SPEAKER_VOL,          CFG_TYPE_ENUM, "Speaker Volume (SpkDat)",      "%s", speaker_vol,  0, 10, 5 },

    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };


extern Overlay *overlay;

U64Config :: U64Config() : SubSystem(SUBSYSID_U64)
{
    systemMode = -1;
    U64_ETHSTREAM_ENA = 0;

    if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		struct t_cfg_definition *def = u64_cfg;
		uint32_t store = 0x55363443;
		register_store(store, "U64 Specific Settings", def);

		// enable "hot" updates for mixer
		for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
		    cfg->set_change_hook(b, U64Config :: setMixer);
		}
        for (uint8_t b = CFG_MIXER0_PAN; b <= CFG_MIXER9_PAN; b++) {
            cfg->set_change_hook(b, U64Config :: setMixer);
        }

        cfg->set_change_hook(CFG_EMUSID1_FILTER, U64Config :: setFilter);
        cfg->set_change_hook(CFG_EMUSID2_FILTER, U64Config :: setFilter);
        cfg->set_change_hook(CFG_SCAN_MODE_TEST, U64Config :: setScanMode);
		cfg->set_change_hook(CFG_COLOR_CLOCK_ADJ, U64Config :: setPllOffset);
		cfg->set_change_hook(CFG_EMUSID1_RESONANCE, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_EMUSID2_RESONANCE, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_EMUSID1_WAVES, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_EMUSID2_WAVES, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_EMUSID1_DIGI, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_EMUSID2_DIGI, U64Config :: setSidEmuParams);
        cfg->set_change_hook(CFG_LED_SELECT_0, U64Config :: setLedSelector);
        cfg->set_change_hook(CFG_LED_SELECT_1, U64Config :: setLedSelector);
		effectuate_settings();
	}
	fm = FileManager :: getFileManager();

    uint8_t rev = (U2PIO_BOARDREV >> 3);
    if (rev != 0x13) {
        cfg->disable(CFG_SID1_SHUNT);
        cfg->disable(CFG_SID2_SHUNT);
        cfg->disable(CFG_SID1_CAPS);
        cfg->disable(CFG_SID2_CAPS);
    }

	skipReset = false;
    xTaskCreate( U64Config :: reset_task, "U64 Reset Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 3, &resetTaskHandle );
    resetSemaphore = xSemaphoreCreateBinary();
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

    uint8_t sp_vol = cfg->get_value(CFG_SPEAKER_VOL);

    U2PIO_SPEAKER_EN = sp_vol ? (sp_vol << 1) | 0x01 : 0;

    {
        uint8_t typ = cfg->get_value(CFG_SID1_TYPE); // 0 = none, 1 = 6581, 2 = 8580
        uint8_t shu = cfg->get_value(CFG_SID1_SHUNT);
        uint8_t cap = 1-cfg->get_value(CFG_SID1_CAPS);
        uint8_t reg;
        switch (typ) {
            case 1: reg = 3; break;
            case 2: reg = 2; break;
            default: reg = 0; break;
        }
        // bit 0 = voltage
        // bit 1 = regulator enable
        // bit 2 = shunt
        // bit 3 = caps
        uint8_t value = reg | (shu << 2) | (cap << 3);
        //C64_PLD_SIDCTRL1 = value | 0xB0;
        C64_PLD_SIDCTRL1 = value | 0x50;
    }

    {
        uint8_t typ = cfg->get_value(CFG_SID2_TYPE); // 0 = none, 1 = 6581, 2 = 8580
        uint8_t shu = cfg->get_value(CFG_SID2_SHUNT);
        uint8_t cap = 1-cfg->get_value(CFG_SID2_CAPS);
        uint8_t reg;
        switch (typ) {
            case 1: reg = 3; break;
            case 2: reg = 2; break;
            default: reg = 0; break;
        }
        // bit 0 = voltage
        // bit 1 = regulator enable
        // bit 2 = shunt
        // bit 3 = caps
        uint8_t value = reg | (shu << 2) | (cap << 3);
        C64_PLD_SIDCTRL2 = value | 0xB0;
        //C64_PLD_SIDCTRL2 = value | 0x50;
    }

    C64_SCANLINES    =  cfg->get_value(CFG_SCANLINES);
    C64_PADDLE_EN    =  cfg->get_value(CFG_PADDLE_EN);
    C64_STEREO_ADDRSEL = C64_STEREO_ADDRSEL_BAK = cfg->get_value(CFG_STEREO_DIFF);
    C64_SID1_EN_BAK = cfg->get_value(CFG_SID1_TYPE);
    C64_SID2_EN_BAK = cfg->get_value(CFG_SID2_TYPE);
    C64_SID1_EN      =  cfg->get_value(CFG_SID1_TYPE) ? 1 : 0;
    C64_SID2_EN      =  cfg->get_value(CFG_SID2_TYPE) ? 1 : 0;
    C64_SID1_BASE    =  C64_SID1_BASE_BAK = u64_sid_offsets[cfg->get_value(CFG_SID1_ADDRESS)];
    C64_SID2_BASE    =  C64_SID2_BASE_BAK = u64_sid_offsets[cfg->get_value(CFG_SID2_ADDRESS)];
    C64_EMUSID1_BASE = C64_EMUSID1_BASE_BAK =  u64_sid_offsets[cfg->get_value(CFG_EMUSID1_ADDRESS)];
    C64_EMUSID2_BASE = C64_EMUSID2_BASE_BAK =  u64_sid_offsets[cfg->get_value(CFG_EMUSID2_ADDRESS)];
    C64_SID1_MASK	 =  C64_SID1_MASK_BAK = u64_sid_mask[cfg->get_value(CFG_SID1_ADDRESS)];
    C64_SID2_MASK	 =  C64_SID2_MASK_BAK = u64_sid_mask[cfg->get_value(CFG_SID2_ADDRESS)];
    C64_EMUSID1_MASK =  C64_EMUSID1_MASK_BAK = u64_sid_mask[cfg->get_value(CFG_EMUSID1_ADDRESS)];
    C64_EMUSID2_MASK =  C64_EMUSID2_MASK_BAK = u64_sid_mask[cfg->get_value(CFG_EMUSID2_ADDRESS)];
    U64_HDMI_ENABLE  =  cfg->get_value(CFG_HDMI_ENABLE);
    U64_PARCABLE_EN  =  cfg->get_value(CFG_PARCABLE_ENABLE);

    int chromaDelay  =  cfg->get_value(CFG_CHROMA_DELAY);
    if (chromaDelay < 0) {
        C64_LUMA_DELAY   = -chromaDelay;
        C64_CHROMA_DELAY = 0;
    } else {
        C64_LUMA_DELAY   = 0;
        C64_CHROMA_DELAY = chromaDelay;
    }

    uint8_t format = 0;

    if (cfg->get_value(CFG_ANALOG_OUT_SELECT)) {
        format |= VIDEO_FMT_RGB_OUTPUT;
    }

    bool doPll = false;
    if (cfg->get_value(CFG_SYSTEM_MODE) != systemMode) {
        systemMode = cfg->get_value(CFG_SYSTEM_MODE);
        doPll = true;
    }

    if (systemMode) {
        format |= VIDEO_FMT_CYCLES_65 | VIDEO_FMT_NTSC_ENCODING | VIDEO_FMT_NTSC_FREQ | VIDEO_FMT_60_HZ;
        C64_BURST_PHASE = 32;
    } else {
        format |= VIDEO_FMT_CYCLES_63;
        C64_BURST_PHASE = 24;
    }
    if (doPll) {
        SetVideoPll(systemMode);
        SetHdmiPll(systemMode);
        SetVideoMode(systemMode);
        ResetHdmiPll();
        SetResampleFilter(systemMode);
        overlay->initRegs();
    }
    C64_VIDEOFORMAT = format;

#if DEVELOPER
    C64_VIC_TEST = cfg->get_value(CFG_VIC_TEST);
#endif
    /*
    C64_LINE_PHASE   = 9;
    C64_PHASE_INCR   = 9;
    C64_BURST_PHASE  = 24;
*/
    setMixer(cfg->items[0]);
    setFilter(cfg->find_item(CFG_EMUSID1_FILTER));
    setFilter(cfg->find_item(CFG_EMUSID2_FILTER));
    setPllOffset(cfg->find_item(CFG_COLOR_CLOCK_ADJ));
    setScanMode(cfg->find_item(CFG_SCAN_MODE_TEST));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_RESONANCE));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_RESONANCE));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_WAVES));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_WAVES));
    setSidEmuParams(cfg->find_item(CFG_EMUSID1_DIGI));
    setSidEmuParams(cfg->find_item(CFG_EMUSID2_DIGI));
    setLedSelector(cfg->find_item(CFG_LED_SELECT_0)); // does both anyway

/*
    printf("Resulting address map: Slot1: %02X/%02X (%s) Slot2: %02X/%02X (%s)  Emu1: %02X/%02X  Emu2: %02X/%02X\n",
            C64_SID1_BASE_BAK, C64_SID1_MASK_BAK, en_dis4[C64_SID1_EN_BAK],
            C64_SID2_BASE_BAK, C64_SID2_MASK_BAK, en_dis4[C64_SID2_EN_BAK],
            C64_EMUSID1_BASE_BAK, C64_EMUSID1_MASK_BAK,
            C64_EMUSID2_BASE_BAK, C64_EMUSID2_MASK_BAK );
*/

}


void U64Config :: setFilter(ConfigItem *it)
{
    volatile uint8_t *base = (volatile uint8_t *)(C64_SID_BASE + 0x1000);
    if (it->definition->id == CFG_EMUSID2_FILTER) {
        base += 0x800;
    }
    uint16_t *coef = sid8580_filter_coefficients;
    int mul = 1;
    int div = 4;
    switch(it->value) {
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
}

void U64Config :: setMixer(ConfigItem *it)
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
        *(mixer++) = vol_left;
        *(mixer++) = vol_right;
    }
}

void U64Config :: setPllOffset(ConfigItem *it)
{
	if(it) {
		pllOffsetPpm(it->value); // Set correct mfrac
	}
}

void U64Config :: setScanMode(ConfigItem *it)
{
	if(it) {
//		SetScanMode(it->value);
	}
}

void U64Config :: setLedSelector(ConfigItem *it)
{
    if(it) {
        ConfigStore *cfg = it->store;
        uint8_t sel0 = (uint8_t)cfg->get_value(CFG_LED_SELECT_0);
        uint8_t sel1 = (uint8_t)cfg->get_value(CFG_LED_SELECT_1);
        U64_CASELED_SELECT = (sel1 << 4) | sel0;
    }
}

void U64Config :: setSidEmuParams(ConfigItem *it)
{
    switch(it->definition->id) {
    case CFG_EMUSID1_RESONANCE:
        C64_EMUSID1_RES = it->value;
        break;
    case CFG_EMUSID2_RESONANCE:
        C64_EMUSID2_RES = it->value;
        break;
    case CFG_EMUSID1_WAVES:
        C64_EMUSID1_WAVES = it->value;
        break;
    case CFG_EMUSID2_WAVES:
        C64_EMUSID2_WAVES = it->value;
        break;
    case CFG_EMUSID1_DIGI:
        C64_EMUSID1_DIGI = it->value;
        break;
    case CFG_EMUSID2_DIGI:
        C64_EMUSID2_DIGI = it->value;
        break;
    }
}

#define MENU_U64_SAVEEDID 1
#define MENU_U64_SAVEEEPROM 2
#define MENU_U64_WIFI_DISABLE 3
#define MENU_U64_WIFI_ENABLE 4
#define MENU_U64_WIFI_BOOT 5

int U64Config :: fetch_task_items(Path *p, IndexedList<Action*> &item_list)
{
	int count = 0;
	if(fm->is_path_writable(p)) {
    	item_list.append(new Action("Save EDID to file", SUBSYSID_U64, MENU_U64_SAVEEDID));
    	count ++;
#if DEVELOPER > 1
        item_list.append(new Action("Save I2C ROM to file", SUBSYSID_U64, MENU_U64_SAVEEEPROM));
        count ++;
#endif
    }
#if DEVELOPER
	item_list.append(new Action("Disable WiFi", SUBSYSID_U64, MENU_U64_WIFI_DISABLE));  count++;
	item_list.append(new Action("Enable WiFi",  SUBSYSID_U64, MENU_U64_WIFI_ENABLE));  count++;
    item_list.append(new Action("Enable WiFi Boot", SUBSYSID_U64, MENU_U64_WIFI_BOOT));  count++;
#endif
	return count;

}

int U64Config :: executeCommand(SubsysCommand *cmd)
{
	File *f = 0;
	FRESULT res;
	uint8_t edid[256];
	char name[32];
	FRESULT fres;
	uint32_t trans;
	name[0] = 0;

    switch(cmd->functionID) {
    case MENU_U64_SAVEEDID:
    	// Try to read EDID, just a hardware test
    	if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
    		U64_HDMI_REG = U64_HDMI_HPD_RESET;

    		if (U64_HDMI_REG & U64_HDMI_HPD_CURRENT) {
    			U64_HDMI_REG = U64_HDMI_DDC_ENABLE;
    			printf("Monitor detected, now reading EDID.\n");
    			if (i2c_read_block(0xA0, 0x00, edid, 256) == 0) {
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
    		}
    	}
    	break;
    case MENU_U64_SAVEEEPROM:
        // Try to read EDID, just a hardware test
        if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
            if (ext_i2c_read_block(0xA0, 0x00, edid, 256) == 0) {
                dump_hex_relative(edid, 256);
                if (cmd->user_interface->string_box("Reading I2C OK. Save to:", name, 31) > 0) {
                    set_extension(name, ".bin", 32);
                    fres = fm->fopen(cmd->path.c_str(), name, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
                    if (fres == FR_OK) {
                        f->write(edid, 256, &trans);
                        fm->fclose(f);
                    }
                }
            }
        }
        break;

    case MENU_U64_WIFI_DISABLE:
        U64_WIFI_CONTROL = 0;
        break;

    case MENU_U64_WIFI_ENABLE:
        U64_WIFI_CONTROL = 0;
        vTaskDelay(50);
        U64_WIFI_CONTROL = 5;
        break;

    case MENU_U64_WIFI_BOOT:
        U64_WIFI_CONTROL = 2;
        vTaskDelay(150);
        U64_WIFI_CONTROL = 7;
        break;

    default:
    	printf("U64 does not know this command\n");
    }
    return 0;
}


uint8_t U64Config :: GetSidType(int slot)
{
    uint8_t val;

    switch(slot) {
    case 0: // slot 1A
        val = cfg->get_value(CFG_SID1_TYPE);
        switch (val) {  // "None", "6581", "8580", "SidFX", "fpgaSID" };
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 1; // to be implemented
        case 4:
            return 3; // either type, we can TELL the fpgaSID to configure itself in the right mode
        default:
            return 0;
        }
        break;

    case 1: // slot 2A
        val = cfg->get_value(CFG_SID2_TYPE);
        switch (val) {  // "None", "6581", "8580", "SidFX", "fpgaSID" };
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 1; // to be implemented
        case 4:
            return 3; // either type, we can TELL the fpgaSID to configure itself in the right mode
        default:
            return 0;
        }
        break;

    case 2:
    case 3:
        if (cfg->get_value(CFG_ALLOW_EMUSID)) {
            return 1;
        } else {
            return 0;
        }

    case 4: // slot 1B
        val = cfg->get_value(CFG_SID1_TYPE);
        if (val == 4) { // fpgaSID
            return 3;
        }
        return 0; // no "other" SID available in slot 1.

    case 5: // slot 2B
        val = cfg->get_value(CFG_SID2_TYPE);
        if (val == 4) { // fpgaSID
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
        switch(C64_STEREO_ADDRSEL_BAK) {
        case 0: // A5:
            other = 0x02;
            break;
        case 1: // A8:
            other = 0x10;
            break;
        default:
            break;
        }
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
            C64_SID1_EN = C64_SID1_EN_BAK = 1;
            return true;
        case 1: // Socket 2 address
            C64_SID2_BASE = C64_SID2_BASE_BAK = base;
            C64_SID2_MASK = C64_SID2_MASK_BAK = 0xFE & ~other;
            C64_SID2_EN = C64_SID2_EN_BAK = 1;
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
    selectedVolumes[0] = volume_ctrl[cfg->get_value(CFG_MIXER2_VOL)];
    selectedVolumes[1] = volume_ctrl[cfg->get_value(CFG_MIXER3_VOL)];
    selectedVolumes[2] = volume_ctrl[cfg->get_value(CFG_MIXER0_VOL)];
    selectedVolumes[3] = volume_ctrl[cfg->get_value(CFG_MIXER1_VOL)];

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
        mixer[0 + channelMap[slots[i]]] = (pan_ctrl[pan] * volume) >> 8;
        mixer[1 + channelMap[slots[i]]] = (pan_ctrl[10-pan] * volume) >> 8;
    }
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

    C64_SID1_EN = C64_SID1_EN_BAK = 0;
    C64_SID2_EN = C64_SID2_EN_BAK = 0;

    memset(mappedOnSlot, 0, 8);
    mappedSids = 0;
    bool failed = false;
    for (int i=0; i < count; i++) {
        if (!MapSid(i, count, mappedSids, mappedOnSlot, &requested[i], false)) {
            failed = true;
        }
    }
    if (failed) {
        C64_SID1_EN = C64_SID1_EN_BAK = 0;
        C64_SID2_EN = C64_SID2_EN_BAK = 0;
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
        C64_SID1_EN = C64_SID1_EN_BAK = 0;
        C64_SID2_EN = C64_SID2_EN_BAK = 0;
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
            C64_SID1_BASE_BAK, C64_SID1_MASK_BAK, en_dis4[C64_SID1_EN_BAK],
            C64_SID2_BASE_BAK, C64_SID2_MASK_BAK, en_dis4[C64_SID2_EN_BAK],
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


const uint32_t c_filter2_coef_pal_alt[] = {
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

const uint32_t c_filter2_coef_ntsc[] = {
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


void U64Config :: SetResampleFilter(int mode)
{
    const uint32_t *pulFilter = (mode == 0) ? c_filter2_coef_pal_alt : c_filter2_coef_ntsc;
    int size = (mode == 0) ? 675 : 512;

    U64_RESAMPLE_RESET = 1;
    U64_RESAMPLE_FLUSH = 1;
    for(int i=0; i < size; i++) {
        U64_RESAMPLE_DATA = *(pulFilter ++);
    }
    U64_RESAMPLE_LABOR = (mode == 0) ? 45 : 169; // 675 / 15 and 510 / 3
    U64_RESAMPLE_FLUSH = 0;
}
