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

// static pointer
U64Config u64_configurator;

#define CFG_SCANLINES        0x01
#define CFG_SID1_ADDRESS     0x02
#define CFG_SID2_ADDRESS     0x03
#define CFG_EMUSID1_ADDRESS  0x04
#define CFG_EMUSID2_ADDRESS  0x05
#define CFG_AUDIO_LEFT_OUT	 0x06
#define CFG_AUDIO_RIGHT_OUT	 0x07

const char *u64_sid_base[] = { "$D400-$D7FF", "$D400", "$D420", "$D440", "$D480", "$D500", "$D600", "$D700",
                           "$DE00", "$DE20", "$DE40", "$DE60",
                           "$DE80", "$DEA0", "$DEC0", "$DEE0",
                           "$DF00", "$DF20", "$DF40", "$DF60",
                           "$DF80", "$DFA0", "$DFC0", "$DFE0" };

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

struct t_cfg_definition u64_cfg[] = {
    { CFG_SCANLINES,    		CFG_TYPE_ENUM, "HDMI Scanlines",          	   "%s", en_dis4,      0,  1, 0 },
    { CFG_SID1_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 1 Address",         "%s", u64_sid_base, 0, 23, 0 },
    { CFG_SID2_ADDRESS,   		CFG_TYPE_ENUM, "SID Socket 2 Address",         "%s", u64_sid_base, 0, 23, 0 },
    { CFG_EMUSID1_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 1 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_EMUSID2_ADDRESS,   	CFG_TYPE_ENUM, "UltiSID 2 Address",            "%s", u64_sid_base, 0, 23, 0 },
    { CFG_AUDIO_LEFT_OUT,       CFG_TYPE_ENUM, "Output Selector Left",         "%s", audio_sel,    0,  9, 0 },
    { CFG_AUDIO_RIGHT_OUT,      CFG_TYPE_ENUM, "Output Selector Right",        "%s", audio_sel,    0,  9, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

extern "C" {
    void set_sid_coefficients(volatile uint8_t *);
}

U64Config :: U64Config()
{
	if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		struct t_cfg_definition *def = u64_cfg;
		uint32_t store = 0x55363443;
		register_store(store, "U64 Specific Settings", def);

		set_sid_coefficients((volatile uint8_t *)C64_SID_BASE);

		effectuate_settings();
	}
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
}
