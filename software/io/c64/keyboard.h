#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "integer.h"
#include "host.h"

#define KEY_BUFFER_SIZE 16

class GenericHost;

class Keyboard
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
    Keyboard(GenericHost *, volatile BYTE *r, volatile BYTE *c);
    ~Keyboard();
    
    void scan(void);
    void set_delays(int, int);
    BYTE getch(void);
    void wait_free(void);
    void clear_buffer(void);
};

#endif
