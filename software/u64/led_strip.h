/*
 * led_strip.h
 *
 *  Created on: Dec 12, 2018
 *      Author: gideon
 */

#ifndef LED_STRIP_H_
#define LED_STRIP_H_

#include "config.h"

#define CFG_LED_MODE          0x01
#define CFG_LED_RED           0x02
#define CFG_LED_GREEN         0x03
#define CFG_LED_BLUE          0x04
#define CFG_LED_INTENSITY     0x05
#define CFG_LED_SIDSELECT     0x06
#define CFG_LED_FIXED_COLOR   0x07
#define CFG_LED_FIXED_TINT    0x08
#define CFG_LED_LENGTH        0x09
#define CFG_LED_TYPE          0x0A
#define CFG_LED_PATTERN       0x0B

#define LED_MAP       0xFC
#define LED_STARTADDR 0xFD
#define LED_INTENSITY 0xFE
#define LED_START     0xFF

typedef enum {
    e_led_off = 0,
    e_led_fixed,
    e_led_sid,
    e_led_rainbow,
    e_led_rsparkle,
    e_led_sparkle,
    e_led_default,
} led_mode_t;

typedef struct { uint8_t r, g, b; } RGB;

class LedStrip : public ConfigurableObject
{
    volatile led_mode_t mode;
    volatile uint8_t intensity, sidsel, pattern;
    volatile uint8_t hue, tint, offset, soft_start;
    volatile uint8_t length, protocol;
    
    int model;

    static int hot_effectuate(ConfigItem *item);
    static void task(void *);
    void run(void);
    void setup_config_menu(void);
    void update_menu(void);
    void play_boot_pattern(void);
    void boot_pattern_starlight(void);
    void boot_pattern_founders(void);

    void MapDirect(void);
    void MapSingleColor(void);
    void MapLeftToRight(void);
    void MapRightToLeft(void);
    void MapFromCenter(void);
    void MapToCenter(void);
    void MapSerpentine(void);
    void ClearColors(void);
    void ShiftInColor(RGB &color);
public:
    LedStrip();
    void effectuate_settings(void);
};


#endif /* LED_STRIP_H_ */
