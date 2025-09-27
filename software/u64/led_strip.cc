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

const char *fixed_colors[] = {
    "Red",
    "Scarlet",
    "Orange",
    "Amber",
    "Yellow",
    "Lemon-Lime",
    "Chartreuse",
    "Lime",
    "Green",
    "Jade",
    "Spring Green",
    "Aquamarine",
    "Cyan",
    "Deep Sky Blue",
    "Azure",
    "Royal Blue",
    "Blue",
    "Indigo",
    "Violet",
    "Purple",
    "Magenta",
    "Fuchsia",
    "Rose",
    "Cerise"
};
const char *color_tints[] = { "Pure", "Bright", "Pastel", "Whisper" };
static const uint8_t tint_factors[] = { 0, 50, 100, 170 };

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
    { CFG_LED_FIXED_COLOR,      CFG_TYPE_ENUM,  "Fixed Color",                  "%s", fixed_colors, 0, 23, 15  },
    { CFG_LED_FIXED_TINT,       CFG_TYPE_ENUM,  "Color tint",                   "%s", color_tints,  0,  3,  1  },
    { CFG_TYPE_END,             CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

typedef struct { uint8_t r, g, b; } RGB;

// Map hue index (0..N-1) to fully saturated, full-brightness RGB.
// Even spacing around the HSV wheel, using integer ramps.
// If N is a multiple of 6, you'll land exactly on R, Y, G, C, B, M.

static inline RGB hue_index_to_rgb(uint16_t idx, uint16_t N)
{
    // Scale hue to [0..1536) == 6 * 256; 256 steps per primary-secondary edge.
    uint32_t h = ((uint32_t)idx * 1536u) / N;
    uint8_t seg = (uint8_t)(h >> 8);   // 0..5
    uint8_t f   = (uint8_t)(h & 0xFF); // 0..255

    RGB c;
    switch (seg) {
        case 0: c.r = 255;     c.g = f;       c.b = 0;       break; // R → Y
        case 1: c.r = 255 - f; c.g = 255;     c.b = 0;       break; // Y → G
        case 2: c.r = 0;       c.g = 255;     c.b = f;       break; // G → C
        case 3: c.r = 0;       c.g = 255 - f; c.b = 255;     break; // C → B
        case 4: c.r = f;       c.g = 0;       c.b = 255;     break; // B → M
        default:c.r = 255;     c.g = 0;       c.b = 255 - f; break; // M → R
    }
    return c;
}

// Mix one 8-bit channel toward another with t in [0..255]
// t=0 → a, t=255 → b (rounded).
static inline uint8_t mix8(uint8_t a, uint8_t b, uint8_t t)
{
    return (uint8_t)(((uint32_t)a * (255 - t) + (uint32_t)b * t + 127) / 255);
}

// Add white (tint). t=0 keeps the color, t=255 gives pure white.
static inline RGB tint_with_white(RGB c, uint8_t t)
{
    RGB o;
    o.r = mix8(c.r, 255, t);
    o.g = mix8(c.g, 255, t);
    o.b = mix8(c.b, 255, t);
    return o;
}

static inline RGB apply_intensity(RGB c, uint8_t t)
{
    RGB o;
    o.r = ((uint16_t)c.r * t) >> 5;
    o.g = ((uint16_t)c.g * t) >> 5;
    o.b = ((uint16_t)c.b * t) >> 5;
    return o;
}


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
    cfg->set_change_hook(CFG_LED_FIXED_COLOR, LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_FIXED_TINT,  LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT, LedStrip :: hot_effectuate);
    cfg->hide();
    setup_config_menu();
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

    RGB fixed;

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
            fixed = hue_index_to_rgb(strip->hue, 24);
            fixed = tint_with_white(fixed, tint_factors[strip->tint]);
            fixed = apply_intensity(fixed, strip->intensity);
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            LEDSTRIP_DIRECTION = 0 | strip->protocol;
            if (strip->protocol) {
                LEDSTRIP_DATA[0] = fixed.g;
                LEDSTRIP_DATA[1] = fixed.r;
                LEDSTRIP_DATA[2] = fixed.b;
            } else {
                LEDSTRIP_DATA[0] = fixed.b;
                LEDSTRIP_DATA[1] = fixed.g;
                LEDSTRIP_DATA[2] = fixed.r;
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
    hue       = cfg->get_value(CFG_LED_FIXED_COLOR);
    tint      = cfg->get_value(CFG_LED_FIXED_TINT);
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

void LedStrip :: setup_config_menu(void)
{
    ConfigGroup *grp = ConfigGroupCollection :: getGroup(GROUP_NAME_LEDS, SORT_ORDER_CFG_LEDS);
    grp->append(ConfigItem::heading("Case Lights"));
    grp->append(cfg->find_item(CFG_LED_MODE)->set_item_altname("Mode"));
    grp->append(cfg->find_item(CFG_LED_INTENSITY)->set_item_altname("Intensity"));
    grp->append(cfg->find_item(CFG_LED_FIXED_COLOR)->set_item_altname("Color"));
    grp->append(cfg->find_item(CFG_LED_FIXED_TINT)->set_item_altname("Tint"));
    grp->append(ConfigItem::separator());
}
