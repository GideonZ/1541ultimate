/*
 * bling_board.h
 *
 *  Created on: Jul 26, 2025
 *      Author: gideon
 *
 * This is basically a copy of led_strip.h/.cc, but with some different mappings and functions.
 */

#ifndef BLING_BOARD_H_
#define BLING_BOARD_H_

#include "config.h"
#include "FreeRTOS.h"
#include "queue.h"

#define CFG_LED_MODE          0x01
#define CFG_LED_RED           0x02
#define CFG_LED_GREEN         0x03
#define CFG_LED_BLUE          0x04
#define CFG_LED_INTENSITY     0x05
#define CFG_LED_SIDSELECT     0x06
#define CFG_LED_FIXED_COLOR   0x07
#define CFG_LED_FIXED_TINT    0x08

#define CFG_LED_PATTERN       0x0B

#define LED_MAP       0xFC
#define LED_STARTADDR 0xFD
#define LED_INTENSITY 0xFE
#define LED_START     0xFF

class BlingBoard : public ConfigurableObject
{
    volatile uint8_t mode, intensity, sidsel, pattern;
    volatile uint8_t hue, tint, offset, soft_start;
    volatile int speed;
    QueueHandle_t key_queue;

    static uint8_t irq_handler(void *context);
    static void task(void *);
    void run(void);
    static int hot_effectuate(ConfigItem *item);
    void setup_config_menu(void);
    void play_boot_pattern(void);

    void MapDirect(void);
    void MapSingleColor(void);
    void MapLeftToRight(void);
    void MapRightToLeft(void);
    void MapFromCenter(void);
    void MapToCenter(void);
    void MapCircular(void);
    void ClearColors(void);
public:
    BlingBoard();
    void effectuate_settings(void);
};

#endif /* BLING_BOARD_H_ */
