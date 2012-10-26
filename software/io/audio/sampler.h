#ifndef SAMPLER_H
#define SAMPLER_H

#include "event.h"
#include "poll.h"
#include "menu.h"

#define SAMPLER_BASE        0x5048000

#define VOICES              8
#define BYTES_PER_VOICE     32

// #define VOICE_CONTROL       *((volatile BYTE *) SAMPLER_BASE + 0x00)
// #define VOICE_VOLUME        *((volatile BYTE *) SAMPLER_BASE + 0x01)
// #define VOICE_PAN           *((volatile BYTE *) SAMPLER_BASE + 0x02)
// #define VOICE_START         *((volatile DWORD *)SAMPLER_BASE + 0x04)
// #define VOICE_LENGTH        *((volatile DWORD *)SAMPLER_BASE + 0x08)
// #define VOICE_RATE          *((volatile DWORD *)SAMPLER_BASE + 0x0C)
// #define VOICE_RATE_H        *((volatile WORD *) SAMPLER_BASE + 0x0E)
// #define VOICE_RATE_L        *((volatile WORD *) SAMPLER_BASE + 0x0F)

#define SAMPLER_VERSION        *((volatile BYTE *) (SAMPLER_BASE + 1))
#define VOICE_CONTROL(x)       *((volatile BYTE *) (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x00))
#define VOICE_VOLUME(x)        *((volatile BYTE *) (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x01))
#define VOICE_PAN(x)           *((volatile BYTE *) (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x02))
#define VOICE_START(x)         *((volatile DWORD *)(SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x04))
#define VOICE_LENGTH(x)        *((volatile DWORD *)(SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x08))
#define VOICE_REPEAT_A(x)      *((volatile DWORD *)(SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x10))
#define VOICE_REPEAT_B(x)      *((volatile DWORD *)(SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x14))
#define VOICE_RATE(x)          *((volatile DWORD *)(SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0C))
#define VOICE_RATE_H(x)        *((volatile WORD *) (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0E))
#define VOICE_RATE_L(x)        *((volatile WORD *) (SAMPLER_BASE + (x*BYTES_PER_VOICE) + 0x0F))

#define VOICE_CTRL_ENABLE   0x01
#define VOICE_CTRL_REPEAT   0x02
#define VOICE_CTRL_IRQ      0x04
#define VOICE_CTRL_8BIT     0x00
#define VOICE_CTRL_16BIT    0x10

#define MENU_SAMP_PLAY8B  0x8301
#define MENU_SAMP_PLAY16B 0x8302

class Sampler : public ObjectWithMenu
{
public:
    Sampler();
    ~Sampler();

    void poll(Event &e);
	int  fetch_task_items(IndexedList<PathObject*> &item_list);
};

#endif
