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

class LedStrip : public ConfigurableObject
{
    volatile uint8_t mode, intensity, sidsel, pattern;
    volatile uint8_t hue, tint;
    volatile uint8_t length, protocol;
    
    static void task(void *);
    static int hot_effectuate(ConfigItem *item);
    void setup_config_menu(void);
    void update_menu(void);
    
    void MapDirect(void);
    void MapSingleColor(void);
    void MapLeftToRight(void);
    void MapRightToLeft(void);
    void MapFromCenter(void);
    void MapToCenter(void);
    void MapSerpentine(void);

public:
    LedStrip();
    void effectuate_settings(void);
};


#endif /* LED_STRIP_H_ */
