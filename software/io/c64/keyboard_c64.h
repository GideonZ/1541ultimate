#ifndef KEYBOARD_C64_H
#define KEYBOARD_C64_H

#include "keyboard.h"
#include "host.h"

#define KEY_BUFFER_SIZE 16

class GenericHost;

class Keyboard_C64 : public Keyboard
{
    GenericHost *host;
    volatile uint8_t *row_register;
    volatile uint8_t *col_register;
    volatile uint8_t *joy_register;
    
    uint8_t shift_prev;
    uint8_t mtrx_prev;
    
    int  repeat_speed;
    int  first_delay;
        
    int  delay_count;

    int key_buffer[KEY_BUFFER_SIZE];
    int  key_head;
    int  key_tail;
public:
    Keyboard_C64(GenericHost *, volatile uint8_t *r, volatile uint8_t *c, volatile uint8_t *j);
    ~Keyboard_C64();
    
    static uint8_t scan_keyboard(volatile uint8_t *r, volatile uint8_t *c);

    void scan(void);
    void set_delays(int, int);
    int  getch(void);
    void push_head(int);
    void wait_free(void);
    void clear_buffer(void);
};

#endif
