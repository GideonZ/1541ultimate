#ifndef KEYBOARD_C64_H
#define KEYBOARD_C64_H

#include "keyboard.h"
#include "host.h"

#define KEY_BUFFER_SIZE 16

class GenericHost;

class Keyboard_C64 : public Keyboard
{
    GenericHost *host;
    volatile BYTE *row_register;
    volatile BYTE *col_register;
    
    BYTE shift_prev;
    BYTE mtrx_prev;
    
    int  repeat_speed;
    int  first_delay;
        
    int  delay_count;

    BYTE key_buffer[KEY_BUFFER_SIZE];
    int  key_count;
public:
    Keyboard_C64(GenericHost *, volatile BYTE *r, volatile BYTE *c);
    ~Keyboard_C64();
    
    void scan(void);
    void set_delays(int, int);
    BYTE getch(void);
    void wait_free(void);
    void clear_buffer(void);
};

#endif
