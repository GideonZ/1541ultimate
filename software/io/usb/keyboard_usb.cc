/*
 * keyboard_usb.cc
 *
 *  Created on: Mar 5, 2017
 *      Author: gideon
 */
#include "keyboard_usb.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

// static system wide available keyboard object

Keyboard_USB system_usb_keyboard;

const uint8_t keymap_normal[] = {
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


const uint8_t keymap_shifted[] = {
    0x00, KEY_ERR, KEY_ERR, KEY_ERR, 'A', 'B', 'C', 'D',
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
    '#', '$', '%', '^', '&', '*', '(', ')',
    KEY_RETURN, KEY_ESCAPE, KEY_BACK, KEY_TAB, KEY_SHIFT_SP, '_', '+', '{',
    '}', '|', 0x00, ':', '\"', '~', '<', '>',
    '?', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCR, KEY_SCRLOCK,
    KEY_BREAK, KEY_INSERT, KEY_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT,
    KEY_LEFT, KEY_DOWN, KEY_UP, KEY_NUMLOCK, '/', '*', '-', '+',
    KEY_RETURN, KEY_END, KEY_DOWN, KEY_PAGEDOWN, KEY_LEFT, '5', KEY_RIGHT, KEY_HOME,
    KEY_UP, KEY_PAGEUP, KEY_INSERT, KEY_DELETE, 0x00 };

const uint8_t keymap_control[] = {
    0x00, KEY_ERR, KEY_ERR, KEY_ERR, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, '1', '2',
    '3', '4', '5', '6', '7', '8', '9', '0',
    KEY_RETURN, KEY_ESCAPE, KEY_BACK, KEY_TAB, ' ', '-', '=', '[',
    ']', '\\', 0x00, ';', '\'', '`', ',', '.',
    '/', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCR, KEY_SCRLOCK,
    KEY_BREAK, KEY_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT,
    KEY_LEFT, KEY_DOWN, KEY_UP, KEY_NUMLOCK, '/', '*', '-', '+',
    KEY_RETURN, '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '.', 0x00 };


Keyboard_USB :: Keyboard_USB()
{
	key_head = 0;
	key_tail = 0;
	memset(key_buffer, 0, KEY_BUFFER_SIZE);
	memset(last_data, 0, USB_DATA_SIZE);
}

Keyboard_USB :: ~Keyboard_USB()
{

}

void Keyboard_USB :: putch(uint8_t ch)
{
	int next_head = key_head + 1;
	if (next_head == KEY_BUFFER_SIZE)
		next_head = 0;

	if (next_head == key_tail) {
		return;
	}
	key_buffer[key_head] = ch;
	key_head = next_head;
}

bool Keyboard_USB :: PresentInLastData(uint8_t check)
{
	for(int i=2; i<USB_DATA_SIZE; i++) {
		if (!last_data[i])
			return false;
		if (last_data[i] == check)
			return true;
	}
	return false;
}

// called from USB thread
void Keyboard_USB :: process_data(uint8_t *kbdata)
{
	for(int i=2; i<USB_DATA_SIZE; i++) {
		if (!kbdata[i]) {
			break;
		}
		if (!PresentInLastData(kbdata[i])) {
			if (kbdata[0] & 0x11) { // control
				putch(keymap_control[kbdata[i]]);
			} else if (kbdata[0] & 0x22) { // shift
				putch(keymap_shifted[kbdata[i]]);
			} else {
				putch(keymap_normal[kbdata[i]]);
			}
		}
	}

	memcpy(last_data, kbdata, USB_DATA_SIZE);
}

// called from the user interface thread
int  Keyboard_USB :: getch(void)
{
	if (key_head != key_tail) {
		uint8_t key = key_buffer[key_tail];
		key_tail ++;
		if (key_tail == KEY_BUFFER_SIZE) {
			key_tail = 0;
		}
		return key;
	}
	return -1;
}

void Keyboard_USB :: wait_free(void)
{
	bool free;
	do {
		free = true;
		for (int i=0; i < USB_DATA_SIZE; i++) {
			if (last_data[i] != 0) {
				free = false;
				break;
			}
		}
#ifndef NO_FILE_ACCESS
		if (!free) {
			vTaskDelay(10);
		}
#endif
	} while(!free);
}

void Keyboard_USB :: clear_buffer(void)
{
	key_tail = key_head;
}

