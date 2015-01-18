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

char *aud_choices[]  = { "Drive A", "Drive B", "Cassette Read", "Cassette Write", "SID Left", "SID Right", "Sampler Left", "Sampler Right" };
char *aud_choices2[] = { "Drive A", "Drive B", "Cassette Read", "Cassette Write", "Sampler Left", "Sampler Right" };

char *sid_base[] = { "Snoop $D400", "Snoop $D420", "Snoop $D480", "Snoop $D500", "Snoop $D580", 
                     "Snoop $D600", "Snoop $D680", "Snoop $D700", "Snoop $D780",
                     "IO $DE00", "IO $DE20", "IO $DE40", "IO $DE60",
                     "IO $DE80", "IO $DEA0", "IO $DEC0", "IO $DEE0",
                     "IO $DF00", "IO $DF20", "IO $DF40", "IO $DF60",
                     "IO $DF80", "IO $DFA0", "IO $DFC0", "IO $DFE0" };

BYTE sid_offsets[] = { 0x40, 0x42, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78,
                       0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee,
                       0xf0, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe };
                       
char *sid_voices[] = { "Standard", "8 Voices" };
char *en_dis3[] = { "Disabled", "Enabled" };
char *sidchip_sel[] = { "6581", "8580" };

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

int normal_map[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
int skip_sid[8]   = { 0, 1, 2, 3, 6, 7, 6, 7 };

AudioConfig :: AudioConfig()
{
    struct t_cfg_definition *def = audio_cfg;
    DWORD store = 0x41554449;    
    DWORD CAPABILITIES = getFpgaCapabilities();

    if(CAPABILITIES & CAPAB_STEREO_SID) {
        map = normal_map;
        def = audio_cfg;
        if(CAPABILITIES & CAPAB_SAMPLER) {
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
        if(CAPABILITIES & CAPAB_SAMPLER) {
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
    
void AudioConfig :: effectuate_settings()
{
    if(!cfg)
        return;

    AUDIO_SELECT_LEFT   = map[cfg->get_value(CFG_AUDIO_SELECT_LEFT)];
    AUDIO_SELECT_RIGHT  = map[cfg->get_value(CFG_AUDIO_SELECT_RIGHT)];
    
    if(getFpgaCapabilities() & CAPAB_STEREO_SID) {
        printf("Number of SID voices implemented in FPGA: %d\n", SID_VOICES);
        SID_BASE_LEFT     = sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_LEFT)];
        SID_SNOOP_LEFT    = (sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_LEFT)] & 0x80)?0:1;
        SID_ENABLE_LEFT   = cfg->get_value(CFG_AUDIO_SID_ENABLE_L);
        SID_EXTEND_LEFT   = cfg->get_value(CFG_AUDIO_SID_EXT_LEFT);
        SID_COMBSEL_LEFT  = cfg->get_value(CFG_AUDIO_SID_WAVE_LEFT);
    
        SID_BASE_RIGHT    = sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_RIGHT)];
        SID_SNOOP_RIGHT   = (sid_offsets[cfg->get_value(CFG_AUDIO_SID_BASE_RIGHT)] & 0x80)?0:1;
        SID_ENABLE_RIGHT  = cfg->get_value(CFG_AUDIO_SID_ENABLE_R);
        SID_EXTEND_RIGHT  = cfg->get_value(CFG_AUDIO_SID_EXT_RIGHT);
        SID_COMBSEL_RIGHT = cfg->get_value(CFG_AUDIO_SID_WAVE_RIGHT);
    }
}

void AudioConfig :: clear_sampler_registers()
{
    volatile DWORD *sampler = (volatile DWORD *)SAMPLER_BASE;
    if(getFpgaCapabilities() & CAPAB_SAMPLER) {
        for(int i=0;i<64;i++) {
            *(sampler++) = 0;
        }
    }
}
