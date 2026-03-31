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

static const int USB_KEY_BUFFER_SIZE = 64;
#define USB_DATA_SIZE 8

class GenericHost;

class Keyboard_USB : public Keyboard
{
	volatile uint8_t *matrix;
	bool matrixEnabled;
	uint8_t matrix_state[8];
	uint8_t injected_matrix_state[8];
	uint8_t key_buffer[USB_KEY_BUFFER_SIZE];
	uint8_t injected_buffer[USB_KEY_BUFFER_SIZE];
    uint8_t last_data[USB_DATA_SIZE];
    int  key_head;
    int  key_tail;
    int  injected_head;
    int  injected_tail;
    void putch(uint8_t ch);
    bool PresentInLastData(uint8_t check);
    void usb2matrix(uint8_t *data);

    int  num_keys;
    int  repeat_speed;
    int  first_delay;
    int  delay_count;
    int  injected_matrix_hold;
    void applyMatrixState(void);
    void clearInjectedMatrixState(void);
    void setInjectedMatrixKey(int key);
public:
    Keyboard_USB();
    ~Keyboard_USB();

    // called from USB thread
    void process_data(uint8_t *kbdata);

    // called from the user interface thread
    int  getch(void);
    void push_head(int c);
    void push_head_repeat(int c, int repeat);
    bool has_injected_key(int c) const;
    void remove_injected_key(int c);
    void wait_free(void);
    void clear_buffer(void);

    // attach / detach matrix peripheral to send keystrokes to.
    void setMatrix(volatile uint8_t *matrix);
    void enableMatrix(bool enable);
};

extern Keyboard_USB system_usb_keyboard;

#endif /* KEYBOARD_USB_H_ */
