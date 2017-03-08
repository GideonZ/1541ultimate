/*
 * keyboard_usb.h
 *
 *  Created on: Mar 5, 2017
 *      Author: gideon
 */

#ifndef KEYBOARD_USB_H_
#define KEYBOARD_USB_H_

#include "keyboard.h"
#include "integer.h"

#define KEY_BUFFER_SIZE 16
#define USB_DATA_SIZE 8

class GenericHost;

class Keyboard_USB : public Keyboard
{
    uint8_t key_buffer[KEY_BUFFER_SIZE];
    uint8_t last_data[USB_DATA_SIZE];
    int  key_head;
    int  key_tail;
    void putch(uint8_t ch);
    bool PresentInLastData(uint8_t check);
public:
    Keyboard_USB();
    ~Keyboard_USB();

    // called from USB thread
    void process_data(uint8_t *kbdata);

    // called from the user interface thread
    int  getch(void);
    void wait_free(void);
    void clear_buffer(void);
};

extern Keyboard_USB system_usb_keyboard;

#endif /* KEYBOARD_USB_H_ */
