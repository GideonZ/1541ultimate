#ifndef AUDIO_SELECT_H
#define AUDIO_SELECT_H

#include "config.h"
#include "iomap.h"

#define AUDIO_SELECT_LEFT   *((volatile uint8_t *)(AUDIO_SEL_BASE + 0x00))
#define AUDIO_SELECT_RIGHT  *((volatile uint8_t *)(AUDIO_SEL_BASE + 0x01))

#define SOUND_DRIVE_0     0
#define SOUND_DRIVE_1     1
#define SOUND_CAS_READ    2
#define SOUND_CAS_WRITE   3
#define SOUND_SID_LEFT    4
#define SOUND_SID_RIGHT   5
#define SOUND_SAMP_LEFT   6
#define SOUND_SAMP_RIGHT  7

#define SID_VOICES        *((volatile uint8_t *)(SID_BASE + 0x00))
#define SID_FILTER_DIV    *((volatile uint8_t *)(SID_BASE + 0x01))
#define SID_BASE_LEFT     *((volatile uint8_t *)(SID_BASE + 0x02))
#define SID_BASE_RIGHT    *((volatile uint8_t *)(SID_BASE + 0x03))
#define SID_SNOOP_LEFT    *((volatile uint8_t *)(SID_BASE + 0x04))
#define SID_SNOOP_RIGHT   *((volatile uint8_t *)(SID_BASE + 0x05))
#define SID_ENABLE_LEFT   *((volatile uint8_t *)(SID_BASE + 0x06))
#define SID_ENABLE_RIGHT  *((volatile uint8_t *)(SID_BASE + 0x07))
#define SID_EXTEND_LEFT   *((volatile uint8_t *)(SID_BASE + 0x08))
#define SID_EXTEND_RIGHT  *((volatile uint8_t *)(SID_BASE + 0x09))
#define SID_COMBSEL_LEFT  *((volatile uint8_t *)(SID_BASE + 0x0A))
#define SID_COMBSEL_RIGHT *((volatile uint8_t *)(SID_BASE + 0x0B))

class AudioConfig : public ConfigurableObject
{
    int *map;
public:
    AudioConfig();
    ~AudioConfig() {}

    void effectuate_settings();
    void clear_sampler_registers();
};

extern AudioConfig audio_configurator;

#endif
