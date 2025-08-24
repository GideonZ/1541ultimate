/*
 * led_strip.cc
 *
 *  Created on: Dec 12, 2018
 *      Author: gideon
 */

#include "led_strip.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u64.h"
#include "init_function.h"

// globally static
LedStrip *ledstrip = NULL;
static void init(void *_a, void *_b)
{
    ledstrip = new LedStrip();
}
InitFunction ledstrip_init_func("LED Strip", init, NULL, NULL, 61);


static const char *modes[] = {"Off", "Fixed Color", "SID Pulse", "SID Scroll 1", "SID Scroll 2" };
static const char *sidsel[] = { "UltiSID1-A", "UltiSID1-B", "UltiSID1-C", "UltiSID1-D",
                                "UltiSID2-A", "UltiSID2-B", "UltiSID2-C", "UltiSID2-D" };
static const char *types[] = { "APA102", "WS2812" };

static struct t_cfg_definition cfg_definition[] = {
    { CFG_LED_TYPE,             CFG_TYPE_ENUM,  "LedStrip Type",                "%s", types,        0,  1,  1  },
    { CFG_LED_MODE,             CFG_TYPE_ENUM,  "LedStrip Mode",                "%s", modes,        0,  4,  0  },
    { CFG_LED_SIDSELECT,        CFG_TYPE_ENUM,  "LedStrip SID Select",          "%s", sidsel,       0,  7,  0  },
    { CFG_LED_INTENSITY,        CFG_TYPE_VALUE, "Strip Intensity",              "%d", NULL,         0, 31, 16  },
    { CFG_LED_LENGTH,           CFG_TYPE_VALUE, "Strip Length",                 "%d", NULL,         1, 84, 24  },
    { CFG_LED_RED,              CFG_TYPE_VALUE, "Fixed Color Red",            "%02x", NULL,         0,255, 128 },
    { CFG_LED_GREEN,            CFG_TYPE_VALUE, "Fixed Color Green",          "%02x", NULL,         0,255, 0   },
    { CFG_LED_BLUE,             CFG_TYPE_VALUE, "Fixed Color Blue",           "%02x", NULL,         0,255, 150 },
    { CFG_TYPE_END,             CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

LedStrip :: LedStrip()
{
    // because higher priority task may start right away, the return value of the constructor
    // may not yet been written in the global variable.
	ledstrip = this; 
    xTaskCreate( LedStrip :: task, "LedStrip Controller", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, NULL );
    register_store(0x4C454453, "LED Strip Settings", cfg_definition);
    effectuate_settings();
    cfg->set_change_hook(CFG_LED_LENGTH,    LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_TYPE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_MODE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_INTENSITY, LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_RED,       LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_GREEN,     LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_BLUE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT, LedStrip :: hot_effectuate);
}

#define LEDSTRIP_FROM (LEDSTRIP_DATA[LED_STARTADDR])
#define LEDSTRIP_LEN  (LEDSTRIP_DATA[LED_COUNT])
#define LEDSTRIP_INTENSITY (LEDSTRIP_DATA[LED_INTENSITY])
#define LEDSTRIP_DIRECTION (LEDSTRIP_DATA[LED_DIRECTION])


void LedStrip :: task(void *a)
{
    LedStrip *strip = (LedStrip *)a;

    uint8_t offset = 0;
    uint8_t start = 0;
    uint8_t v1, v2, v3;

    while(1) {
        v1 = C64_VOICE_ADSR(strip->sidsel * 4 + 0);
        v2 = C64_VOICE_ADSR(strip->sidsel * 4 + 1);
        v3 = C64_VOICE_ADSR(strip->sidsel * 4 + 2);

        switch (strip->mode) {
        case 0: // Off
            offset = 0;
            U64_LEDSTRIP_EN = 0;
            LEDSTRIP_INTENSITY = 0;
            LEDSTRIP_DIRECTION = 0 | strip->protocol;
            LEDSTRIP_DATA[0] = 0;
            LEDSTRIP_DATA[1] = 0;
            LEDSTRIP_DATA[2] = 0;
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_LEN = 84;
            vTaskDelay(200);
            break;
        case 1: // Fixed Color
            offset = 0;
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            LEDSTRIP_DIRECTION = 0 | strip->protocol;
            if (strip->protocol) {
                LEDSTRIP_DATA[0] = strip->green;
                LEDSTRIP_DATA[1] = strip->red;
                LEDSTRIP_DATA[2] = strip->blue;
            } else {
                LEDSTRIP_DATA[0] = strip->blue;
                LEDSTRIP_DATA[1] = strip->green;
                LEDSTRIP_DATA[2] = strip->red;
            }
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_LEN = strip->length;
            vTaskDelay(50);
            break;
        case 2: // SID Pulse
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            LEDSTRIP_DIRECTION = 0x00 | strip->protocol; // Fixed address
            LEDSTRIP_FROM = start;
            LEDSTRIP_LEN = strip->length;
            vTaskDelay(7);
            break;
        case 3: // SID Scroll 1
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            LEDSTRIP_DIRECTION = 0x01 | strip->protocol; // Address 'up'
            LEDSTRIP_FROM = start;
            LEDSTRIP_LEN = strip->length;
            vTaskDelay(3);
            break;
        case 4: // SID Scroll 2
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            LEDSTRIP_DIRECTION = 0x02 | strip->protocol; // Address 'down'
            LEDSTRIP_FROM = start;
            LEDSTRIP_LEN = strip->length;
            vTaskDelay(3);
            break;
        default:
            strip->mode = 0;
        }
    }
}

void LedStrip :: effectuate_settings(void)
{
    mode  = cfg->get_value(CFG_LED_MODE);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    red   = cfg->get_value(CFG_LED_RED);
    green = cfg->get_value(CFG_LED_GREEN);
    blue  = cfg->get_value(CFG_LED_BLUE);
    sidsel = cfg->get_value(CFG_LED_SIDSELECT);
    length = cfg->get_value(CFG_LED_LENGTH);
    protocol = cfg->get_value(CFG_LED_TYPE) ? 0x80 : 0x00; // 0x80 = WS2812, 0x00 = APA102;

    U64_PWM_DUTY = 0xC0;
}

int LedStrip :: hot_effectuate(ConfigItem *item)
{
    item->store->set_need_effectuate();
    item->store->effectuate();
    return 0;
}
