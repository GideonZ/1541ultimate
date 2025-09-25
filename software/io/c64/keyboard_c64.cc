#include <stdio.h>
#include "itu.h"
#include "keyboard_c64.h"
#include "c64.h"
#include "keyboard_usb.h"

#ifndef NO_FILE_ACCESS
#include "FreeRTOS.h"
#include "task.h"
#endif

uint8_t wasd_to_joy = 0; // overwritten by U64_config, if it exists

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
    0x33,0x77,0x61,0x34,0x7a,0x73,0x65,0x00, //  3, w, a, 4, z, s, e, empty
    0x35,0x72,0x64,0x36,0x63,0x66,0x74,0x78, //  5, r, d, 6, c, f, t, x
    0x37,0x79,0x67,0x38,0x62,0x68,0x75,0x76, //  7, y, g, 8, b, h, u, v
    0x39,0x69,0x6A,0x30,0x6D,0x6B,0x6F,0x6E, //  9, i, j, 0, m, k, o, n
    0x2B,0x70,0x6C,0x2D,0x2E,0x3A,0x40,0x2C, //  +, p, l, -, ., :, @, ,
    0x5C,0x2A,0x3B,0x13,0x01,0x3D,0x7C,0x2F, //  \, *, ;, Home , (empty), =, |, /
    0x31,0x60,0x00,0x32,0x20,0x00,0x71,0x03, //  1, `, (empty), 2, SP, (empty), q, RunStop
    0x00 };
*/

const uint8_t keymap_shifted[] = {
    KEY_INSERT, 0x8D, KEY_LEFT, KEY_F8, KEY_F2, KEY_F4, KEY_F6, KEY_UP,
    '#', 'W', 'A', '$', 'Z', 'S', 'E', 0x00,
	'%', 'R', 'D', '&', 'C', 'F', 'T', 'X',
	'\'','Y', 'G', '(', 'B', 'H', 'U', 'V',
	')', 'I', 'J', '0', 'M', 'K', 'O', 'N',
	'{', 'P', 'L', '}', '>', '[', '@', '<',
	'_', '*', ']', KEY_CLEAR,0x00, '=', '^', '?',
	'!', '~', 0x00, '\"', KEY_SHIFT_SP, 0x00, 'Q', KEY_BREAK,
    0x00 };

const uint8_t keymap_control[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x17, 0x01, 0x00, 0x1a, 0x13, 0x05, 0x00, //  3, w, a, 4, z, s, e, empty
    0x00, 0x12, 0x04, 0x00, 0x03, 0x06, 0x14, 0x18, //  5, r, d, 6, c, f, t, x
    0x00, 0x19, 0x07, 0x00, 0x02, 0x08, 0x15, 0x16, //  7, y, g, 8, b, h, u, v
    0x00, 0x09, 0x0A, 0x00, 0x0D, 0x0B, 0x0F, 0x0E, //  9, i, j, 0, m, k, o, n
    0x00, 0x10, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, //  +, p, l, -, ., :, @, ,
    0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, //  \, *, ;, Home , (empty), =, |, /
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, //  1, `, (empty), 2, SP, (empty), q, RunStop
	0x00 };

const uint8_t *keymaps[8] = {
		keymap_normal,  // 0
		keymap_shifted, // 1 Shift
		keymap_control, // 2 C=
		keymap_control, // 3 Shift C=
		keymap_control, // 4 Control
		keymap_control, // 5 Shift Control
		keymap_control, // 6 C= control
		keymap_control  // 7 Shift C= Control
};

Keyboard_C64 :: Keyboard_C64(GenericHost *h, volatile uint8_t *row, volatile uint8_t *col, volatile uint8_t *joy)
{
    host = h;
    col_register = col;
    row_register = row;
    joy_register = joy;

    repeat_speed = 4;
    first_delay = 16;
    key_head = 0;
    key_tail = 0;
    mtrx_prev  = 0xFF;
    shift_prev = 0xFF;
    delay_count = first_delay;
}
    
Keyboard_C64 :: ~Keyboard_C64()
{
}

uint8_t Keyboard_C64 :: scan_keyboard(volatile uint8_t *row_reg, volatile uint8_t *col_reg)
{
    const uint8_t *map;

    uint8_t shift_flag = 0;
    uint8_t mtrx = 0x40;
    uint8_t col, row;
    uint8_t mod = 0;

    *col_reg = 0; // select all rows of keyboard
    if (*row_reg != 0xFF) { // process key image
        map = keymap_normal;
        col = 0xFE;
        for(int idx=0,y=0;y<8;y++) {
            *col_reg = 0xFF;
            *col_reg = col;
            *col_reg = col;
            do {
                row = *row_reg;
                *col_reg = col;
            } while(row != *row_reg);
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
    }
    
    map = keymaps[shift_flag & 0x07];
    return map[mtrx];
}
#include "u64.h" // temporary!
void Keyboard_C64 :: scan(void)
{
    const uint8_t *map;

    uint8_t shift_flag = 0;
    uint8_t mtrx = 0x40;
    uint8_t col, row, key = 0xFF;
    uint8_t mod = 0;
    bool joy = false;
    
#define BLING_RX_DATA  (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 0))
#define BLING_RX_GET   (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 1))
#define BLING_RX_FLAGS (*(volatile uint8_t *)(U64II_BLINGBOARD_KEYB + 2))

    if (BLING_RX_FLAGS & UART_RxDataAv) {
        printf("Bling RX: %02x\n", BLING_RX_DATA);
        BLING_RX_GET  = 1;
    }

    if(!host) {
        return;
    }
    // check if we have access to the I/O
    if(!(host->is_accessible()))
        return;

#if U64 == 2
    MATRIX_WASD_TO_JOY = 0;
#endif

    *row_register = 0xFF;
    *row_register = 0xFF;

    // Scan Joystick Port 2 first
    *col_register = 0xFF; // deselect keyboard for pure joystick scan
    *col_register = 0XFF; // delay

    row = *joy_register;
    row = *joy_register;
    if((row & 0x1F) != 0x1F) {
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
                *col_register = col;
                do {
                    row = *row_register;
                    *col_register = col;
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
#if U64 == 2
    MATRIX_WASD_TO_JOY = wasd_to_joy;
#endif
            return;
        }
    }
    
#if U64 == 2
    MATRIX_WASD_TO_JOY = wasd_to_joy;
#endif
    // there was a key pressed (or the joystick is used)
    // determine which map to use
/*
    if(shift_flag) { // we now only have two maps, so put all modifiers to shift
        map = keymap_shifted;
    } else {
        map = keymap_normal;
    }
*/
    map = keymaps[shift_flag & 0x07];
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
    int next_head = (key_head + 1) % KEY_BUFFER_SIZE;
    if(next_head != key_tail) {
        key_buffer[key_head] = key;
        key_head = next_head;
    }
}

int Keyboard_C64 :: getch(void)
{
#ifndef NO_FILE_ACCESS
    static TickType_t previousWake = 0;
    vTaskDelayUntil(&previousWake, 4);
    TickType_t now = xTaskGetTickCount();
    previousWake = now;
    scan();
#else
    scan();
    wait_ms(20);
#endif

    if(key_head == key_tail) {
        return system_usb_keyboard.getch();
    }
    uint8_t key = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;

    return (int)key;
}

void Keyboard_C64 :: push_head(int c)
{
    uint8_t uc = (uint8_t)c;

    // For now, we only support push tail, alas
    int next_head = (key_head + 1) % KEY_BUFFER_SIZE;
    if(next_head != key_tail) {
        key_buffer[key_head] = uc;
        key_head = next_head;
    }
}

void Keyboard_C64 :: wait_free(void)
{
    if(!host) {
        return;
    }

    // check if we have access to the I/O
    if(!(host->is_accessible()))
        return;

    *col_register = 0; // select all rows

    int timeout = 2000;
    while ((*row_register != 0xFF) && (timeout)) {
        timeout--;
        wait_ms(1);
    }
    *col_register = 0xFF;
}

void Keyboard_C64 :: set_delays(int initial, int repeat)
{
    first_delay  = initial;
    repeat_speed = repeat;
}

void Keyboard_C64 :: clear_buffer(void)
{
    key_head = key_tail = 0;
}
