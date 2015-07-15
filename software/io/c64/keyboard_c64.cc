#include <stdio.h>
#include "itu.h"
#include "keyboard_c64.h"
#include "c64.h"
#include "FreeRTOS.h"
#include "task.h"

const uint8_t modifier_map[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01, // left shift
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00, // right shift
    0x00,0x00,0x04,0x00,0x00,0x02,0x00,0x00, // control, C=
    0x00 }; 
    
const uint8_t keymap_normal[] = {
    KEY_BACK, KEY_RETURN, KEY_RIGHT, KEY_F7, KEY_F1, KEY_F3, KEY_F5, KEY_DOWN,
    '3', 'w', 'a', '4', 'z', 's', 'e', 0x00,
	'5', 'r', 'd', '6', 'c', 'f', 't', 'x',
	'7', 'y', 'g', '8', 'b', 'h', 'u', 'v',
	'9', 'i', 'j', '0', 'm', 'k', 'o', 'n',
	'+', 'p', 'l', '-', '.', ':', '@', ',',
	'\\','*', ';', KEY_HOME, 0x00, '=', '|', '/',
	'1', '`', 0x00, '2', ' ', 0x00, 'q', KEY_BREAK,
	0x00 };

/*
    0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78, //  5, r, d, 6, c, f, t, x
    0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76, //  7, y, g, 8, b, h, u, v
    0x39,0x69,0x6A,0x30,0x6D,0x6B,0x6F,0x6E, //  9, i, j, 0, m, k, o, n
    0x2B,0x70,0x6C,0x2D,0x2E,0x3A,0x40,0x2C, //  +, p, l, -, ., :, @, ,
    0x5C,0x2A,0x3B,0x13,0x01,0x3D,0x7C,0x2F, //  \, *, ;, Home , (empty), =, |, /
    0x31,0x60,0x00,0x32,0x20,0x00,0x71,0x03, //  1, `, (empty), 2, SP, (empty), q, RunStop
    0x00 };
*/

const uint8_t keymap_shifted[] = {
    KEY_INSERT,0x8D,0x9D,0x8C,0x89,0x8A,0x8B,0x91,
    0x23,0x57,0x41,0x24,0x5A,0x53,0x45,0x00,
    0x25,0x52,0x44,0x26,0x43,0x46,0x54,0x58,
    0x27,0x59,0x47,0x28,0x42,0x48,0x55,0x56,
    0x29,0x49,0x4A,0x30,0x4D,0x4B,0x4F,0x4E,
    0x7B,0x50,0x4C,0x7D,0x3E,0x5B,0xBA,0x3C,
    0x5F,0x60,0x5D,0x93,0x00,0x3D,0x5E,0x3F,
    0x21,0x7E,0x00,0x22,0xA0,0x00,0x51,0x83,
    0x00 };

Keyboard_C64 :: Keyboard_C64(GenericHost *h, volatile uint8_t *row, volatile uint8_t *col)
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
    
Keyboard_C64 :: ~Keyboard_C64()
{
}

void Keyboard_C64 :: scan(void)
{
    const uint8_t *map;

    uint8_t shift_flag = 0;
    uint8_t mtrx = 0x40;
    uint8_t col, row, key = 0xFF;
    uint8_t mod = 0;
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

int Keyboard_C64 :: getch(void)
{
    static TickType_t previousWake = 0;
    vTaskDelayUntil(&previousWake, 4);

    scan();

    if(!key_count) {
        return -1;
    }
    ENTER_SAFE_SECTION
    uint8_t key = key_buffer[0];
    for(int i=0;i<key_count-1;i++) {
        key_buffer[i] = key_buffer[i+1];
    }
    key_count--;
    LEAVE_SAFE_SECTION

    return (int)key;
}

void Keyboard_C64 :: wait_free(void)
{
    // check if we have access to the I/O
    if(!(host->is_accessible()))
        return;

    *col_register = 0; // select all rows
    while(*row_register != 0xFF)
        ;
    *col_register = 0xFF;
}

void Keyboard_C64 :: set_delays(int initial, int repeat)
{
    first_delay  = initial;
    repeat_speed = repeat;
}

void Keyboard_C64 :: clear_buffer(void)
{
    key_count = 0;
}
