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
    "Cerise",
    "White"
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


static const char *modes[] = {"Off", "Fixed Color", "SID Music", "Rainbow", "Rainbow Sparkle" };
static const char *patterns[] = { "SingleColor", "Left to Right", "Right to Left", "Serpentine", "Outward"};
static const char *sidsel[] = { "UltiSID1-A", "UltiSID1-B", "UltiSID1-C", "UltiSID1-D",
                                "UltiSID2-A", "UltiSID2-B", "UltiSID2-C", "UltiSID2-D" };
// static const char *types[] = { "APA102", "WS2812" };

static struct t_cfg_definition cfg_definition[] = {
//    { CFG_LED_TYPE,             CFG_TYPE_ENUM,  "LedStrip Type",                "%s", types,        0,  1,  1  },
    { CFG_LED_MODE,             CFG_TYPE_ENUM,  "LedStrip Mode",                "%s", modes,        0,  4,  1  },
    { CFG_LED_PATTERN,          CFG_TYPE_ENUM,  "LedStrip Pattern",             "%s", patterns,     0,  4,  0  },
    { CFG_LED_SIDSELECT,        CFG_TYPE_ENUM,  "LedStrip SID Select",          "%s", sidsel,       0,  7,  0  },
    { CFG_LED_INTENSITY,        CFG_TYPE_VALUE, "Strip Intensity",              "%d", NULL,         0, 31,  9  },
    { CFG_LED_FIXED_COLOR,      CFG_TYPE_ENUM,  "Fixed Color",                  "%s", fixed_colors, 0, 24, 15  },
    { CFG_LED_FIXED_TINT,       CFG_TYPE_ENUM,  "Color tint",                   "%s", color_tints,  0,  3,  0  },
    { CFG_TYPE_END,             CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

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

static inline RGB alpha(RGB c, RGB x, uint8_t t)
{
    RGB o;
    o.r = mix8(c.r, x.r, t);
    o.g = mix8(c.g, x.g, t);
    o.b = mix8(c.b, x.b, t);
    return o;
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
    register_store(0x4C454453, "LED Strip Settings", cfg_definition);

    //cfg->set_change_hook(CFG_LED_TYPE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_PATTERN,   LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_MODE,      LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_INTENSITY, LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_FIXED_COLOR, LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_FIXED_TINT,  LedStrip :: hot_effectuate);
    cfg->set_change_hook(CFG_LED_SIDSELECT, LedStrip :: hot_effectuate);
    cfg->hide();

    xTaskCreate( LedStrip :: task, "LedStrip Controller", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, NULL );
}

#define LEDSTRIP_DATA ( (volatile uint8_t *)(C64_IO_LED))
#define LEDSTRIP_FROM (LEDSTRIP_DATA[LED_STARTADDR])
#define LEDSTRIP_START (LEDSTRIP_DATA[LED_START])
#define LEDSTRIP_INTENSITY (LEDSTRIP_DATA[LED_INTENSITY])
#define LEDSTRIP_MAP_ENABLE (LEDSTRIP_DATA[LED_MAP])


void LedStrip :: MapDirect(void)
{
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0;i<84*3;i++) {
        LEDSTRIP_DATA[i] = (uint8_t)i;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void LedStrip :: MapSingleColor(void)
{
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0, j=0;i<84;i++) {
        LEDSTRIP_DATA[j++] = 0;
        LEDSTRIP_DATA[j++] = 1;
        LEDSTRIP_DATA[j++] = 2;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void LedStrip :: MapLeftToRight(void)
{
    // Order of the LEDs is:
    // case bottom left to right (30 pcs)
    // case top left to right (30 pcs)
    // kbstrip left to right (24 pcs) (left aligned)
    // So, maybe for true left to right, we could do case bottom, case top, strip, then next etc for the first 24
    // then the last 6 steps only case bottom and case top.
    // In other words: 0, 30, 60, 1, 31, 61, 2, 32, 62, ..., 23, 53, 83, 24, 54, 25, 55, ..., 29, 59
    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    uint8_t pos = 0;
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0;i<24;i++) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;

        LEDSTRIP_DATA[3*i+90] = pos++;
        LEDSTRIP_DATA[3*i+91] = pos++;
        LEDSTRIP_DATA[3*i+92] = pos++;

        LEDSTRIP_DATA[3*i+180] = pos++;
        LEDSTRIP_DATA[3*i+181] = pos++;
        LEDSTRIP_DATA[3*i+182] = pos++;
    }

    for(int i=25;i<30;i++) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;

        LEDSTRIP_DATA[3*i+90] = pos++;
        LEDSTRIP_DATA[3*i+91] = pos++;
        LEDSTRIP_DATA[3*i+92] = pos++;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void LedStrip :: MapRightToLeft(void)
{
    // Order of the LEDs is:
    // case bottom left to right (30 pcs)
    // case top left to right (30 pcs)
    // kbstrip left to right (24 pcs) (left aligned)
    // So, maybe for true left to right, we could do case bottom, case top, strip, then next etc for the first 24
    // then the last 6 steps only case bottom and case top.
    // In other words: 0, 30, 60, 1, 31, 61, 2, 32, 62, ..., 23, 53, 83, 24, 54, 25, 55, ..., 29, 59
    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    uint8_t pos = 0;
    LEDSTRIP_MAP_ENABLE = 1;

    for(int i=29;i>=24;i--) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;

        LEDSTRIP_DATA[3*i+90] = pos++;
        LEDSTRIP_DATA[3*i+91] = pos++;
        LEDSTRIP_DATA[3*i+92] = pos++;
    }

    for(int i=23;i>=0;i--) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;

        LEDSTRIP_DATA[3*i+90] = pos++;
        LEDSTRIP_DATA[3*i+91] = pos++;
        LEDSTRIP_DATA[3*i+92] = pos++;

        LEDSTRIP_DATA[3*i+180] = pos++;
        LEDSTRIP_DATA[3*i+181] = pos++;
        LEDSTRIP_DATA[3*i+182] = pos++;
    }

    LEDSTRIP_MAP_ENABLE = 0;
}

void LedStrip :: MapSerpentine(void)
{
    // Order of the LEDs is:
    // case bottom left to right (30 pcs)
    // case top left to right (30 pcs)
    // kbstrip left to right (24 pcs) (left aligned)
    // Let's start with the keyboard strip from left to right, then go down into the case top strip right to left and then
    // the bottom strip left to right again. So 60-83, 59 downto 30 and then 0-29
    // Remember: the led controller reads the address where to find the colors, so these indices should be written in
    // on the addresses, sequenced in the list above.
    uint8_t pos = 0;
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=60;i<=83;i++) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;
    }
    for(int i=59;i>=30;i--) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;
    }
    for(int i=0;i<=29;i++) {
        LEDSTRIP_DATA[3*i+0] = pos++;
        LEDSTRIP_DATA[3*i+1] = pos++;
        LEDSTRIP_DATA[3*i+2] = pos++;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}

void LedStrip :: MapFromCenter(void)
{
    // Order of the LEDs is:
    // case bottom left to right (30 pcs)
    // case top left to right (30 pcs)
    // kbstrip left to right (24 pcs) (left aligned)
    // Strip: 60, 61, 62, 63, ... 83
    // Top:   30, 31, 32, 33, ... 53, 54, 55, 56, 57, 58, 59
    // Bottom: 0,  1,  2,  3, ... 23, 24, 25, 26, 27, 28, 29

    // From center, we start from LEDs 14/15, LEDs 44/45, and LEDs 74/75
    // then 13 and 16, and so on..
    const uint8_t leds[84] = {
        14, 44, 74, 75, 45, 15,
        13, 43, 73, 76, 46, 16,
        12, 42, 72, 77, 47, 17,
        11, 41, 71, 78, 48, 18,
        10, 40, 70, 79, 49, 19,
         9, 39, 69, 80, 50, 20,
         8, 38, 68, 81, 51, 21,
         7, 37, 67, 82, 52, 22,
         6, 36, 66, 83, 53, 23,
         5, 35, 65,     54, 24,        
         4, 34, 64,     55, 25,        
         3, 33, 63,     56, 26,        
         2, 32, 62,     57, 27,        
         1, 31, 61,     58, 28,        
         0, 30, 60,     59, 29,        
    };

    uint8_t pos = 0;
    LEDSTRIP_MAP_ENABLE = 1;
    for(int i=0;i<=83;i++) {
        LEDSTRIP_DATA[3*leds[i]+0] = pos++;
        LEDSTRIP_DATA[3*leds[i]+1] = pos++;
        LEDSTRIP_DATA[3*leds[i]+2] = pos++;
    }
    LEDSTRIP_MAP_ENABLE = 0;
}


void LedStrip :: task(void *a)
{
    LedStrip *strip = (LedStrip *)a;
    strip->run();
}

void LedStrip :: run(void)
{
    int rainbow_hue = 0;
    int rnd, lfsr = 64738;
    uint8_t spp = 0x00, spr = 0, spg = 0, spb = 0;
    uint8_t v1, v2, v3;

    RGB fixed;
    static uint8_t backup[252];

    setup_config_menu();
    play_boot_pattern();
    effectuate_settings();
    update_menu();

    while(1) {
        v1 = C64_VOICE_ADSR(sidsel * 4 + 0);
        v2 = C64_VOICE_ADSR(sidsel * 4 + 1);
        v3 = C64_VOICE_ADSR(sidsel * 4 + 2);

        switch (mode) {
        case 0: // Off
            offset = 0;
            LEDSTRIP_INTENSITY = 0;
            LEDSTRIP_DATA[0] = 0;
            LEDSTRIP_DATA[1] = 0;
            LEDSTRIP_DATA[2] = 0;
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_START = 0; // and go!
            vTaskDelay(200);
            U64_LEDSTRIP_EN = 0;
            break;
        case 1: // Fixed Color
            offset = 0;
            U64_LEDSTRIP_EN = 1;
            if (hue == 24) {
                fixed = { 255, 255, 255 };
            } else {
                fixed = hue_index_to_rgb(hue, 24);
            }
            fixed = tint_with_white(fixed, tint_factors[tint]);
            fixed = apply_intensity(fixed, intensity);
            LEDSTRIP_INTENSITY = intensity << 2;
            if (protocol) {
                LEDSTRIP_DATA[0] = fixed.g;
                LEDSTRIP_DATA[1] = fixed.r;
                LEDSTRIP_DATA[2] = fixed.b;
            } else {
                LEDSTRIP_DATA[0] = fixed.b;
                LEDSTRIP_DATA[1] = fixed.g;
                LEDSTRIP_DATA[2] = fixed.r;
            }
            LEDSTRIP_FROM = 0x00;
            LEDSTRIP_START = 0; // and go!
            vTaskDelay(50);
            break;
        case 2: // SID Scroll 1 // shift new data in.
            // So first data appears at address 0, offset 0
            // then new data is written to LED 83, and offset is set to 83.
            // then new data is written to LED 82, and offset it set to 82.
            // etc
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = intensity << 2;
            LEDSTRIP_DATA[offset+0] = v1;
            LEDSTRIP_DATA[offset+1] = v2;
            LEDSTRIP_DATA[offset+2] = v3;
            LEDSTRIP_FROM = offset;
            if (offset == 0) {
                offset = 3*83;
            } else {
                offset -= 3;
            }
            LEDSTRIP_START = 0; // and go!
            vTaskDelay(3);
            break;
        case 3: // rainbow
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = intensity << 2;
            rainbow_hue++;
            if (rainbow_hue >= 768)
                rainbow_hue = 0;
            fixed = hue_index_to_rgb(rainbow_hue, 768);
            fixed = tint_with_white(fixed, tint_factors[tint]);
            if (soft_start) {
                fixed = tint_with_white(fixed, soft_start);
                soft_start -= 3; // should end with 0
            }
            if (protocol) {
                LEDSTRIP_DATA[offset+0] = fixed.g;
                LEDSTRIP_DATA[offset+1] = fixed.r;
                LEDSTRIP_DATA[offset+2] = fixed.b;
            } else {
                LEDSTRIP_DATA[offset+0] = fixed.b;
                LEDSTRIP_DATA[offset+1] = fixed.g;
                LEDSTRIP_DATA[offset+2] = fixed.r;
            }
            LEDSTRIP_FROM = offset;
            if (offset == 0) {
                offset = 3*83;
            } else {
                offset -= 3;
            }
            LEDSTRIP_START = 0; // and go!
            vTaskDelay(16);
            break;

        case 4: // rainbow with sparkle
            U64_LEDSTRIP_EN = 1;
            LEDSTRIP_INTENSITY = 0x7F; // do it with the data itself
            rainbow_hue++;
            if (rainbow_hue >= 768)
                rainbow_hue = 0;

            fixed = hue_index_to_rgb(rainbow_hue, 768);
            fixed = tint_with_white(fixed, tint_factors[tint]);
            fixed = apply_intensity(fixed, intensity);
            if (protocol) {
                backup[offset+0] = fixed.g;
                backup[offset+1] = fixed.r;
                backup[offset+2] = fixed.b;

                LEDSTRIP_DATA[offset+0] = fixed.g;
                LEDSTRIP_DATA[offset+1] = fixed.r;
                LEDSTRIP_DATA[offset+2] = fixed.b;
            } else {
                backup[offset+0] = fixed.b;
                backup[offset+1] = fixed.g;
                backup[offset+2] = fixed.r;

                LEDSTRIP_DATA[offset+0] = fixed.b;
                LEDSTRIP_DATA[offset+1] = fixed.g;
                LEDSTRIP_DATA[offset+2] = fixed.r;
            }
            LEDSTRIP_FROM = offset;
            if (offset == 0) {
                offset = 3*83;
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
            rnd = lfsr & 0x1FFF;
            if (rnd < 84) {
                spr = rnd;
                LEDSTRIP_DATA[rnd*3+0] = 0xFF;
                LEDSTRIP_DATA[rnd*3+1] = 0xFF;
                LEDSTRIP_DATA[rnd*3+2] = 0xFF;

            }
            LEDSTRIP_START = 0; // and go!
            vTaskDelay(3);
            break;

        default:
            mode = 0;
        }
    }
}

void LedStrip :: effectuate_settings(void)
{
    mode      = cfg->get_value(CFG_LED_MODE);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    hue       = cfg->get_value(CFG_LED_FIXED_COLOR);
    tint      = cfg->get_value(CFG_LED_FIXED_TINT);
    sidsel    = cfg->get_value(CFG_LED_SIDSELECT);
    // protocol  = cfg->get_value(CFG_LED_TYPE) ? 0x80 : 0x00; // 0x80 = WS2812, 0x00 = APA102;
    protocol  = 1;
    pattern   = cfg->get_value(CFG_LED_PATTERN);

    switch(mode)
    {
        case 0:
        case 1:
            MapSingleColor();
            break;
        default:
            switch(pattern) {
                case 0: // default
                    MapSingleColor();
                    break;
                case 1: // left to right
                    MapLeftToRight();
                    break;
                case 2: // right to left
                    MapRightToLeft();
                    break;
                case 3: // serpentine
                    MapSerpentine();
                    break;
                case 4: // from center outward
                    MapFromCenter();
                    break;
                default:
                    MapDirect();
                    break;
            }
            break;
    }
    // if (protocol) {
    //     LEDSTRIP_MAP_ENABLE = 0x80; // WS
    // } else {
    //     LEDSTRIP_MAP_ENABLE = 0x40; // APA
    // }
//    U64_PWM_DUTY = 0xC0;
}

void LedStrip :: update_menu()
{
    switch(mode) {
    case 0:
        cfg->find_item(CFG_LED_PATTERN)->setEnabled(false);
        cfg->find_item(CFG_LED_INTENSITY)->setEnabled(false);
        cfg->find_item(CFG_LED_FIXED_COLOR)->setEnabled(false);
        cfg->find_item(CFG_LED_FIXED_TINT)->setEnabled(false);
        break;
    case 1: // fixed color
        cfg->find_item(CFG_LED_PATTERN)->setEnabled(false);
        cfg->find_item(CFG_LED_INTENSITY)->setEnabled(true);
        cfg->find_item(CFG_LED_FIXED_COLOR)->setEnabled(true);
        cfg->find_item(CFG_LED_FIXED_TINT)->setEnabled(true);
        break;
    case 2: // music
        cfg->find_item(CFG_LED_PATTERN)->setEnabled(true);
        cfg->find_item(CFG_LED_INTENSITY)->setEnabled(true);
        cfg->find_item(CFG_LED_FIXED_COLOR)->setEnabled(false);
        cfg->find_item(CFG_LED_FIXED_TINT)->setEnabled(false);
        break;
    default:
        cfg->find_item(CFG_LED_PATTERN)->setEnabled(true);
        cfg->find_item(CFG_LED_INTENSITY)->setEnabled(true);
        cfg->find_item(CFG_LED_FIXED_COLOR)->setEnabled(false);
        cfg->find_item(CFG_LED_FIXED_TINT)->setEnabled(true);
    }
}

int LedStrip :: hot_effectuate(ConfigItem *item)
{
    item->store->set_need_effectuate();
    item->store->effectuate();
    LedStrip *ls = (LedStrip *)item->store->get_first_object();
    ls->update_menu();
    
    return 1;
}

#define BLING_RX_FLAGS (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 2))
void LedStrip :: setup_config_menu(void)
{
    ConfigGroup *grp = ConfigGroupCollection :: getGroup(GROUP_NAME_LEDS, SORT_ORDER_CFG_LEDS);
    if (!BLINGBOARD_INSTALLED) { // No Bling Board
        grp->append(ConfigItem::heading("LED Strip (if installed)"));
    } else {
        grp->append(ConfigItem::heading("Case Lights"));
    }
    grp->append(cfg->find_item(CFG_LED_MODE)->set_item_altname("Mode"));
    grp->append(cfg->find_item(CFG_LED_PATTERN)->set_item_altname("Pattern"));
    grp->append(cfg->find_item(CFG_LED_INTENSITY)->set_item_altname("Intensity"));
    grp->append(cfg->find_item(CFG_LED_FIXED_COLOR)->set_item_altname("Color"));
    grp->append(cfg->find_item(CFG_LED_FIXED_TINT)->set_item_altname("Tint"));
    grp->append(ConfigItem::separator());
}

void LedStrip :: ShiftInColor(RGB &fixed)
{
    LEDSTRIP_DATA[offset+0] = fixed.g;
    LEDSTRIP_DATA[offset+1] = fixed.r;
    LEDSTRIP_DATA[offset+2] = fixed.b;
    LEDSTRIP_FROM = offset;
    if (offset <= 0) {
        offset = 3*83;
    } else {
        offset -= 3;
    }
}


void LedStrip :: play_boot_pattern(void)
{
    // start with the right values
    mode      = cfg->get_value(CFG_LED_MODE);
    pattern   = cfg->get_value(CFG_LED_PATTERN);
    intensity = cfg->get_value(CFG_LED_INTENSITY);
    hue       = cfg->get_value(CFG_LED_FIXED_COLOR);
    tint      = cfg->get_value(CFG_LED_FIXED_TINT);
    sidsel    = cfg->get_value(CFG_LED_SIDSELECT);

    MapFromCenter();
    ClearColors();
    LEDSTRIP_MAP_ENABLE = 0;
    LEDSTRIP_INTENSITY = intensity << 2;

    // start with yellow and cycle through the hue values
    int rainbow_hue = 112;
    RGB fixed;

    // Soft start
    for(int i=0; i < 16; i++) {
        rainbow_hue+=1;
        if (rainbow_hue >= 768)
            rainbow_hue -= 768;
        fixed = hue_index_to_rgb(rainbow_hue, 768);
        fixed = alpha(fixed, {0,0,0}, 255-i*16);
        ShiftInColor(fixed);
        LEDSTRIP_START = 0;
        vTaskDelay(5);
    }

    for(int i=0; i < 320; i++) {
        rainbow_hue+=2;
        if (rainbow_hue >= 768)
            rainbow_hue -= 768;
        fixed = hue_index_to_rgb(rainbow_hue, 768);
        ShiftInColor(fixed);
        LEDSTRIP_START = 0;
        vTaskDelay(2);
    }

    // we are now on yellow again, let's fade to white
    for(int i=0;i<140;i++) {
        rainbow_hue+=2;
        fixed = hue_index_to_rgb(rainbow_hue, 768);
        int a = (i*7) >> 1;
        fixed = tint_with_white(fixed, a > 255 ? 255 : a);
        ShiftInColor(fixed);
        LEDSTRIP_START = 0;
        vTaskDelay(2);
    }

    RGB target;
    switch(mode) {
    case 0: // off 
    case 2: // sid music
        target = { 0, 0, 0 };
        MapSingleColor();
        break;
    case 1: // fixed
        MapSingleColor();
        if (hue == 24) {
            goto done; // white -> white
        }
        target = hue_index_to_rgb(hue, 24);
        target = tint_with_white(target, tint_factors[tint]);
        break;
    }

    RGB now;
    if (mode < 3) {
        // fade to target
        for(int i=0;i<140;i++) {
            int a = (i*7) >> 1;
            now = alpha(fixed, target, a > 255 ? 255 : a);
            ShiftInColor(now);
            LEDSTRIP_START = 0;
            vTaskDelay(5);
        }
    }
done:
    soft_start = 255;
}

void LedStrip :: ClearColors(void)
{
    LEDSTRIP_MAP_ENABLE = 0;
    for(int i=0;i<84*3;i++) {
        LEDSTRIP_DATA[i] = 0;
    }
}
