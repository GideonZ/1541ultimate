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

class LedStrip : public ConfigurableObject
{
    volatile uint8_t mode, intensity;
    volatile uint8_t red, green, blue;
    static void task(void *);
    static void hot_effectuate(ConfigItem *item);
public:
    LedStrip();
    void effectuate_settings(void);
};


#endif /* LED_STRIP_H_ */
