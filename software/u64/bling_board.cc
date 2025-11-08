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

extern const char *fixed_colors[];
extern const char *color_tints[];
static const char *modes[] = {"Off", "Fixed Color", "SID Pulse", "SID Music", "Rainbow", "Rainbow Sparkle" };
static const char *patterns[] = { "Default", "Left to Right", "Right to Left", "Circular", "Outward" };
static const char *sidsel[] = { "UltiSID1-A", "UltiSID1-B", "UltiSID1-C", "UltiSID1-D",
                                "UltiSID2-A", "UltiSID2-B", "UltiSID2-C", "UltiSID2-D" };

static const uint8_t tint_factors[] = { 0, 50, 100, 170 };

static struct t_cfg_definition cfg_definition[] = {
    { CFG_LED_MODE,             CFG_TYPE_ENUM,  "LedStrip Mode",                "%s", modes,        0,  5,  0  },
    { CFG_LED_PATTERN,          CFG_TYPE_ENUM,  "LedStrip Pattern",             "%s", patterns,     0,  4,  0  },
    { CFG_LED_SIDSELECT,        CFG_TYPE_ENUM,  "LedStrip SID Select",          "%s", sidsel,       0,  7,  0  },
    { CFG_LED_INTENSITY,        CFG_TYPE_VALUE, "Strip Intensity",              "%d", NULL,         0, 31, 16  },
    { CFG_LED_FIXED_COLOR,      CFG_TYPE_ENUM,  "Fixed Color",                  "%s", fixed_colors, 0, 23, 15  },
    { CFG_LED_FIXED_TINT,       CFG_TYPE_ENUM,  "Color tint",                   "%s", color_tints,  0,  3,  1  },
    { CFG_TYPE_END,             CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

#include <math.h>
#include <stdint.h>

static const uint8_t GAMMA22[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
    3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
    6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
   12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
   20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
   30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
   42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
   56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
   73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
   91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
  113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
  137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
  163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
  192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
  223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255
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

// After tint/brightness:
static inline void apply_gamma_inplace(RGB *c)
{
    c->r = GAMMA22[c->r];
    c->g = GAMMA22[c->g];
    c->b = GAMMA22[c->b];
}

BlingBoard :: BlingBoard()
{
    speed = 10;
    xTaskCreate( BlingBoard :: task, "Bling Controller", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, NULL );
    register_store(0x44524557, "Keyboard Lighting", cfg_definition);
    effectuate_settings();
    cfg->set_change_hook(CFG_LED_MODE,        BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_PATTERN,     BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_INTENSITY,   BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_FIXED_COLOR, BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_FIXED_TINT,  BlingBoard :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT,   BlingBoard :: hot_effectuate);
    cfg->hide();
    setup_config_menu();
}

#define LEDSTRIP_DATA ( (volatile uint8_t *)(U64II_BLINGBOARD_LEDS))
#define LEDSTRIP_FROM (LEDSTRIP_DATA[LED_STARTADDR])
#define LEDSTRIP_START  (LEDSTRIP_DATA[LED_START])
#define LEDSTRIP_INTENSITY (LEDSTRIP_DATA[LED_INTENSITY])
#define LEDSTRIP_MAP_ENABLE (LEDSTRIP_DATA[LED_MAP])
#define NUM_BLINGLEDS 71

void BlingBoard :: MapDirect(void)
{
    printf("Map DIRECT\n");
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0;i<84*3;i++) {
        LEDSTRIP_DATA[i] = (uint8_t)i;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void BlingBoard :: MapSingleColor(void)
{
    printf("Map Single Color\n");
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0, j=0;i<84;i++) {
        LEDSTRIP_DATA[j++] = 0;
        LEDSTRIP_DATA[j++] = 1;
        LEDSTRIP_DATA[j++] = 2;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void BlingBoard :: MapLeftToRight(void)
{
    int mapping[] = { -1,  5, 36, -1, -1,
                      -1,  6, 35, 37, 69,
                       4,  7, 34, 38, 68,
                      -1,  8, 33, 39, 67,
                       3,  9, 32, 40, 66,
                      -1, 10, 31, 41, 65,
                       2, 11, 30, 42, 64,
                      -1, 12, 29, 43, 63,
                       1, 13, 28, 44, 62,
                      -1, 14, 27, 45, 61,
                       0, 15, 26, 46, 60,
                      -1, 16, 25, 47, 59,
                      -1, 17, 24, 48, 58,
                      -1, -1, 23, 49, 57,
                      -1, 18, 22, 50, 56,
                      -1, 19, 51, 54, 55,
                      -1, 20, 21, 52, 53,

                  };

    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    int *k = mapping;
    LEDSTRIP_MAP_ENABLE = 1;
    for(uint8_t i=0;i<17;i++) {
        for(int j=0;j<5;j++) {
            int n = *(k++);            
            if (n >= 0) {
                LEDSTRIP_DATA[3*n+0] = 3*i+0;
                LEDSTRIP_DATA[3*n+1] = 3*i+1;
                LEDSTRIP_DATA[3*n+2] = 3*i+2;
            }
        }
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void BlingBoard :: MapRightToLeft(void)
{
    int mapping[] = {
                      -1, 20, 21, 52, 53,
                      -1, 19, 51, 54, 55,
                      -1, 18, 22, 50, 56,
                      -1, -1, 23, 49, 57,
                      -1, 17, 24, 48, 58,
                      -1, 16, 25, 47, 59,
                       0, 15, 26, 46, 60,
                      -1, 14, 27, 45, 61,
                       1, 13, 28, 44, 62,
                      -1, 12, 29, 43, 63,
                       2, 11, 30, 42, 64,
                      -1, 10, 31, 41, 65,
                       3,  9, 32, 40, 66,
                      -1,  8, 33, 39, 67,
                       4,  7, 34, 38, 68,
                      -1,  6, 35, 37, 69,
                      -1,  5, 36, -1, -1,
                  };

    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    int *k = mapping;
    LEDSTRIP_MAP_ENABLE = 1;
    for(uint8_t i=0;i<17;i++) {
        for(int j=0;j<5;j++) {
            int n = *(k++);            
            if (n >= 0) {
                LEDSTRIP_DATA[3*n+0] = 3*i+0;
                LEDSTRIP_DATA[3*n+1] = 3*i+1;
                LEDSTRIP_DATA[3*n+2] = 3*i+2;
            }
        }
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void BlingBoard :: MapFromCenter(void)
{
    int mapping[] = {
                      62, 44, -1, -1, -1, -1, -1, -1, 61,
                      63, 43, 29, 12, -1, -1, 28, 45, 60,
                      64, 42, 30, 11,  2, 13, 27, 46, 59,
                      65, 41, 31, 10,  1, 14, 26, 47, 58,
                      66, 40, 32,  9,  3, 15, 25, 48, 57,
                      67, 39, 33,  8,  0, 16, 24, 49, 56,
                      68, 38, 34,  7,  4, 17, 23, 50, 55,
                      69, 37, 35,  6, -1, 18, 22, 51, 54,
                      36,  5, -1, 70, 19, 20, 21, 52, 53,
                  };

    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    int *k = mapping;
    LEDSTRIP_MAP_ENABLE = 1;
    for(uint8_t i=0;i<9;i++) {
        for(int j=0;j<9;j++) {
            int n = *(k++);            
            if (n >= 0) {
                LEDSTRIP_DATA[3*n+0] = 3*i+0;
                LEDSTRIP_DATA[3*n+1] = 3*i+1;
                LEDSTRIP_DATA[3*n+2] = 3*i+2;
            }
        }
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void BlingBoard :: MapCircular(void)
{
    short mapping[] = {
        28, -1, -1, -1, -1, -1, -1,
        27, 29, 44, -1, -1, -1, -1,
        12, 13, 43, 45, -1, -1, -1,
        11, 14, 26, 30, 46, 61, -1,
        1, 2, 42, 60, 62, -1, -1,
        10, 15, 25, 31, 47, 59, 63,
        0, 9, 16, 41, 64, -1, -1,
        3, 24, 32, 48, 58, -1, -1,
        8, 40, 57, 65, -1, -1, -1,
        17, 23, 33, 49, 66, -1, -1,
        4, 7, 39, 56, -1, -1, -1,
        18, 34, 38, 50, 67, -1, -1,
        22, 55, -1, -1, -1, -1, -1,
        6, 19, 35, 51, 68, -1, -1,
        37, 54, -1, -1, -1, -1, -1,
        5, 36, 69, -1, -1, -1, -1,
        20, 21, 52, 53, -1, -1, -1,
    };

    short mapping2[] = {28, 27, 29, 44, 12, 13, 45, 43, 11, 14, 26, 30,
                        61, 46, 62, 1,  2,  60, 42, 63, 10, 15, 47, 25,
                        31, 59, 0,  41, 64, 9,  16, 3,  58, 48, 24, 32,
                        65, 40, 8,  57, 17, 49, 23, 33, 66, 39, 7,  4,
                        56, 50, 34, 67, 18, 38, 22, 55, 6,  35, 68, 19,
                        51, 54, 37, 69, 36, 5,  21, 20, 52, 53, 70 };

    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
/*
    short *k = mapping;
    LEDSTRIP_MAP_ENABLE = 1;
    for(uint8_t i=0;i<17;i++) {
        for(int j=0;j<7;j++) {
            int n = *(k++);            
            if (n >= 0) {
                LEDSTRIP_DATA[3*n+0] = 3*i+0;
                LEDSTRIP_DATA[3*n+1] = 3*i+1;
                LEDSTRIP_DATA[3*n+2] = 3*i+2;
            }
        }
    }
    LEDSTRIP_MAP_ENABLE = 0;
*/
    uint8_t pos = 0;
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0;i<71;i++) {
        LEDSTRIP_DATA[3*mapping2[i]+0] = pos++;
        LEDSTRIP_DATA[3*mapping2[i]+1] = pos++;
        LEDSTRIP_DATA[3*mapping2[i]+2] = pos++;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}


void BlingBoard :: task(void *a)
{
    BlingBoard *strip = (BlingBoard *)a;

    int rainbow_hue = 0;
    uint8_t offset = 0;
    uint8_t v1, v2, v3;
    int rnd, lfsr = 64738;
    uint8_t spp = 0x00, spr = 0, spg = 0, spb = 0;

    RGB fixed;
    static uint8_t backup[252];

    while(1) {
        v1 = C64_VOICE_ADSR(strip->sidsel * 4 + 0);
        v2 = C64_VOICE_ADSR(strip->sidsel * 4 + 1);
        v3 = C64_VOICE_ADSR(strip->sidsel * 4 + 2);

        switch (strip->mode) {
        case 0: // Off
            offset = 0;
            LEDSTRIP_INTENSITY = 0;
            LEDSTRIP_DATA[0] = 0;
            LEDSTRIP_DATA[1] = 0;
            LEDSTRIP_DATA[2] = 0;
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_START = 0;
            vTaskDelay(200);
            U64_LEDSTRIP_EN = 0;
            break;
        case 1: // Fixed Color
            offset = 0;
            U64_LEDSTRIP_EN = 1;
            fixed = hue_index_to_rgb(strip->hue, 24);
            fixed = tint_with_white(fixed, tint_factors[strip->tint]);
            fixed = apply_intensity(fixed, strip->intensity);
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            LEDSTRIP_DATA[0] = fixed.g;
            LEDSTRIP_DATA[1] = fixed.r;
            LEDSTRIP_DATA[2] = fixed.b;
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_START = 0;
            vTaskDelay(50);
            break;
        case 2: // SID Music Pulse
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            offset = 0;
            LEDSTRIP_DATA[0] = v1;
            LEDSTRIP_DATA[1] = v2;
            LEDSTRIP_DATA[2] = v3;
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_START = 0;
            vTaskDelay(strip->speed);
            break;
        case 3: // SID Scroll 1 // shift new data in.
            // So first data appears at address 0, offset 0
            // then new data is written to LED 83, and offset is set to 83.
            // then new data is written to LED 82, and offset it set to 82.
            // etc
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            LEDSTRIP_DATA[offset+0] = v1;
            LEDSTRIP_DATA[offset+1] = v2;
            LEDSTRIP_DATA[offset+2] = v3;
            LEDSTRIP_FROM = offset;
            if (offset == 0) {
                offset = 3*70;
            } else {
                offset -= 3;
            }
            LEDSTRIP_START = 0;
            vTaskDelay(strip->speed);
            break;
        case 4: // rainbow
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = strip->intensity << 2;
            rainbow_hue+=6;
            if (rainbow_hue >= 768)
                rainbow_hue -= 768;
            fixed = hue_index_to_rgb(rainbow_hue, 768);
            fixed = tint_with_white(fixed, tint_factors[strip->tint]);
            LEDSTRIP_DATA[offset+0] = fixed.g;
            LEDSTRIP_DATA[offset+1] = fixed.r;
            LEDSTRIP_DATA[offset+2] = fixed.b;
            LEDSTRIP_FROM = offset;
            if (offset <= 0) {
                offset = 3*70;
            } else {
                offset -= 3;
            }
            LEDSTRIP_START = 0;
            vTaskDelay(strip->speed);
            break;

        case 5: // rainbow with sparkle
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = 0x7F; // do it with the data itself
            rainbow_hue+=10;
            if (rainbow_hue >= 768)
                rainbow_hue -= 768;

            fixed = hue_index_to_rgb(rainbow_hue, 768);
            fixed = tint_with_white(fixed, tint_factors[strip->tint]);
            fixed = apply_intensity(fixed, strip->intensity);
            backup[offset+0] = fixed.g;
            backup[offset+1] = fixed.r;
            backup[offset+2] = fixed.b;

            LEDSTRIP_DATA[offset+0] = fixed.g;
            LEDSTRIP_DATA[offset+1] = fixed.r;
            LEDSTRIP_DATA[offset+2] = fixed.b;
            LEDSTRIP_FROM = offset;
            if (offset == 0) {
                offset = 3*70;
            } else {
                offset -= 3;
            }

            // restore sparkle color
            if (spr != 0xFF) {
                LEDSTRIP_DATA[spr*3+0] = backup[spr*3+0];
                LEDSTRIP_DATA[spr*3+1] = backup[spr*3+1];
                LEDSTRIP_DATA[spr*3+2] = backup[spr*3+2];
                spr = 0xFF;
            }
            if (lfsr < 0) {
                lfsr = (lfsr << 1) ^ 0x04C11DB7;
            } else {
                lfsr <<= 1;
            }
            rnd = lfsr & 0x03FF;
            if (rnd < NUM_BLINGLEDS) {
                spr = rnd;
                LEDSTRIP_DATA[rnd*3+0] = 0xFF;
                LEDSTRIP_DATA[rnd*3+1] = 0xFF;
                LEDSTRIP_DATA[rnd*3+2] = 0xFF;

            }
            LEDSTRIP_START = 0;
            vTaskDelay(5);
            break;

        default:
            strip->mode = 0;
        }
    }
}

// globally static, causes constructor to be called
BlingBoard blingy;

void BlingBoard :: effectuate_settings(void)
{
    mode      = cfg->get_value(CFG_LED_MODE);
    pattern   = cfg->get_value(CFG_LED_PATTERN);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    hue       = cfg->get_value(CFG_LED_FIXED_COLOR);
    tint      = cfg->get_value(CFG_LED_FIXED_TINT);
    sidsel    = cfg->get_value(CFG_LED_SIDSELECT);

    switch(mode)
    {
        case 0:
        case 1:
        case 2:
            MapSingleColor();
            break;
        default:
            switch(pattern) {
                case 0: // default
                    MapDirect();
                    speed = 5;
                    break;
                case 1: // left to right
                    MapLeftToRight();
                    speed = 20;
                    break;
                case 2: // right to left
                    MapRightToLeft();
                    speed = 20;
                    break;
                case 3: // circular
                    MapCircular();
                    speed = 5;
                    break;
                case 4: // from center outward
                    MapFromCenter();
                    speed = 20;
                    break;
                default:
                    MapDirect();
                    speed = 5;
                    break;
            }
            break;
    }
}

int BlingBoard :: hot_effectuate(ConfigItem *item)
{
    item->store->set_need_effectuate();
    item->store->effectuate();
    return 0;
}

void BlingBoard :: setup_config_menu(void)
{
    ConfigGroup *grp = ConfigGroupCollection :: getGroup(GROUP_NAME_LEDS, SORT_ORDER_CFG_LEDS);
    grp->append(ConfigItem::heading("Keyboard Lights"));
    grp->append(cfg->find_item(CFG_LED_MODE)->set_item_altname("Mode"));
    grp->append(cfg->find_item(CFG_LED_PATTERN)->set_item_altname("Pattern"));
    grp->append(cfg->find_item(CFG_LED_INTENSITY)->set_item_altname("Intensity"));
    grp->append(cfg->find_item(CFG_LED_FIXED_COLOR)->set_item_altname("Color"));
    grp->append(cfg->find_item(CFG_LED_FIXED_TINT)->set_item_altname("Tint"));
    grp->append(ConfigItem::separator());
}
