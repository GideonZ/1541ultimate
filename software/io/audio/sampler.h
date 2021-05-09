#ifndef SAMPLER_H
#define SAMPLER_H

#include "menu.h"
#include "iomap.h"

#define VOICES              8
#define BYTES_PER_VOICE     32

#define SAMPLER_VERSION        (SAMPLER_BASE + 1)
#define VOICE_CONTROL(x)       (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x00)
#define VOICE_VOLUME(x)        (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x01)
#define VOICE_PAN(x)           (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x02)
#define VOICE_START(x)         (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x04)
#define VOICE_LENGTH(x)        (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x08)
#define VOICE_REPEAT_A(x)      (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x10)
#define VOICE_REPEAT_B(x)      (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x14)
#define VOICE_RATE(x)          (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0C)
#define VOICE_RATE_H(x)        (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0E)
#define VOICE_RATE_L(x)        (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0F)

#define VOICE_CTRL_ENABLE   0x01
#define VOICE_CTRL_REPEAT   0x02
#define VOICE_CTRL_IRQ      0x04
#define VOICE_CTRL_8BIT     0x00
#define VOICE_CTRL_16BIT    0x10


class Sampler
{
public:
    Sampler() {}
    ~Sampler() {}

	static void reset(void) {
        for(int i=0; i < 8; i++) {
            ioWrite8(VOICE_CONTROL(i), 0);
        }
    }
};

#endif
