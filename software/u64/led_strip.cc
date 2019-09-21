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

static const char *modes[] = {"Off", "Fixed Color", "SID Pulse", "SID Scroll 1", "SID Scroll 2" };
static const char *sidsel[] = { "UltiSID1-A", "UltiSID1-B", "UltiSID1-C", "UltiSID1-D",
                                "UltiSID2-A", "UltiSID2-B", "UltiSID2-C", "UltiSID2-D" };

static struct t_cfg_definition cfg_definition[] = {
    { CFG_LED_MODE,             CFG_TYPE_ENUM,  "LedStrip Mode",                "%s", modes,        0,  4,  0  },
    { CFG_LED_SIDSELECT,        CFG_TYPE_ENUM,  "LedStrip SID Select",          "%s", sidsel,       0,  7,  0  },
    { CFG_LED_INTENSITY,        CFG_TYPE_VALUE, "Strip Intensity",              "%d", NULL,         0, 31, 16  },
    { CFG_LED_RED,              CFG_TYPE_VALUE, "Fixed Color Red",            "%02x", NULL,         0,255, 128 },
    { CFG_LED_GREEN,            CFG_TYPE_VALUE, "Fixed Color Green",          "%02x", NULL,         0,255, 0   },
    { CFG_LED_BLUE,             CFG_TYPE_VALUE, "Fixed Color Blue",           "%02x", NULL,         0,255, 150 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

LedStrip :: LedStrip()
{
    xTaskCreate( LedStrip :: task, "LedStrip Controller", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 3, NULL );
    register_store(0x4C454453, "LED Strip Settings", cfg_definition);
    effectuate_settings();
    cfg->set_change_hook(CFG_LED_MODE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_INTENSITY, LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_RED,       LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_GREEN,     LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_BLUE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT, LedStrip :: hot_effectuate);
}

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
        case 0:
            LEDSTRIP_DATA[offset++] = 0xE0; // intensity 0
            LEDSTRIP_DATA[offset++] = 0;
            LEDSTRIP_DATA[offset++] = 0;
            LEDSTRIP_DATA[offset++] = 0;
            LEDSTRIP_FROM = 0x00 | (start & 0x3F);
            LEDSTRIP_LEN = 25;
            start ++;
            vTaskDelay(200);
            U64_LEDSTRIP_EN = 0;
            break;
        case 1:
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_DATA[offset++] = 0xE0 | strip->intensity;
            LEDSTRIP_DATA[offset++] = strip->blue;
            LEDSTRIP_DATA[offset++] = strip->green;
            LEDSTRIP_DATA[offset++] = strip->red;
            LEDSTRIP_FROM = 0x00 | (start & 0x3F);
            LEDSTRIP_LEN = 25;
            start ++;
            vTaskDelay(50);
            break;
        case 2:
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_DATA[offset++] = 0xE0 | strip->intensity;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            // LEDSTRIP_FROM = 0x40 | (start & 0x3F);
            LEDSTRIP_FROM = 0x00 | (start & 0x3F);
            LEDSTRIP_LEN = 25;
            start ++;
            vTaskDelay(7);
            break;
        case 3:
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_DATA[offset++] = 0xE0 | strip->intensity;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            LEDSTRIP_FROM = 0x40 | ((start - 24) & 0x3F);
            LEDSTRIP_LEN = 25;
            start ++;
            vTaskDelay(3);
            break;
        case 4:
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_DATA[offset++] = 0xE0 | strip->intensity;
            LEDSTRIP_DATA[offset++] = v1;
            LEDSTRIP_DATA[offset++] = v2;
            LEDSTRIP_DATA[offset++] = v3;
            LEDSTRIP_FROM = 0x80 | (start & 0x3F);
            LEDSTRIP_LEN = 25;
            start ++;
            vTaskDelay(3);
            break;
        }
    }
}

// globally static, causes constructor to be called
LedStrip ledstrip;

void LedStrip :: effectuate_settings(void)
{
    mode  = cfg->get_value(CFG_LED_MODE);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    red   = cfg->get_value(CFG_LED_RED);
    green = cfg->get_value(CFG_LED_GREEN);
    blue  = cfg->get_value(CFG_LED_BLUE);
    sidsel = cfg->get_value(CFG_LED_SIDSELECT);

    U64_PWM_DUTY = 0xC0;
}

int LedStrip :: hot_effectuate(ConfigItem *item)
{
    item->store->effectuate();
    return 0;
}
