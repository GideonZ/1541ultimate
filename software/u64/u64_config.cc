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

// static pointer
U64Config u64_configurator;

#define CFG_SCANLINES        0x01
#define CFG_SID1_ADDRESS     0x02
#define CFG_SID2_ADDRESS     0x03
#define CFG_EMUSID1_ADDRESS  0x04
#define CFG_EMUSID2_ADDRESS  0x05
#define CFG_AUDIO_LEFT_OUT	 0x06
#define CFG_AUDIO_RIGHT_OUT	 0x07
#define CFG_COLOR_CLOCK_ADJ  0x08

#define CFG_SCAN_MODE_TEST   0xA8

const char *u64_sid_base[] = { "$D400-$D7FF", "$D400", "$D420", "$D440", "$D480", "$D500", "$D600", "$D700",
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

uint8_t u64_sid_offsets[] = { 0x40, 0x40, 0x42, 0x44, 0x48, 0x50, 0x60, 0x70,
						  0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
						  0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA, 0xFC, 0xFE,
						  };

uint8_t u64_sid_mask[]    = { 0xC0, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
						  };

static const char *en_dis4[] = { "Disabled", "Enabled" };

static const char *audio_sel[] = { "Emulated SID 1", "Emulated SID 2", "SID Socket 1", "SID Socket 2",
								   "Sampler Left", "Sampler Right", "Drive A", "Drive B", "Tape Read", "Tape Write" };
// Ultimate:
// "Drive A", "Drive B", "Cassette Read", "Cassette Write", "SID Left", "SID Right", "Sampler Left", "Sampler Right"
static const uint8_t ult_select_map[] = { 4, 4, 4, 4, // sid from ultimate, not implemented, so silence
										  6, 7, 0, 1, 2, 3 };
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
    { CFG_SCANLINES,    		CFG_TYPE_ENUM, "HDMI Scanlines",          	   "%s", en_dis4,      0,  1, 0 },
    { CFG_SID1_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 1 Address",         "%s", u64_sid_base, 0, 23, 0 },
    { CFG_SID2_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 2 Address",         "%s", u64_sid_base, 0, 23, 0 },
    { CFG_EMUSID1_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 1 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_EMUSID2_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 2 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_AUDIO_LEFT_OUT,       CFG_TYPE_ENUM, "Output Selector Left",         "%s", audio_sel,    0,  9, 0 },
    { CFG_AUDIO_RIGHT_OUT,      CFG_TYPE_ENUM, "Output Selector Right",        "%s", audio_sel,    0,  9, 0 },
    { CFG_COLOR_CLOCK_ADJ, 		CFG_TYPE_VALUE, "Adjust Color Clock",      "%d ppm", NULL,      -100,100, 0 },
    { CFG_SCAN_MODE_TEST,       CFG_TYPE_ENUM, "HDMI Scan Mode Test",          "%s", scan_modes,   0, 14, 0 },
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
    C64_SID1_BASE    =  u64_sid_offsets[cfg->get_value(CFG_SID1_ADDRESS)];
    C64_SID2_BASE    =  u64_sid_offsets[cfg->get_value(CFG_SID2_ADDRESS)];
    C64_EMUSID1_BASE =  u64_sid_offsets[cfg->get_value(CFG_EMUSID1_ADDRESS)];
    C64_EMUSID2_BASE =  u64_sid_offsets[cfg->get_value(CFG_EMUSID2_ADDRESS)];
    C64_SID1_MASK	 =  u64_sid_mask[cfg->get_value(CFG_SID1_ADDRESS)];
    C64_SID2_MASK	 =  u64_sid_mask[cfg->get_value(CFG_SID2_ADDRESS)];
    C64_EMUSID1_MASK =  u64_sid_mask[cfg->get_value(CFG_EMUSID1_ADDRESS)];
    C64_EMUSID2_MASK =  u64_sid_mask[cfg->get_value(CFG_EMUSID2_ADDRESS)];

    uint8_t sel_left  = cfg->get_value(CFG_AUDIO_RIGHT_OUT);
    uint8_t sel_right = cfg->get_value(CFG_AUDIO_LEFT_OUT);
    // Ultimate:
    // "Drive A", "Drive B", "Cassette Read", "Cassette Write", "SID Left", "SID Right", "Sampler Left", "Sampler Right"
    if (sel_left > 3) {
    	ioWrite8(AUDIO_SELECT_LEFT, ult_select_map[sel_left]);
    	sel_left = 4;  // left channel from ultimate
    }
    if (sel_right > 3) {
    	ioWrite8(AUDIO_SELECT_RIGHT, ult_select_map[sel_right]);
    	sel_right = 5; // right channel from ultimate
	}
    U64_AUDIO_SEL_REG = (sel_left << 4) | sel_right;

    setPllOffset(cfg->find_item(CFG_COLOR_CLOCK_ADJ));
    setScanMode(cfg->find_item(CFG_SCAN_MODE_TEST));
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
		SetScanMode(it->value);
	}
}

#define MENU_U64_SAVEEDID 1

int U64Config :: fetch_task_items(Path *p, IndexedList<Action*> &item_list)
{
	int count = 0;
	if(fm->is_path_writable(p)) {
    	item_list.append(new Action("Save EDID to file", SUBSYSID_U64, MENU_U64_SAVEEDID));
    	count ++;
    }
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
    			}
    			U64_HDMI_REG = U64_HDMI_DDC_DISABLE;
    		}
    	}
    	break;

    default:
    	printf("U64 does not know this command\n");
    }
    return 0;
}
