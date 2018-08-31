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

// static pointer
U64Config u64_configurator;

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

uint8_t u64_sid_offsets[] = { 0x40, 0x40, 0x60, 0x40, 0x50, 0x60, 0x70,
						  0x40, 0x42, 0x44, 0x48, 0x50, 0x60, 0x70,
						  0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
						  0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE,
						  };

uint8_t u64_sid_mask[]    = { 0xC0, 0xE0, 0xE0, 0xF0, 0xF0, 0xF0, 0xF0,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  };

static const char *stereo_addr[] = { "A5", "A8" };
static const char *en_dis4[] = { "Disabled", "Enabled" };
static const char *yes_no[] = { "No", "Yes" };
static const char *dvi_hdmi[] = { "DVI", "HDMI" };
static const char *video_sel[] = { "CVBS + SVideo", "RGB" };
static const char *color_sel[] = { "PAL", "NTSC" };
static const char *sid_types[] = { "None", "6581", "8580", "SidFX", "fpgaSID" };

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
    { CFG_HDMI_ENABLE,          CFG_TYPE_ENUM, "Digital Video Mode",           "%s", dvi_hdmi,     0,  1, 0 },
    { CFG_PARCABLE_ENABLE,      CFG_TYPE_ENUM, "SpeedDOS Parallel Cable",      "%s", en_dis4,      0,  1, 0 },
    { CFG_SID1_TYPE,			CFG_TYPE_ENUM, "SID in Socket 1",              "%s", sid_types,    0,  4, 0 },
    { CFG_SID2_TYPE,			CFG_TYPE_ENUM, "SID in Socket 2",              "%s", sid_types,    0,  4, 0 },
    { CFG_PLAYER_AUTOCONFIG,    CFG_TYPE_ENUM, "SID Player Autoconfig",        "%s", en_dis4,      0,  1, 1 },
    { CFG_ALLOW_EMUSID,         CFG_TYPE_ENUM, "Allow Autoconfig uses EmuSid", "%s", yes_no,       0,  1, 1 },
    { CFG_SID1_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 1 Address",         "%s", u64_sid_base, 0, 29, 0 },
    { CFG_SID2_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 2 Address",         "%s", u64_sid_base, 0, 29, 0 },
    { CFG_PADDLE_EN,			CFG_TYPE_ENUM, "Paddle Override",              "%s", en_dis4,      0,  1, 1 },
    { CFG_STEREO_DIFF,			CFG_TYPE_ENUM, "Ext StereoSID addrline",       "%s", stereo_addr,  0,  1, 0 },
    { CFG_EMUSID1_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 1 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_EMUSID2_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 2 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_COLOR_CLOCK_ADJ, 		CFG_TYPE_VALUE, "Adjust Color Clock",      "%d ppm", NULL,      -100,100, 0 },
    { CFG_ANALOG_OUT_SELECT,    CFG_TYPE_ENUM, "Analog Video",                 "%s", video_sel,    0,  1, 0 },
    { CFG_CHROMA_DELAY,         CFG_TYPE_VALUE, "Chroma Delay",                "%d", NULL,        -3,  3, 0 },
#if DEVELOPER
    { CFG_VIC_TEST,             CFG_TYPE_ENUM, "VIC Test Colors",              "%s", en_dis4,      0,  1, 0 },
#endif
    //    { CFG_COLOR_CODING,         CFG_TYPE_ENUM, "Color Coding (not Timing!)",   "%s", color_sel,    0,  1, 0 },
    { CFG_MIXER0_VOL,           CFG_TYPE_ENUM, "Vol EmuSid1",                  "%s", volumes,      0, 30, 7 },
    { CFG_MIXER1_VOL,           CFG_TYPE_ENUM, "Vol EmuSid2",                  "%s", volumes,      0, 30, 7 },
    { CFG_MIXER2_VOL,           CFG_TYPE_ENUM, "Vol Socket 1",                 "%s", volumes,      0, 30, 7 },
    { CFG_MIXER3_VOL,           CFG_TYPE_ENUM, "Vol Socket 2",                 "%s", volumes,      0, 30, 7 },
    { CFG_MIXER4_VOL,           CFG_TYPE_ENUM, "Vol Sampler L",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER5_VOL,           CFG_TYPE_ENUM, "Vol Sampler R",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER6_VOL,           CFG_TYPE_ENUM, "Vol Drive 1",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER7_VOL,           CFG_TYPE_ENUM, "Vol Drive 2",                  "%s", volumes,      0, 30, 11 },
    { CFG_MIXER8_VOL,           CFG_TYPE_ENUM, "Vol Tape Read",                "%s", volumes,      0, 30, 29 },
    { CFG_MIXER9_VOL,           CFG_TYPE_ENUM, "Vol Tape Write",               "%s", volumes,      0, 30, 29 },
    { CFG_MIXER0_PAN,           CFG_TYPE_ENUM, "Pan EmuSid1",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER1_PAN,           CFG_TYPE_ENUM, "Pan EmuSid2",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER2_PAN,           CFG_TYPE_ENUM, "Pan Socket 1",                 "%s", pannings,     0, 10, 2 },
    { CFG_MIXER3_PAN,           CFG_TYPE_ENUM, "Pan Socket 2",                 "%s", pannings,     0, 10, 8 },
    { CFG_MIXER4_PAN,           CFG_TYPE_ENUM, "Pan Sampler L",                "%s", pannings,     0, 10, 2 },
    { CFG_MIXER5_PAN,           CFG_TYPE_ENUM, "Pan Sampler R",                "%s", pannings,     0, 10, 8 },
    { CFG_MIXER6_PAN,           CFG_TYPE_ENUM, "Pan Drive 1",                  "%s", pannings,     0, 10, 3 },
    { CFG_MIXER7_PAN,           CFG_TYPE_ENUM, "Pan Drive 2",                  "%s", pannings,     0, 10, 7 },
    { CFG_MIXER8_PAN,           CFG_TYPE_ENUM, "Pan Tape Read",                "%s", pannings,     0, 10, 5 },
    { CFG_MIXER9_PAN,           CFG_TYPE_ENUM, "Pan Tape Write",               "%s", pannings,     0, 10, 5 },

    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

extern "C" {
    void set_sid_coefficients(volatile uint8_t *);
}

U64Config :: U64Config() : SubSystem(SUBSYSID_U64)
{
	if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		struct t_cfg_definition *def = u64_cfg;
		uint32_t store = 0x55363443;
		register_store(store, "U64 Specific Settings", def);

		set_sid_coefficients((volatile uint8_t *)C64_SID_BASE);

		// enable "hot" updates for mixer
		for (uint8_t b = CFG_MIXER0_VOL; b <= CFG_MIXER9_VOL; b++) {
		    cfg->set_change_hook(b, U64Config :: setMixer);
		}
        for (uint8_t b = CFG_MIXER0_PAN; b <= CFG_MIXER9_PAN; b++) {
            cfg->set_change_hook(b, U64Config :: setMixer);
        }

		cfg->set_change_hook(CFG_SCAN_MODE_TEST, U64Config :: setScanMode);
		cfg->set_change_hook(CFG_COLOR_CLOCK_ADJ, U64Config :: setPllOffset);

		effectuate_settings();
	}
	fm = FileManager :: getFileManager();
}

void U64Config :: effectuate_settings()
{
    if(!cfg)
        return;

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

    if (cfg->get_value(CFG_ANALOG_OUT_SELECT)) {
        C64_VIDEOFORMAT = 0x04;
    }
/*
    else if (cfg->get_value(CFG_COLOR_CODING)) {
        C64_VIDEOFORMAT = 0x01;
        C64_BURST_PHASE = 32;
    }
*/
    else { // PAL
        C64_VIDEOFORMAT = 0x00;
        C64_BURST_PHASE = 24;
    }

#if DEVELOPER
    C64_VIC_TEST = cfg->get_value(CFG_VIC_TEST);
#endif
    /*
    C64_LINE_PHASE   = 9;
    C64_PHASE_INCR   = 9;
    C64_BURST_PHASE  = 24;
*/
    setMixer(cfg->items[0]);

    setPllOffset(cfg->find_item(CFG_COLOR_CLOCK_ADJ));
    setScanMode(cfg->find_item(CFG_SCAN_MODE_TEST));
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

bool U64Config :: SetSidAddress(int slot, uint8_t actualType, uint8_t base)
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

bool U64Config :: MapSid(int index, uint16_t& mappedSids, uint8_t *mappedOnSlot, t_sid_definition *requested, bool any)
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
            if (SetSidAddress(i, actualType, requested->baseAddress)) {
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
        if (!MapSid(i, mappedSids, mappedOnSlot, &requested[i], false)) {
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
            if (!MapSid(i, mappedSids, mappedOnSlot, &requested[i], false)) {
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
            if (!MapSid(i, mappedSids, mappedOnSlot, &requested[i], true)) {
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
