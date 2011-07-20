extern "C" {
    #include "itu.h"
    #include "small_printf.h"
}

#include "keyboard.h"
#include "c64.h"


#define NO_IRQ 1

const BYTE modifier_map[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0x00,0x00,0x04,0x00,0x00,0x02,0x00,0x00,
    0x00 }; 
    
const BYTE keymap_normal[] = {
    0x14,0x0D,0x1D,0x88,0x85,0x86,0x87,0x11,
    0x33,0x77,0x61,0x34,0x7A,0x73,0x65,0x00,
    0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78,
    0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76,
    0x39,0x69,0x6A,0x30,0x6D,0x6B,0x6F,0x6E,
    0x2B,0x70,0x6C,0x2D,0x2E,0x3A,0x40,0x2C,
    0x5C,0x2A,0x3B,0x13,0x01,0x3D,0x7C,0x2F,
    0x31,0x60,0x00,0x32,0x20,0x00,0x71,0x03,
    0x00 };

const BYTE keymap_shifted[] = {
    0x94,0x8D,0x9D,0x8C,0x89,0x8A,0x8B,0x91,
    0x23,0x57,0x41,0x24,0x5A,0x53,0x45,0x00,
    0x25,0x52,0x44,0x26,0x43,0x46,0x54,0x58,
    0x27,0x59,0x47,0x28,0x42,0x48,0x55,0x56,
    0x29,0x49,0x4A,0x30,0x4D,0x4B,0x4F,0x4E,
    0x7B,0x50,0x4C,0x7D,0x3E,0x5B,0xBA,0x3C,
    0x5F,0x60,0x5D,0x93,0x00,0x3D,0x5E,0x3F,
    0x21,0x7E,0x00,0x22,0xA0,0x00,0x51,0x83,
    0x00 };

Keyboard :: Keyboard(GenericHost *h, volatile BYTE *row, volatile BYTE *col)
{
    host = h;
    col_register = col;
    row_register = row;
    
    repeat_speed = 4;
    first_delay = 16;
    key_count = 0;

    mtrx_prev  = 0xFF;
    shift_prev = 0xFF;
    delay_count = first_delay;
}
    
Keyboard :: ~Keyboard()
{
}

void Keyboard :: scan(void)
{
    const BYTE *map;

    BYTE shift_flag = 0;
    BYTE mtrx = 0x40;
    BYTE col, row, key = 0xFF;
    BYTE mod = 0;
    bool joy = false;
    
    // check if we have access to the I/O
    if(!(host->is_accessible()))
        return;

    // Scan Joystick Port 2 first
    *col_register = 0xFF; // deselect keyboard for pure joystick scan
    *col_register = 0XFF; // delay
    row = *col_register; // port2
    if(row != 0xFF) {
        joy = true;
        if     (!(row & 0x01)) { shift_flag = 0x01; mtrx = 0x07; }
        else if(!(row & 0x02)) { shift_flag = 0x00; mtrx = 0x07; }
        else if(!(row & 0x04)) { shift_flag = 0x01; mtrx = 0x02; }
        else if(!(row & 0x08)) { shift_flag = 0x00; mtrx = 0x02; }
        else if(!(row & 0x10)) { shift_flag = 0x00; mtrx = 0x01; }
    }
    
    // If the joystick was not used, we can safely scan the keyboard
    if(!joy) {
        *col_register = 0; // select all rows of keyboard
        if (*row_register != 0xFF) { // process key image
            map = keymap_normal;
            col = 0xFE;
            for(int idx=0,y=0;y<8;y++) {
                *col_register = 0xFF;
                *col_register = col;
                do {
                    row = *row_register;
                } while(row != *row_register);
                for(int x=0;x<8;x++, idx++) {
                    if((row & 1)==0) {
                        mod = modifier_map[idx];
                        shift_flag |= mod;
                        if(!mod) {
                            mtrx = idx;
                        }
                    }
                    row >>= 1;
                }
                col = (col << 1) | 1;
            }
        } else { // no key pressed
            mtrx_prev = 0xFF;
            shift_prev = 0xFF;
            return;
        }
    }
    
    // there was a key pressed (or the joystick is used)
    // determine which map to use
    if(shift_flag) { // we now only have two maps, so put all modifiers to shift
        map = keymap_shifted;
    } else {
        map = keymap_normal;
    }
    key = map[mtrx];

    if(!key) { // no sensible key pressed, clear history
        mtrx_prev = 0xFF;
        shift_prev = 0xFF;
        return;
    }
            
    if((shift_flag == shift_prev) && (mtrx_prev == mtrx)) { // this key was pressed before
        if (delay_count == 0) {
            delay_count = repeat_speed;
            // do not return, store char
        } else {
            delay_count = delay_count - 1;
            return;
        }
    } else if(mtrx == mtrx_prev) { // same key, but modifier changed.. ignore! (for some time)
        delay_count = first_delay;
        shift_prev = shift_flag;
        return;
    } else {  // first time this key was pressed
        delay_count = first_delay;
        mtrx_prev = mtrx;
        shift_prev = shift_flag;        
    }

//    printf("%b ", key);
    if(key_count != KEY_BUFFER_SIZE) {
        key_buffer[key_count++] = key;
    }
}

BYTE Keyboard :: getch(void)
{
#ifdef NO_IRQ
    scan();
    wait_ms(20);
#endif

    if(!key_count) {
        return 0;
    }
    ENTER_SAFE_SECTION
    BYTE key = key_buffer[0];
    for(int i=0;i<key_count-1;i++) {
        key_buffer[i] = key_buffer[i+1];
    }
    key_count--;
    LEAVE_SAFE_SECTION

    return key;    
}

void Keyboard :: wait_free(void)
{
    // check if we have access to the I/O
    if(!(host->is_accessible()))
        return;

    *col_register = 0; // select all rows
    while(*row_register != 0xFF)
        ;
    *col_register = 0xFF;
}

void Keyboard :: set_delays(int initial, int repeat)
{
    first_delay  = initial;
    repeat_speed = repeat;
}

void Keyboard :: clear_buffer(void)
{
    key_count = 0;
}
