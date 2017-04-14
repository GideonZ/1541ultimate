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
#include <stdio.h>

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

const uint8_t keymap_usb2matrix[] = {
		0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x1C, 0x14, 0x12,
		0x0E, 0x15, 0x1A, 0x1D, 0x21, 0x22, 0x25, 0x2A,
		0x24, 0x27, 0x26, 0x29, 0x3E, 0x11, 0x0D, 0x16,
		0x1E, 0x1F, 0x09, 0x17, 0x19, 0x0C, 0x38, 0x3B,
		0x08, 0x0B, 0x10, 0x13, 0x18, 0x1B, 0x20, 0x23,
		0x01, 0xFF, 0x00, 0xFF, 0x3C, 0x2B, 0x35, 0xAD,
		0xB2, 0x30, 0x0F, 0x32, 0x98, 0x39, 0x2F, 0x2C,
		0x37, 0xFF, 0x04, 0x84, 0x05, 0x85, 0x06, 0x86,
		0x03, 0x83, 0x81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x3F, 0x80, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0x02,
		0x82, 0x07, 0x87, 0xFF, 0x37, 0x31, 0x2B, 0x28,
		0x01, 0x38, 0x3B, 0x08, 0x0B, 0x10, 0x13, 0x18,
		0x1B, 0x20, 0x23, 0x2C, 0x0F,
};

Keyboard_USB :: Keyboard_USB()
{
	matrix = 0;
	matrixEnabled = false;
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

void Keyboard_USB :: usb2matrix(uint8_t *kd)
{
	if (!matrix) {
		return;
	}
	uint8_t out[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	// USB modifiers are:
	// 0 = left control | 4 = right control
	// 1 = left shift   | 5 = right shift
	// 2 = left alt     | 6 = right alt (altGr)
	// 3 = left windows | 7 = right windows
	uint8_t modi = kd[0];
	const uint8_t modifier_locations[] = { 0x72, 0x17, 0x00, 0x75, 0x72, 0x64, 0x00, 0x75 };
	const uint8_t key_locations[] = { };

	// reset
	if (modi == 0x0F) {
		matrix[8] = 1;
	} else {
		matrix[8] = 0;
	}

	// Handle the modifiers
	for(int i=0;i<8;i++, modi >>= 1) {
		if (modi & 1) {
			uint8_t bit = modifier_locations[i];
			if (bit) {
				printf("M%i ", i);
				out[bit >> 4] |= (1 << (bit & 0x07));
			}
		}
	}
	// Handle the other keys
	uint8_t restore = 0;

	for(int i=2; i<USB_DATA_SIZE; i++) {
		if (!kd[i]) {
			break;
		}
		if (keymap_normal[kd[i]] == KEY_F12) {
			restore = 1;;
		}
		uint8_t n = keymap_usb2matrix[kd[i]];
		if (n != 0xFF) {
			out[(n & 0x38) >> 3] |= (1 << (n & 0x07));
			if (n & 0x80) {
				out[6] |= (1 << 4);
			}
		}
	}

	matrix[9] = restore;

	for(int i=0; i<8; i++) {
		printf("%b ", out[i]);
	} printf("\n");

	// copy temporary to hardware
	for(int i=0; i<8; i++) {
		matrix[i] = out[i];
	}
}

// called from USB thread
void Keyboard_USB :: process_data(uint8_t *kbdata)
{
	if(matrixEnabled) {
		usb2matrix(kbdata);
	}

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

void Keyboard_USB :: setMatrix(volatile uint8_t *matrix)
{
	if (this->matrix) {
		for (int i=0; i<8; i++) {
			matrix[i] = 0x00;
		}
	}

	this->matrix = matrix;

	if (this->matrix) {
		for (int i=0; i<8; i++) {
			matrix[i] = 0x00;
		}
	}
}
