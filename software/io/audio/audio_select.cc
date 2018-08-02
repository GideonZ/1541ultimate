#include "integer.h"
extern "C" {
    #include "itu.h"
	#include "dump_hex.h"
    #include "small_printf.h"
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
const char *en_dis3[] = { "Disabled", "Enabled" };
const char *sidchip_sel[] = { "6581", "8580" };
const char *speaker_vol[] = { "Disabled", "Vol 1", "Vol 2", "Vol 3", "Vol 4", "Vol 5", "Vol 6", "Vol 7", "Vol 8", "Vol 9", "Vol 10", "Vol 11", "Vol 12", "Vol 13", "Vol 14", "Vol 15" };

struct t_cfg_definition audio_cfg[] = {
    { CFG_AUDIO_SELECT_LEFT,    CFG_TYPE_ENUM, "Left Channel Output",          "%s", aud_choices, 0,  7, 0 },
    { CFG_AUDIO_SELECT_RIGHT,   CFG_TYPE_ENUM, "Right Channel Output",         "%s", aud_choices, 0,  7, 1 },
    { CFG_AUDIO_SID_ENABLE_L,   CFG_TYPE_ENUM, "SID Left",                     "%s", en_dis3,     0,  1, 1 },
    { CFG_AUDIO_SID_BASE_LEFT,  CFG_TYPE_ENUM, "SID Left Base",                "%s", sid_base,    0, 24, 0 },
    { CFG_AUDIO_SID_EXT_LEFT,   CFG_TYPE_ENUM, "SID Left Mode",                "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_LEFT,  CFG_TYPE_ENUM, "SID Left Combined Waveforms",  "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_ENABLE_R,   CFG_TYPE_ENUM, "SID Right",                    "%s", en_dis3,     0,  1, 1 },
    { CFG_AUDIO_SID_BASE_RIGHT, CFG_TYPE_ENUM, "SID Right Base",               "%s", sid_base,    0, 24, 8 },
    { CFG_AUDIO_SID_EXT_RIGHT,  CFG_TYPE_ENUM, "SID Right Mode",               "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_RIGHT, CFG_TYPE_ENUM, "SID Right Combined Waveforms", "%s", sidchip_sel, 0,  1, 0 },
//    { CFG_AUDIO_SAMPLER_IO,     CFG_TYPE_ENUM, "Map Sampler in $DF20-DFFF",    "%s", en_dis3,     0,  1, 0 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,        0,  0, 0 } };

struct t_cfg_definition audio_cfg_no_sid[] = {
    { CFG_AUDIO_SELECT_LEFT,    CFG_TYPE_ENUM, "Left Channel Output",          "%s", aud_choices2, 0,  3, 0 },
    { CFG_AUDIO_SELECT_RIGHT,   CFG_TYPE_ENUM, "Right Channel Output",         "%s", aud_choices2, 0,  3, 1 },
    { CFG_TYPE_END,             CFG_TYPE_END,  "",                             "",   NULL,         0,  0, 0 } };

struct t_cfg_definition audio_cfg_plus[] = {
    { CFG_AUDIO_SPEAKER_EN,     CFG_TYPE_ENUM, "Built-in Speaker (Drive-A)",   "%s", speaker_vol,     0,  15, 15 },
    { CFG_AUDIO_SELECT_LEFT,    CFG_TYPE_ENUM, "Left Channel Output",          "%s", aud_choices3,0,  7, 4 },
    { CFG_AUDIO_SELECT_RIGHT,   CFG_TYPE_ENUM, "Right Channel Output",         "%s", aud_choices3,0,  7, 5 },
    { CFG_AUDIO_SID_ENABLE_L,   CFG_TYPE_ENUM, "SID Left",                     "%s", en_dis3,     0,  1, 1 },
    { CFG_AUDIO_SID_BASE_LEFT,  CFG_TYPE_ENUM, "SID Left Base",                "%s", sid_base,    0, 24, 0 },
    { CFG_AUDIO_SID_EXT_LEFT,   CFG_TYPE_ENUM, "SID Left Mode",                "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_LEFT,  CFG_TYPE_ENUM, "SID Left Combined Waveforms",  "%s", sidchip_sel, 0,  1, 0 },
    { CFG_AUDIO_SID_ENABLE_R,   CFG_TYPE_ENUM, "SID Right",                    "%s", en_dis3,     0,  1, 1 },
    { CFG_AUDIO_SID_BASE_RIGHT, CFG_TYPE_ENUM, "SID Right Base",               "%s", sid_base,    0, 24, 8 },
    { CFG_AUDIO_SID_EXT_RIGHT,  CFG_TYPE_ENUM, "SID Right Mode",               "%s", sid_voices,  0,  1, 0 },
    { CFG_AUDIO_SID_WAVE_RIGHT, CFG_TYPE_ENUM, "SID Right Combined Waveforms", "%s", sidchip_sel, 0,  1, 0 },
//    { CFG_AUDIO_SAMPLER_IO,     CFG_TYPE_ENUM, "Map Sampler in $DF20-DFFF",    "%s", en_dis3,     0,  1, 0 },
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
    register_store(store, "Audio Output settings", def);

    effectuate_settings();
}
    
extern "C" {
    void set_sid_coefficients(volatile uint8_t *);
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

        set_sid_coefficients((volatile uint8_t *)(SID_BASE + 0x800));
    }
    if(getFpgaCapabilities() & CAPAB_ULTIMATE2PLUS) {
        unsigned char tmp = cfg->get_value(CFG_AUDIO_SPEAKER_EN);
    	U2PIO_SPEAKER_EN = (tmp << 1) | (tmp != 0);
    }
}

