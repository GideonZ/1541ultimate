/*
 * transform.c
 *
 *  Created on: Mar 25, 2017
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>
#include "keyboard.h"

const uint8_t keymap_c64[] = {
    KEY_BACK, KEY_RETURN, KEY_RIGHT, KEY_F7, KEY_F1, KEY_F3, KEY_F5, KEY_DOWN,
    '3', 'w', 'a', '4', 'z', 's', 'e', 0x00,
	'5', 'r', 'd', '6', 'c', 'f', 't', 'x',
	'7', 'y', 'g', '8', 'b', 'h', 'u', 'v',
	'9', 'i', 'j', '0', 'm', 'k', 'o', 'n',
	'+', 'p', 'l', '-', '.', ':', '@', ',',
	'\\','*', ';', KEY_HOME, 0x00, '=', '|', '/',
	'1', '`', 0x00, '2', ' ', 0x00, 'q', KEY_BREAK,
	0x00 };

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

const uint8_t keymap_usb[] = {
    0x00, KEY_ERR, KEY_ERR, KEY_ERR, 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
    '3', '4', '5', '6', '7', '8', '9', '0',
    KEY_RETURN, KEY_ESCAPE, KEY_BACK, KEY_TAB, ' ', '-', '=', '[',
    ']', '\\', 0x00, ';', '\'', '`', ',', '.',
    '/', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCR, KEY_SCRLOCK,
    KEY_BREAK, KEY_INSERT, KEY_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT,
    KEY_LEFT, KEY_DOWN, KEY_UP, KEY_NUMLOCK, '/', '*', '-', '+',
    KEY_RETURN, '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '.', 0x00 };

// BSS, thus zero
uint8_t mapped[64];

int main(void)
{
	for (int i=0;i<sizeof(keymap_usb);i++) {
		uint8_t usbkey = keymap_usb[i];
		uint8_t c64keypos = 0xFF;
		uint8_t modi = 0;
		for (int j=0;j<sizeof(keymap_c64);j++) {
			if (keymap_c64[j] == usbkey) {
				c64keypos = j;
				break;
			}
		}
		if (c64keypos == 0xFF) {
			for (int j=0;j<sizeof(keymap_shifted);j++) {
				if (keymap_shifted[j] == usbkey) {
					c64keypos = j | 0x80;
					break;
				}
			}
		}
		// printf("USB KEY %02X => %02X (%c) => %02X\n", i, usbkey, ((usbkey >= ' ') && (usbkey < 0x7F))?usbkey:'?', c64keypos);
		printf("0x%02X, ", c64keypos);
		mapped[c64keypos] = 1;
	}
	for(int i=0;i<64;i++) {
		if (mapped[i])
			continue;
		printf("Warning: Key '%c' (%02x) not mapped\n", keymap_c64[i], keymap_c64[i]);
	}
	return 0;
}
