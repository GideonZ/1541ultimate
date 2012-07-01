#ifndef AUDIO_SELECT_H
#define AUDIO_SELECT_H

#include "config.h"

#define AUDIO_SELECT_LEFT   *((volatile BYTE *)0x4060700)
#define AUDIO_SELECT_RIGHT  *((volatile BYTE *)0x4060701)

#define SOUND_DRIVE_0     0
#define SOUND_DRIVE_1     1
#define SOUND_CAS_READ    2
#define SOUND_CAS_WRITE   3
#define SOUND_SID_LEFT    4
#define SOUND_SID_RIGHT   5
#define SOUND_SAMP_LEFT   6
#define SOUND_SAMP_RIGHT  7

#define SID_VOICES        *((volatile BYTE *)0x4042000)
#define SID_FILTER_DIV    *((volatile BYTE *)0x4042001)
#define SID_BASE_LEFT     *((volatile BYTE *)0x4042002)
#define SID_BASE_RIGHT    *((volatile BYTE *)0x4042003)
#define SID_SNOOP_LEFT    *((volatile BYTE *)0x4042004)
#define SID_SNOOP_RIGHT   *((volatile BYTE *)0x4042005)
#define SID_ENABLE_LEFT   *((volatile BYTE *)0x4042006)
#define SID_ENABLE_RIGHT  *((volatile BYTE *)0x4042007)
#define SID_EXTEND_LEFT   *((volatile BYTE *)0x4042008)
#define SID_EXTEND_RIGHT  *((volatile BYTE *)0x4042009)
#define SID_COMBSEL_LEFT  *((volatile BYTE *)0x404200A)
#define SID_COMBSEL_RIGHT *((volatile BYTE *)0x404200B)

class AudioConfig : public ConfigurableObject
{
public:
    AudioConfig();
    ~AudioConfig() {}

    void effectuate_settings();
};

extern AudioConfig audio_configurator;

#endif
