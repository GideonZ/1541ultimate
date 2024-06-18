#include "integer.h"
extern "C" {
    #include "itu.h"
	#include "dump_hex.h"
    #include "small_printf.h"
    #include "sid_coeff.h"
}
#include "audio_select.h"
#include <string.h>
#include "menu.h"
#include "flash.h"
#include "userinterface.h"
#include "sampler.h"
#include "u2p.h"

// static pointer
AudioConfig audio_configurator;

                          
#define CFG_AUDIO_SID_WAVE_LEFT  0x56
#define CFG_AUDIO_SID_WAVE_RIGHT 0x57
#define CFG_AUDIO_SID_ENABLE_L   0x58
#define CFG_AUDIO_SID_ENABLE_R   0x59
#define CFG_AUDIO_SELECT_LEFT    0x5A
#define CFG_AUDIO_SELECT_RIGHT   0x5B
#define CFG_AUDIO_SID_BASE_LEFT  0x5C
#define CFG_AUDIO_SID_BASE_RIGHT 0x5D
#define CFG_AUDIO_SID_EXT_LEFT   0x5E
#define CFG_AUDIO_SID_EXT_RIGHT  0x5F
#define CFG_AUDIO_SAMPLER_IO     0x60
#define CFG_AUDIO_SPEAKER_EN     0x61
#define CFG_AUDIO_SID_FILT_LEFT  0x62
#define CFG_AUDIO_SID_FILT_RIGHT 0x63

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

const char *aud_choices[]  = { "Drive A", "Drive B", "Cassette Read", "Cassette Write", "SID Left", "SID Right", "Sampler Left", "Sampler Right" };
const char *aud_choices2[] = { "Drive A", "Drive B", "Cassette Read", "Cassette Write", "Sampler Left", "Sampler Right" };
const char *aud_choices3[] = { "Drive A", "Drive B", "Cassette Read", "Cassette Write", "SID Left", "SID Right", "Sampler Left", "Sampler Right",
		                       "SID + Samp L", "SID + Samp R", "SID Mono", "Sampler Mono" };

const char *sid_base[] = { "Snoop $D400", "Snoop $D420", "Snoop $D480", "Snoop $D500", "Snoop $D520", "Snoop $D580",
                           "Snoop $D600", "Snoop $D620", "Snoop $D680", "Snoop $D700", "Snoop $D720", "Snoop $D780",
                           "IO $DE00", "IO $DE20", "IO $DE40", "IO $DE60",
                           "IO $DE80", "IO $DEA0", "IO $DEC0", "IO $DEE0",
                           "IO $DF00", "IO $DF20", "IO $DF40", "IO $DF60",
                           "IO $DF80", "IO $DFA0", "IO $DFC0", "IO $DFE0" };

uint8_t sid_offsets[] = { 0x40, 0x42, 0x48, 0x50, 0x52, 0x58, 0x60, 0x62, 0x68, 0x70, 0x72, 0x78,
                       0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee,
                       0xf0, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe };
                       
const char *sid_voices[] = { "Standard", "8 Voices" };
const char *sidchip_sel[] = { "6581", "8580" };
const char *speaker_vol[] = { "Disabled", "Vol 1", "Vol 2", "Vol 3", "Vol 4", "Vol 5", "Vol 6", "Vol 7", "Vol 8", "Vol 9", "Vol 10", "Vol 11", "Vol 12", "Vol 13", "Vol 14", "Vol 15" };

struct t_cfg_definition audio_cfg[] = {
    { CFG_AUDIO_SELECT_LEFT,    CFG_TYPE_ENUM, "Left Channel Output",          "%s", aud_choices, 0,  7, 0 },
    { CFG_AUDIO_SELECT_RIGHT,   CFG_TYPE_ENUM, "Right Channel Output",         "%s", aud_choices, 0,  7, 1 },
    { CFG_AUDIO_SID_ENABLE_L,   CFG_TYPE_ENUM, "SID Left",                     "%s", en_dis,      0,  1, 1 },
    { CFG_AUDIO_SID_BASE_LEFT,  CFG_TYPE_ENUM, "SID Left Base",                "%s", sid_base,    0, 24, 0 },
    { CFG_AUDIO_SID_EXT_LEFT,   CFG_TYPE_ENUM, "SID Left Mode",                "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_FILT_LEFT,  CFG_TYPE_ENUM, "SID Left Filter Curve",        "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_LEFT,  CFG_TYPE_ENUM, "SID Left Combined Waveforms",  "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_ENABLE_R,   CFG_TYPE_ENUM, "SID Right",                    "%s", en_dis,      0,  1, 1 },
    { CFG_AUDIO_SID_BASE_RIGHT, CFG_TYPE_ENUM, "SID Right Base",               "%s", sid_base,    0, 24, 8 },
    { CFG_AUDIO_SID_EXT_RIGHT,  CFG_TYPE_ENUM, "SID Right Mode",               "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_FILT_RIGHT, CFG_TYPE_ENUM, "SID Right Filter Curve",       "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_RIGHT, CFG_TYPE_ENUM, "SID Right Combined Waveforms", "%s", sidchip_sel, 0,  1, 0 },
    //    { CFG_AUDIO_SAMPLER_IO,     CFG_TYPE_ENUM, "Map Sampler in $DF20-DFFF",    "%s", en_dis3,     0,  1, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,        0,  0, 0 } };

struct t_cfg_definition audio_cfg_no_sid[] = {
    { CFG_AUDIO_SELECT_LEFT,    CFG_TYPE_ENUM, "Left Channel Output",          "%s", aud_choices2, 0,  3, 0 },
    { CFG_AUDIO_SELECT_RIGHT,   CFG_TYPE_ENUM, "Right Channel Output",         "%s", aud_choices2, 0,  3, 1 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

static const char *volumes[] = { "OFF", "+6 dB", "+5 dB", "+4 dB", "+3 dB", "+2 dB", "+1 dB", " 0 dB", "-1 dB",
                                 "-2 dB", "-3 dB", "-4 dB", "-5 dB", "-6 dB", "-7 dB", "-8 dB", "-9 dB",
                                 "-10 dB","-11 dB","-12 dB","-13 dB","-14 dB","-15 dB","-16 dB","-17 dB",
                                 "-18 dB","-24 dB","-27 dB","-30 dB","-36 dB","-42 dB" }; // 31 settings

static const char *pannings[] = { "Left 5", "Left 4", "Left 3", "Left 2", "Left 1", "Center",
                                  "Right 1", "Right 2", "Right 3", "Right 4", "Right 5" }; // 11 settings

static const uint8_t volume_ctrl[] = { 0x00, 0xff, 0xe4, 0xcb, 0xb5, 0xa1, 0x90, 0x80, 0x72,
                                       0x66, 0x5b, 0x51, 0x48, 0x40, 0x39, 0x33, 0x2d,
                                       0x28, 0x24, 0x20, 0x1d, 0x1a, 0x17, 0x14, 0x12,
                                       0x10, 0x08, 0x06, 0x04, 0x02, 0x01 };

static const uint16_t pan_ctrl[] = { 0, 40, 79, 116, 150, 181, 207, 228, 243, 253, 256 };

struct t_cfg_definition audio_cfg_plus[] = {
    { CFG_AUDIO_SPEAKER_EN,     CFG_TYPE_ENUM, "Built-in Speaker (Drive-A)",   "%s", speaker_vol, 0,  15, 15 },
    { CFG_AUDIO_SID_ENABLE_L,   CFG_TYPE_ENUM, "SID Left",                     "%s", en_dis,      0,  1, 1 },
    { CFG_AUDIO_SID_BASE_LEFT,  CFG_TYPE_ENUM, "SID Left Base",                "%s", sid_base,    0, 24, 0 },
    { CFG_AUDIO_SID_EXT_LEFT,   CFG_TYPE_ENUM, "SID Left Mode",                "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_FILT_LEFT,  CFG_TYPE_ENUM, "SID Left Filter Curve",        "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_LEFT,  CFG_TYPE_ENUM, "SID Left Combined Waveforms",  "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_ENABLE_R,   CFG_TYPE_ENUM, "SID Right",                    "%s", en_dis,      0,  1, 1 },
    { CFG_AUDIO_SID_BASE_RIGHT, CFG_TYPE_ENUM, "SID Right Base",               "%s", sid_base,    0, 24, 8 },
    { CFG_AUDIO_SID_EXT_RIGHT,  CFG_TYPE_ENUM, "SID Right Mode",               "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_FILT_RIGHT, CFG_TYPE_ENUM, "SID Right Filter Curve",       "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_RIGHT, CFG_TYPE_ENUM, "SID Right Combined Waveforms", "%s", sidchip_sel, 0,  1, 0 },
//    { CFG_AUDIO_SAMPLER_IO,     CFG_TYPE_ENUM, "Map Sampler in $DF20-DFFF",    "%s", en_dis3,     0,  1, 0 },
    { CFG_MIXER0_VOL,           CFG_TYPE_ENUM, "Vol EmuSid1",                  "%s", volumes,      0, 30, 7 },
    { CFG_MIXER1_VOL,           CFG_TYPE_ENUM, "Vol EmuSid2",                  "%s", volumes,      0, 30, 7 },
    { CFG_MIXER2_VOL,           CFG_TYPE_ENUM, "Vol ExtIn Left",               "%s", volumes,      0, 30, 7 },
    { CFG_MIXER3_VOL,           CFG_TYPE_ENUM, "Vol ExtIn Right",              "%s", volumes,      0, 30, 7 },
    { CFG_MIXER4_VOL,           CFG_TYPE_ENUM, "Vol Sampler L",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER5_VOL,           CFG_TYPE_ENUM, "Vol Sampler R",                "%s", volumes,      0, 30, 7 },
    { CFG_MIXER6_VOL,           CFG_TYPE_ENUM, "Vol Drive 1",                  "%s", volumes,      0, 30, 12 },
    { CFG_MIXER7_VOL,           CFG_TYPE_ENUM, "Vol Drive 2",                  "%s", volumes,      0, 30, 12 },
    { CFG_MIXER8_VOL,           CFG_TYPE_ENUM, "Vol Tape Read",                "%s", volumes,      0, 30, 0 },
    { CFG_MIXER9_VOL,           CFG_TYPE_ENUM, "Vol Tape Write",               "%s", volumes,      0, 30, 0 },
    { CFG_MIXER0_PAN,           CFG_TYPE_ENUM, "Pan EmuSid1",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER1_PAN,           CFG_TYPE_ENUM, "Pan EmuSid2",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER2_PAN,           CFG_TYPE_ENUM, "Pan ExtIn Left",               "%s", pannings,     0, 10, 2 },
    { CFG_MIXER3_PAN,           CFG_TYPE_ENUM, "Pan ExtIn Right",              "%s", pannings,     0, 10, 8 },
    { CFG_MIXER4_PAN,           CFG_TYPE_ENUM, "Pan Sampler L",                "%s", pannings,     0, 10, 2 },
    { CFG_MIXER5_PAN,           CFG_TYPE_ENUM, "Pan Sampler R",                "%s", pannings,     0, 10, 8 },
    { CFG_MIXER6_PAN,           CFG_TYPE_ENUM, "Pan Drive 1",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER7_PAN,           CFG_TYPE_ENUM, "Pan Drive 2",                  "%s", pannings,     0, 10, 5 },
    { CFG_MIXER8_PAN,           CFG_TYPE_ENUM, "Pan Tape Read",                "%s", pannings,     0, 10, 5 },
    { CFG_MIXER9_PAN,           CFG_TYPE_ENUM, "Pan Tape Write",               "%s", pannings,     0, 10, 5 },

    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,        0,  0, 0 } };


int normal_map[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
int skip_sid[8]   = { 0, 1, 2, 3, 6, 7, 6, 7 };

AudioConfig :: AudioConfig()
{
    struct t_cfg_definition *def = audio_cfg;
    uint32_t store = 0x41554449;    
    uint32_t capabilities = getFpgaCapabilities();

    if(capabilities & CAPAB_ULTIMATE2PLUS) {
    	map = normal_map;
        def = audio_cfg_plus;
    } else if(capabilities & CAPAB_STEREO_SID) {
    	map = normal_map;
        def = audio_cfg;
        if(capabilities & CAPAB_SAMPLER) {
            def[0].max = 7;
            def[1].max = 7;
        } else {
            def[0].max = 5;
            def[1].max = 5;
        }
    } else {// no sid
        map = skip_sid;
        def = audio_cfg_no_sid;
        store = 0x4155444a;
        if(capabilities & CAPAB_SAMPLER) {
            def[0].max = 5;
            def[1].max = 5;
        } else {
            def[0].max = 3;
            def[1].max = 3;
        }
    }        
    register_store(store, "Audio Output Settings", def);

    if(capabilities & CAPAB_ULTIMATE2PLUS) {
        map = normal_map;
        def = audio_cfg_plus;
        if(!(capabilities & CAPAB_STEREO_SID)) {
            cfg->disable(CFG_AUDIO_SID_ENABLE_L);
            cfg->disable(CFG_AUDIO_SID_BASE_LEFT);
            cfg->disable(CFG_AUDIO_SID_EXT_LEFT);
            cfg->disable(CFG_AUDIO_SID_FILT_LEFT);
            cfg->disable(CFG_AUDIO_SID_WAVE_LEFT);
            cfg->disable(CFG_AUDIO_SID_ENABLE_R);
            cfg->disable(CFG_AUDIO_SID_BASE_RIGHT);
            cfg->disable(CFG_AUDIO_SID_EXT_RIGHT);
            cfg->disable(CFG_AUDIO_SID_FILT_RIGHT);
            cfg->disable(CFG_AUDIO_SID_WAVE_RIGHT);
            cfg->disable(CFG_MIXER0_VOL);
            cfg->disable(CFG_MIXER1_VOL);
            cfg->disable(CFG_MIXER0_PAN);
            cfg->disable(CFG_MIXER1_PAN);
        }
        if(!(capabilities & CAPAB_SAMPLER)) {
            cfg->disable(CFG_MIXER4_VOL);
            cfg->disable(CFG_MIXER5_VOL);
            cfg->disable(CFG_MIXER4_PAN);
            cfg->disable(CFG_MIXER5_PAN);
        }
    }

    effectuate_settings();
}
    

void AudioConfig :: effectuate_settings()
{
    if(!cfg)
        return;

    ioWrite8(AUDIO_SELECT_LEFT,  map[cfg->get_value(CFG_AUDIO_SELECT_LEFT)]);
    ioWrite8(AUDIO_SELECT_RIGHT, map[cfg->get_value(CFG_AUDIO_SELECT_RIGHT)]);
    
    if(getFpgaCapabilities() & CAPAB_STEREO_SID) {
        printf("Number of SID voices implemented in FPGA: %d\n", ioRead8(SID_VOICES));
        ioWrite8(SID_BASE_LEFT,    sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_LEFT)]);
        ioWrite8(SID_SNOOP_LEFT,   (sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_LEFT)] & 0x80)?0:1);
        ioWrite8(SID_ENABLE_LEFT,  cfg->get_value(CFG_AUDIO_SID_ENABLE_L));
        ioWrite8(SID_EXTEND_LEFT,  cfg->get_value(CFG_AUDIO_SID_EXT_LEFT));
        ioWrite8(SID_COMBSEL_LEFT, cfg->get_value(CFG_AUDIO_SID_WAVE_LEFT));
    
        ioWrite8(SID_BASE_RIGHT,    sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_RIGHT)]);
        ioWrite8(SID_SNOOP_RIGHT,   (sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_RIGHT)] & 0x80)?0:1);
        ioWrite8(SID_ENABLE_RIGHT,  cfg->get_value(CFG_AUDIO_SID_ENABLE_R));
        ioWrite8(SID_EXTEND_RIGHT,  cfg->get_value(CFG_AUDIO_SID_EXT_RIGHT));
        ioWrite8(SID_COMBSEL_RIGHT, cfg->get_value(CFG_AUDIO_SID_WAVE_RIGHT));

        if (cfg->get_value(CFG_AUDIO_SID_FILT_LEFT)) {
            set_sid_coefficients((volatile uint8_t *)(SID_BASE + 0x0800), sid8580_filter_coefficients, 1, 4);
        } else {
            set_sid_coefficients((volatile uint8_t *)(SID_BASE + 0x0800), sid_curve_original, 1, 1);
        }

        if (cfg->get_value(CFG_AUDIO_SID_FILT_RIGHT)) {
            set_sid_coefficients((volatile uint8_t *)(SID_BASE + 0x1000), sid8580_filter_coefficients, 1, 4);
        } else {
            set_sid_coefficients((volatile uint8_t *)(SID_BASE + 0x1000), sid_curve_original, 1, 1);
        }
    }

    if(getFpgaCapabilities() & CAPAB_ULTIMATE2PLUS) {
        unsigned char tmp = cfg->get_value(CFG_AUDIO_SPEAKER_EN);
    	U2PIO_SPEAKER_EN = (tmp << 1) | (tmp != 0);

        // Now, configure the mixer
        volatile uint8_t *mixer = (volatile uint8_t *)U2P_AUDIO_MIXER;

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
}

