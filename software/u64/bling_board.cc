/*
 * bling_board.cc
 *
 *  Created on: Jul 26, 2025
 *      Author: gideon
 *
 * This is basically a copy of led_strip.h/.cc, but with some different mappings and functions.
 */

#include "bling_board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u64.h"

static const char *modes[] = {"Off", "Fixed Color", "SID Pulse", "SID Scroll 1", "SID Scroll 2" };
static const char *sidsel[] = { "UltiSID1-A", "UltiSID1-B", "UltiSID1-C", "UltiSID1-D",
                                "UltiSID2-A", "UltiSID2-B", "UltiSID2-C", "UltiSID2-D" };

static struct t_cfg_definition cfg_definition[] = {
    { CFG_LED_MODE,             CFG_TYPE_ENUM,  "LedStrip Mode",                "%s", modes,        0,  4,  1  },
    { CFG_LED_SIDSELECT,        CFG_TYPE_ENUM,  "LedStrip SID Select",          "%s", sidsel,       0,  7,  0  },
    { CFG_LED_INTENSITY,        CFG_TYPE_VALUE, "Strip Intensity",              "%d", NULL,         0, 31, 16  },
    { CFG_LED_RED,              CFG_TYPE_VALUE, "Fixed Color Red",            "%02x", NULL,         0,255, 128 },
    { CFG_LED_GREEN,            CFG_TYPE_VALUE, "Fixed Color Green",          "%02x", NULL,         0,255, 0   },
    { CFG_LED_BLUE,             CFG_TYPE_VALUE, "Fixed Color Blue",           "%02x", NULL,         0,255, 150 },
    { CFG_TYPE_END,             CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

BlingBoard :: BlingBoard()
{
    xTaskCreate( BlingBoard :: task, "Bling Controller", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, NULL );
    register_store(0x44524557, "Keyboard Lighting", cfg_definition);
    effectuate_settings();
    cfg->set_change_hook(CFG_LED_MODE,      BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_INTENSITY, BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_RED,       BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_GREEN,     BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_BLUE,      BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT, BlingBoard :: hot_effectuate);
}

#define BLINGLEDS_DATA ( (volatile uint8_t *)(U64II_BLINGBOARD_LEDS))

#define BLINGLEDS_FROM (BLINGLEDS_DATA[LED_STARTADDR])
#define BLINGLEDS_LEN  (BLINGLEDS_DATA[LED_COUNT])
#define BLINGLEDS_INTENSITY (BLINGLEDS_DATA[LED_INTENSITY])
#define BLINGLEDS_DIRECTION (BLINGLEDS_DATA[LED_DIRECTION])
#define NUM_BLINGLEDS 71

void BlingBoard :: task(void *a)
{
    BlingBoard *strip = (BlingBoard *)a;

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
            BLINGLEDS_INTENSITY = 0;
            BLINGLEDS_DIRECTION = 0;
            BLINGLEDS_DATA[0] = 0;
            BLINGLEDS_DATA[1] = 0;
            BLINGLEDS_DATA[2] = 0;
            BLINGLEDS_FROM = 0x00;
            BLINGLEDS_LEN = 84;
            vTaskDelay(200);
            break;
        case 1: // Fixed Color
            offset = 0;
            BLINGLEDS_INTENSITY = strip->intensity << 2;
            BLINGLEDS_DIRECTION = 0;
            BLINGLEDS_DATA[0] = strip->green;
            BLINGLEDS_DATA[1] = strip->red;
            BLINGLEDS_DATA[2] = strip->blue;
            BLINGLEDS_FROM = 0x00;
            BLINGLEDS_LEN = NUM_BLINGLEDS;
            vTaskDelay(50);
            break;
        case 2: // SID Pulse
            BLINGLEDS_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            BLINGLEDS_DATA[offset++] = v1;
            BLINGLEDS_DATA[offset++] = v2;
            BLINGLEDS_DATA[offset++] = v3;
            BLINGLEDS_DIRECTION = 0x00; // Fixed address
            BLINGLEDS_FROM = start;
            BLINGLEDS_LEN = NUM_BLINGLEDS;
            vTaskDelay(7);
            break;
        case 3: // SID Scroll 1
            BLINGLEDS_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            BLINGLEDS_DATA[offset++] = v1;
            BLINGLEDS_DATA[offset++] = v2;
            BLINGLEDS_DATA[offset++] = v3;
            BLINGLEDS_DIRECTION = 0x01; // Address 'up'
            BLINGLEDS_FROM = start;
            BLINGLEDS_LEN = NUM_BLINGLEDS;
            vTaskDelay(3);
            break;
        case 4: // SID Scroll 2
            BLINGLEDS_INTENSITY = strip->intensity << 2;
            if (offset >= 0xFC) {
                offset = 0;
            }
            start = offset;
            BLINGLEDS_DATA[offset++] = v1;
            BLINGLEDS_DATA[offset++] = v2;
            BLINGLEDS_DATA[offset++] = v3;
            BLINGLEDS_DIRECTION = 0x02; // Address 'down'
            BLINGLEDS_FROM = start;
            BLINGLEDS_LEN = NUM_BLINGLEDS;
            vTaskDelay(3);
            break;
        }
    }
}

// globally static, causes constructor to be called
BlingBoard blingy;

void BlingBoard :: effectuate_settings(void)
{
    mode  = cfg->get_value(CFG_LED_MODE);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    red   = cfg->get_value(CFG_LED_RED);
    green = cfg->get_value(CFG_LED_GREEN);
    blue  = cfg->get_value(CFG_LED_BLUE);
    sidsel = cfg->get_value(CFG_LED_SIDSELECT);
}

int BlingBoard :: hot_effectuate(ConfigItem *item)
{
    item->store->set_need_effectuate();
    item->store->effectuate();
    return 0;
}
