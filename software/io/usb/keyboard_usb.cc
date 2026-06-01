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

#if U64 && !RECOVERYAPP
#include "timers.h"
#endif
#if U64 == 2
#include "u64.h"
#endif

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#endif

// static system wide available keyboard object

Keyboard_USB system_usb_keyboard;

namespace {

uint8_t usb_keymap_lookup(const uint8_t *map, size_t map_size, uint8_t key)
{
	return (key < map_size) ? map[key] : KEY_ERR;
}

uint8_t usb_matrix_lookup(const uint8_t *map, size_t map_size, uint8_t key)
{
	return (key < map_size) ? map[key] : 0xFF;
}

#if U64 && !RECOVERYAPP
static const uint32_t REST_INPUT_TIMER_TICKS = (pdMS_TO_TICKS(20) > 0) ? pdMS_TO_TICKS(20) : 1;
#endif
static const uint8_t REST_TAP_GAP_TICKS = 2;
static const uint8_t REST_TAP_CHORD_SETUP_TICKS = 1;
static const uint8_t REST_TAP_CHORD_RELEASE_TICKS = 1;

}

#if U64 == 2
extern uint8_t wasd_to_joy __attribute__((weak));
#endif

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
	0x00, KEY_ERR, KEY_ERR, KEY_ERR, 0x01, KEY_CTRL_B, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
	0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, KEY_CTRL_1, KEY_CTRL_2,
	KEY_CTRL_3, KEY_CTRL_4, KEY_CTRL_5, KEY_CTRL_6, KEY_CTRL_7, KEY_CTRL_8, KEY_CTRL_9, KEY_CTRL_0,
    KEY_RETURN, KEY_ESCAPE, KEY_BACK, KEY_TAB, ' ', '-', '=', '[',
    ']', '\\', 0x00, ';', '\'', '`', ',', '.',
    '/', KEY_CAPS, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRSCR, KEY_SCRLOCK,
    KEY_BREAK, KEY_INSERT, KEY_CTRL_HOME, KEY_PAGEUP, KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT,
    KEY_LEFT, KEY_DOWN, KEY_UP, KEY_NUMLOCK, '/', '*', '-', '+',
	KEY_RETURN, KEY_CTRL_1, KEY_CTRL_2, KEY_CTRL_3, KEY_CTRL_4, KEY_CTRL_5, KEY_CTRL_6, KEY_CTRL_7,
	KEY_CTRL_8, KEY_CTRL_9, KEY_CTRL_0, '.', 0x00 };

const uint8_t keymap_usb2matrix[] = {
    0x0F, 0xFF, 0xFF, 0xFF, 0x0A, 0x1C, 0x14, 0x12,
    0x0E, 0x15, 0x1A, 0x1D, 0x21, 0x22, 0x25, 0x2A,
    0x24, 0x27, 0x26, 0x29, 0x3E, 0x11, 0x0D, 0x16,
    0x1E, 0x1F, 0x09, 0x17, 0x19, 0x0C, 0x38, 0x3B,
    0x08, 0x0B, 0x10, 0x13, 0x18, 0x1B, 0x20, 0x23,
    0x01, 0x3F, 0x00, 0xFF, 0x3C, 0x2B, 0x35, 0xAD,
    0xB2, 0x30, 0x0F, 0x32, 0x98, 0x39, 0x2F, 0x2C,
    0x37, 0xFF, 0x04, 0x84, 0x05, 0x85, 0x06, 0x86,
    0x03, 0x83, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x3F, 0x80, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0x02,
    0x82, 0x07, 0x87, 0xFF, 0x37, 0x31, 0x2B, 0x28,
    0x01, 0x38, 0x3B, 0x08, 0x0B, 0x10, 0x13, 0x18,
    0x1B, 0x20, 0x23, 0x2C, 0x0F, };

const uint8_t keymap_usb2matrix_shift[] = {
    0x0F, 0xFF, 0xFF, 0xFF, 0x8A, 0x9C, 0x94, 0x92,
    0x8E, 0x95, 0x9A, 0x9D, 0xA1, 0xA2, 0xA5, 0xAA,
    0xA4, 0xA7, 0xA6, 0xA9, 0xBE, 0x91, 0x8D, 0x96,
    0x9E, 0x9F, 0x89, 0x97, 0x99, 0x8C, 0xB8, 0x2E,
    0x88, 0x8B, 0x90, 0xB6, 0x93, 0x31, 0x9B, 0xA0,
    0x81, 0xBF, 0x80, 0xFF, 0xBC, 0x2B, 0x28, 0xA8,
    0xAB, 0x36, 0x0F, 0x2D, 0xBB, 0xB9, 0xAF, 0xAC,
    0xB7, 0xFF, 0x04, 0x84, 0x05, 0x85, 0x06, 0x86,
    0x03, 0x83, 0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBF, 0x80, 0xB3, 0xFF, 0xFF, 0xFF, 0xFF, 0x02,
    0x82, 0x07, 0x87, 0xFF, 0x37, 0x31, 0x2B, 0x28,
    0x81, 0x38, 0x3B, 0x08, 0x0B, 0x10, 0x13, 0x18,
    0x1B, 0x20, 0x23, 0x2C, 0x0F, };


Keyboard_USB :: Keyboard_USB()
{
	matrix = 0;
	matrixEnabled = false;
	memset(matrix_state, 0, sizeof(matrix_state));
	memset(injected_matrix_state, 0, sizeof(injected_matrix_state));
	memset(rest_matrix_state, 0, sizeof(rest_matrix_state));
	memset(rest_matrix_overlay, 0, sizeof(rest_matrix_overlay));
	memset(rest_overlay_hold, 0, sizeof(rest_overlay_hold));
	rest_matrix_control = false;
	memset(rest_tap_queue, 0, sizeof(rest_tap_queue));
	rest_tap_head = 0;
	rest_tap_tail = 0;
	rest_tap_gap_ticks = 0;
	memset(rest_pending_tap_matrix, 0, sizeof(rest_pending_tap_matrix));
	rest_pending_tap_hold = 0;
	rest_pending_tap_delay = 0;
	usb_restore = 0;
	usb_freeze = 0;
	rest_restore = false;
	rest_restore_overlay = 0;
	rest_restore_hold = 0;
#if U64 && !RECOVERYAPP
	rest_timer = xTimerCreate("RestInput", REST_INPUT_TIMER_TICKS, pdTRUE, this, Keyboard_USB :: S_rest_timer);
	if (rest_timer) {
		xTimerStart(rest_timer, 0);
	}
#endif
	key_head = 0;
	key_tail = 0;
	injected_head = 0;
	injected_tail = 0;
	injected_matrix_hold = 0;

    repeat_speed = 4;
    first_delay = 16;
    delay_count = first_delay;
    num_keys = 0;

	memset(key_buffer, 0, USB_KEY_BUFFER_SIZE);
	memset(injected_buffer, 0, USB_KEY_BUFFER_SIZE);
	memset(last_data, 0, USB_DATA_SIZE);
}

Keyboard_USB :: ~Keyboard_USB()
{
#if U64 && !RECOVERYAPP
	if (rest_timer) {
		xTimerStop(rest_timer, 0);
		xTimerDelete(rest_timer, 0);
		rest_timer = NULL;
	}
#endif
}

void Keyboard_USB :: putch(uint8_t ch)
{
	int next_head = key_head + 1;
	if (next_head == USB_KEY_BUFFER_SIZE)
		next_head = 0;

	if (next_head == key_tail) {
		return;
	}
	key_buffer[key_head] = ch;
	key_head = next_head;
}

void Keyboard_USB :: applyMatrixState(void)
{
	applyRestWasdGuard();
	if (!matrix) {
		return;
	}
	bool rest_control = rest_matrix_control || restMatrixActive();
	for (int i = 0; i < 8; i++) {
		uint8_t rest_state = rest_matrix_state[i] | rest_matrix_overlay[i];
		uint8_t local_state = (matrixEnabled && !rest_control) ? (matrix_state[i] | injected_matrix_state[i]) : 0;
		matrix[i] = local_state | rest_state;
	}
	matrix[9] = (matrixEnabled ? usb_restore : 0) | (rest_restore ? 1 : 0) | (rest_restore_overlay ? 1 : 0);
	matrix[10] = matrixEnabled ? usb_freeze : 0;
}

uint8_t Keyboard_USB :: effectiveRestoreBit(void) const
{
	return usb_restore | (rest_restore ? 1 : 0) | (rest_restore_overlay ? 1 : 0);
}

bool Keyboard_USB :: restMatrixActive(void) const
{
	if (rest_restore || rest_restore_overlay) {
		return true;
	}
	for (int i = 0; i < 8; i++) {
		if (rest_matrix_state[i] || rest_matrix_overlay[i]) {
			return true;
		}
	}
	return false;
}

void Keyboard_USB :: applyRestWasdGuard(void) const
{
#if U64 == 2
	MATRIX_WASD_TO_JOY = (rest_matrix_control || restMatrixActive()) ? 0 : (&wasd_to_joy ? wasd_to_joy : 0);
#endif
}

void Keyboard_USB :: clearInjectedMatrixState(void)
{
	if (injected_matrix_hold == 0) {
		return;
	}
	injected_matrix_hold = 0;
	memset(injected_matrix_state, 0, sizeof(injected_matrix_state));
	applyMatrixState();
}

bool Keyboard_USB :: restTapQueueEmpty(void) const
{
	return rest_tap_head == rest_tap_tail;
}

bool Keyboard_USB :: restTapOverlayActive(void) const
{
	if (rest_pending_tap_hold != 0) {
		return true;
	}
	if (rest_restore_hold != 0) {
		return true;
	}
	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			if (rest_overlay_hold[row][col] != 0) {
				return true;
			}
		}
	}
	return false;
}

void Keyboard_USB :: startRestTap(const RestTapEntry& entry)
{
	uint8_t first_matrix[8];
	uint8_t deferred_matrix[8];
	bool has_setup = false;
	bool has_deferred = false;

	for (int row = 0; row < 8; row++) {
		first_matrix[row] = entry.matrix[row];
		deferred_matrix[row] = 0;
		if (entry.setup_matrix[row]) {
			has_setup = true;
		}
	}
	if (has_setup) {
		for (int row = 0; row < 8; row++) {
			first_matrix[row] = entry.setup_matrix[row] & entry.matrix[row];
			deferred_matrix[row] = entry.matrix[row] & ~first_matrix[row];
			if (deferred_matrix[row]) {
				has_deferred = true;
			}
		}
	}

	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			uint8_t bit = (1 << col);
			if (first_matrix[row] & bit) {
				if ((rest_matrix_state[row] & bit) == 0) {
					rest_matrix_overlay[row] |= bit;
				}
				rest_overlay_hold[row][col] = entry.hold_ticks +
					(has_deferred ? (REST_TAP_CHORD_SETUP_TICKS + REST_TAP_CHORD_RELEASE_TICKS) : 0);
			}
		}
	}
	if (has_deferred) {
		memcpy(rest_pending_tap_matrix, deferred_matrix, sizeof(rest_pending_tap_matrix));
		rest_pending_tap_hold = entry.hold_ticks;
		rest_pending_tap_delay = REST_TAP_CHORD_SETUP_TICKS;
	}
	if (entry.restore) {
		rest_restore_overlay = 1;
		rest_restore_hold = entry.hold_ticks;
	}
}

void Keyboard_USB :: setInjectedMatrixKey(int key)
{
	memset(injected_matrix_state, 0, sizeof(injected_matrix_state));
	switch (key) {
	case KEY_RIGHT:
		injected_matrix_state[0] = (1 << 2);
		break;
	case KEY_LEFT:
		injected_matrix_state[0] = (1 << 2);
		injected_matrix_state[6] = (1 << 4);
		break;
	case KEY_DOWN:
		injected_matrix_state[0] = (1 << 7);
		break;
	case KEY_UP:
		injected_matrix_state[0] = (1 << 7);
		injected_matrix_state[6] = (1 << 4);
		break;
	default:
		injected_matrix_hold = 0;
		return;
	}
	injected_matrix_hold = 1;
	applyMatrixState();
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
	const uint8_t modi = kd[0];
    // const uint8_t modifier_locations[] = { 0x72, 0x17, 0x00, 0x75, 0x72, 0x64, 0x00, 0x75 };
	// without shift
	const uint8_t modifier_locations[] = { 0x72, 0x00, 0x00, 0x75, 0x72, 0x00, 0x00, 0x75 };
	const uint8_t key_locations[] = { };

	// reset
	if (modi == 0x0F) {
		matrix[8] = 1;
	} else {
		matrix[8] = 0;
	}

	// Handle the modifiers
	for(int i=0;i<8;i++) {
		if (modi & (1 << i)) {
			uint8_t bit = modifier_locations[i];
			if (bit) {
				// printf("M%i ", i);
				out[bit >> 4] |= (1 << (bit & 0x07));
			}
		}
	}
	// Handle the other keys
	uint8_t restore = 0;
    uint8_t freeze = 0;
	uint8_t something_else_pressed = 0;
	for(int i=2; i<USB_DATA_SIZE; i++) {
		if (!kd[i]) {
			break;
		}
		uint8_t normal = usb_keymap_lookup(keymap_normal, sizeof(keymap_normal), kd[i]);
		if (normal == KEY_F12) {
			restore = 1;
		}
		if (normal == KEY_F11) {
		    freeze = 1;
		}
		uint8_t n = (kd[0] & 0x22) ? usb_matrix_lookup(keymap_usb2matrix_shift, sizeof(keymap_usb2matrix_shift), kd[i]) :
		                            usb_matrix_lookup(keymap_usb2matrix, sizeof(keymap_usb2matrix), kd[i]);
		//printf("[%b]", n);
		if (n != 0xFF) {
		    something_else_pressed = 1;
		    out[(n & 0x38) >> 3] |= (1 << (n & 0x07));
			if (n & 0x80) {
				out[6] |= (1 << 4);
			}
		}
	}

	usb_restore = restore;
	usb_freeze = freeze;

	if (!something_else_pressed) {
	    if (modi & 0x02) { // left shift
	        out[1] |= 0x80;
	    }
        if (modi & 0x20) { // right shift
            out[6] |= 0x10;
        }
	}

	// copy temporary to hardware
	for(int i=0; i<8; i++) {
		matrix_state[i] = out[i];
	}
	applyMatrixState();
}

#if U64 && !RECOVERYAPP
void Keyboard_USB :: S_rest_timer(TimerHandle_t a)
{
	Keyboard_USB *keyboard = (Keyboard_USB *)pvTimerGetTimerID(a);
	if (keyboard) {
		keyboard->tickRestOverlays();
	}
}
#endif

// called from USB thread
void Keyboard_USB :: process_data(uint8_t *kbdata)
{
	if(matrixEnabled) {
		usb2matrix(kbdata);
	}

	num_keys = USB_DATA_SIZE - 2;
	for(int i=2; i<USB_DATA_SIZE; i++) {
		if (!kbdata[i]) {
		    num_keys = i-2;
		    break;
		}
		if (!PresentInLastData(kbdata[i])) {
			if (kbdata[0] & 0x11) { // control
				uint8_t mapped = usb_keymap_lookup(keymap_control, sizeof(keymap_control), kbdata[i]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
			} else if (kbdata[0] & 0x22) { // shift
				uint8_t mapped = usb_keymap_lookup(keymap_shifted, sizeof(keymap_shifted), kbdata[i]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
			} else {
				uint8_t mapped = usb_keymap_lookup(keymap_normal, sizeof(keymap_normal), kbdata[i]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
			}
		}
	}

	memcpy(last_data, kbdata, USB_DATA_SIZE);
    delay_count = first_delay;
}

// called from the user interface thread
int  Keyboard_USB :: getch(void)
{
    if (injected_matrix_hold > 0) {
		injected_matrix_hold--;
		if (injected_matrix_hold == 0) {
			memset(injected_matrix_state, 0, sizeof(injected_matrix_state));
			applyMatrixState();
		}
    }
    if (num_keys == 1) { // implement repeat for one key pressed (other than the modifiers)
        if (delay_count == 0) {
            delay_count = repeat_speed;

            if (last_data[0] & 0x11) { // control
				uint8_t mapped = usb_keymap_lookup(keymap_control, sizeof(keymap_control), last_data[2]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
            } else if (last_data[0] & 0x22) { // shift
				uint8_t mapped = usb_keymap_lookup(keymap_shifted, sizeof(keymap_shifted), last_data[2]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
            } else {
				uint8_t mapped = usb_keymap_lookup(keymap_normal, sizeof(keymap_normal), last_data[2]);
				if ((mapped != 0) && (mapped != KEY_ERR)) {
					putch(mapped);
				}
            }
        } else {
            delay_count --;
        }
    }
    int injected_key = -1;
    portENTER_CRITICAL();
    if (injected_head != injected_tail) {
		injected_key = injected_buffer[injected_tail];
		injected_tail ++;
		if (injected_tail == USB_KEY_BUFFER_SIZE) {
			injected_tail = 0;
		}
    }
    portEXIT_CRITICAL();
    if (injected_key >= 0) {
		if (matrixEnabled) {
			setInjectedMatrixKey(injected_key);
		}
		return injected_key;
    }
    if (key_head != key_tail) {
		uint8_t key = key_buffer[key_tail];
		key_tail ++;
		if (key_tail == USB_KEY_BUFFER_SIZE) {
			key_tail = 0;
		}
		return key;
	}
	return -1;
}

void Keyboard_USB :: push_head(int c)
{
	push_head_repeat(c, 1);
}

void Keyboard_USB :: push_head_repeat(int c, int repeat)
{
	if ((c < 0) || (c > 0xFF)) {
		return;
	}
	portENTER_CRITICAL();
	while (repeat-- > 0) {
		int next_head = injected_head + 1;
		if (next_head == USB_KEY_BUFFER_SIZE) {
			next_head = 0;
		}
		if (next_head == injected_tail) {
			break;
		}
		injected_buffer[injected_head] = (uint8_t)c;
		injected_head = next_head;
	}
	portEXIT_CRITICAL();
}

bool Keyboard_USB :: has_injected_key(int c) const
{
	return count_injected_key(c) > 0;
}

int Keyboard_USB :: count_injected_key(int c) const
{
	if ((c < 0) || (c > 0xFF)) {
		return 0;
	}
	int count = 0;
	portENTER_CRITICAL();
	for (int index = injected_tail; index != injected_head; ) {
		if (injected_buffer[index] == (uint8_t)c) {
			count++;
		}
		index++;
		if (index == USB_KEY_BUFFER_SIZE) {
			index = 0;
		}
	}
	portEXIT_CRITICAL();
	return count;
}

void Keyboard_USB :: remove_injected_key(int c)
{
	if ((c < 0) || (c > 0xFF)) {
		return;
	}
	if (injected_head == injected_tail) {
		return;
	}

	uint8_t filtered[USB_KEY_BUFFER_SIZE];
	int write_index = 0;
	portENTER_CRITICAL();
	for (int index = injected_tail; index != injected_head; ) {
		uint8_t key = injected_buffer[index];
		if (key != (uint8_t)c) {
			filtered[write_index++] = key;
		}
		index++;
		if (index == USB_KEY_BUFFER_SIZE) {
			index = 0;
		}
	}

	injected_tail = 0;
	injected_head = 0;
	for (int i = 0; i < write_index; i++) {
		injected_buffer[injected_head++] = filtered[i];
	}
	portEXIT_CRITICAL();
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
	portENTER_CRITICAL();
	injected_tail = injected_head;
	portEXIT_CRITICAL();
	clearInjectedMatrixState();
}

void Keyboard_USB :: restPress(uint8_t row, uint8_t col_bit)
{
	if ((row >= 8) || (col_bit >= 8)) {
		return;
	}
	portENTER_CRITICAL();
	rest_matrix_state[row] |= (1 << col_bit);
	rest_matrix_overlay[row] &= ~(1 << col_bit);
	rest_overlay_hold[row][col_bit] = 0;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restRelease(uint8_t row, uint8_t col_bit)
{
	if ((row >= 8) || (col_bit >= 8)) {
		return;
	}
	portENTER_CRITICAL();
	rest_matrix_state[row] &= ~(1 << col_bit);
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restPressMatrix(const uint8_t press_matrix[8])
{
	portENTER_CRITICAL();
	for (int row = 0; row < 8; row++) {
		uint8_t bits = press_matrix[row];
		rest_matrix_state[row] |= bits;
		rest_matrix_overlay[row] &= ~bits;
		for (int col = 0; col < 8; col++) {
			if (bits & (1 << col)) {
				rest_overlay_hold[row][col] = 0;
			}
		}
	}
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restReleaseMatrix(const uint8_t release_matrix[8])
{
	portENTER_CRITICAL();
	for (int row = 0; row < 8; row++) {
		rest_matrix_state[row] &= ~release_matrix[row];
	}
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restBeginMatrixControl(void)
{
	portENTER_CRITICAL();
	rest_matrix_control = true;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restEndMatrixControl(void)
{
	portENTER_CRITICAL();
	rest_matrix_control = false;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restTap(uint8_t row, uint8_t col_bit, uint8_t hold_ticks)
{
	if ((row >= 8) || (col_bit >= 8) || (hold_ticks == 0)) {
		return;
	}
	portENTER_CRITICAL();
	if ((rest_matrix_state[row] & (1 << col_bit)) == 0) {
		rest_matrix_overlay[row] |= (1 << col_bit);
	}
	rest_overlay_hold[row][col_bit] = hold_ticks;
	applyMatrixState();
	portEXIT_CRITICAL();
}

bool Keyboard_USB :: restQueueTap(const uint8_t matrix[8], bool restore, uint8_t hold_ticks)
{
	uint8_t setup_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	return restQueueTap(matrix, setup_matrix, restore, hold_ticks);
}

bool Keyboard_USB :: restQueueTap(const uint8_t matrix[8], const uint8_t setup_matrix[8], bool restore, uint8_t hold_ticks)
{
	if (hold_ticks == 0) {
		return true;
	}
	portENTER_CRITICAL();

	bool has_any_key = restore;
	for (int row = 0; row < 8; row++) {
		if (matrix[row] != 0) {
			has_any_key = true;
			break;
		}
	}
	if (!has_any_key) {
		portEXIT_CRITICAL();
		return true;
	}

	uint8_t next_head = rest_tap_head + 1;
	if (next_head >= REST_TAP_QUEUE_SIZE) {
		next_head = 0;
	}
	if (next_head == rest_tap_tail) {
		portEXIT_CRITICAL();
		return false;
	}

	memcpy(rest_tap_queue[rest_tap_head].matrix, matrix, sizeof(rest_tap_queue[rest_tap_head].matrix));
	memcpy(rest_tap_queue[rest_tap_head].setup_matrix, setup_matrix, sizeof(rest_tap_queue[rest_tap_head].setup_matrix));
	rest_tap_queue[rest_tap_head].hold_ticks = hold_ticks;
	rest_tap_queue[rest_tap_head].restore = restore;
	rest_tap_head = next_head;
	portEXIT_CRITICAL();
	return true;
}

void Keyboard_USB :: restPressRestore(void)
{
	portENTER_CRITICAL();
	rest_restore = true;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restReleaseRestore(void)
{
	portENTER_CRITICAL();
	rest_restore = false;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restTapRestore(uint8_t hold_ticks)
{
	if (hold_ticks == 0) {
		return;
	}
	portENTER_CRITICAL();
	rest_restore_overlay = 1;
	rest_restore_hold = hold_ticks;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restReleaseAll(void)
{
	portENTER_CRITICAL();
	memset(rest_matrix_state, 0, sizeof(rest_matrix_state));
	memset(rest_matrix_overlay, 0, sizeof(rest_matrix_overlay));
	memset(rest_overlay_hold, 0, sizeof(rest_overlay_hold));
	rest_tap_head = 0;
	rest_tap_tail = 0;
	rest_tap_gap_ticks = 0;
	memset(rest_pending_tap_matrix, 0, sizeof(rest_pending_tap_matrix));
	rest_pending_tap_hold = 0;
	rest_pending_tap_delay = 0;
	rest_matrix_control = false;
	rest_restore = false;
	rest_restore_overlay = 0;
	rest_restore_hold = 0;
	applyMatrixState();
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restSnapshot(uint8_t out_matrix[8], bool &out_restore) const
{
	portENTER_CRITICAL();
	for (int i = 0; i < 8; i++) {
		out_matrix[i] = rest_matrix_state[i] | rest_matrix_overlay[i];
	}
	out_restore = rest_restore || (rest_restore_overlay != 0);
	portEXIT_CRITICAL();
}

void Keyboard_USB :: restPersistentSnapshot(uint8_t out_matrix[8], bool &out_restore) const
{
	portENTER_CRITICAL();
	for (int i = 0; i < 8; i++) {
		out_matrix[i] = rest_matrix_state[i] & ~rest_matrix_overlay[i];
	}
	out_restore = rest_restore;
	portEXIT_CRITICAL();
}

void Keyboard_USB :: tickRestOverlays(void)
{
	portENTER_CRITICAL();
	bool changed = false;
	for (int row = 0; row < 8; row++) {
		for (int col = 0; col < 8; col++) {
			if (rest_overlay_hold[row][col] == 0) {
				continue;
			}
			rest_overlay_hold[row][col]--;
			if (rest_overlay_hold[row][col] == 0) {
				uint8_t bit = (1 << col);
				if (rest_matrix_overlay[row] & bit) {
					rest_matrix_overlay[row] &= ~bit;
				}
				rest_tap_gap_ticks = REST_TAP_GAP_TICKS;
				changed = true;
			}
		}
	}
	if (rest_restore_hold > 0) {
		rest_restore_hold--;
		if (rest_restore_hold == 0) {
			rest_restore_overlay = 0;
			rest_tap_gap_ticks = REST_TAP_GAP_TICKS;
			changed = true;
		}
	}
	if (rest_pending_tap_hold != 0) {
		if (rest_pending_tap_delay > 0) {
			rest_pending_tap_delay--;
		}
		if (rest_pending_tap_delay == 0) {
			for (int row = 0; row < 8; row++) {
				for (int col = 0; col < 8; col++) {
					uint8_t bit = (1 << col);
					if (rest_pending_tap_matrix[row] & bit) {
						if ((rest_matrix_state[row] & bit) == 0) {
							rest_matrix_overlay[row] |= bit;
						}
						rest_overlay_hold[row][col] = rest_pending_tap_hold;
					}
				}
			}
			memset(rest_pending_tap_matrix, 0, sizeof(rest_pending_tap_matrix));
			rest_pending_tap_hold = 0;
			changed = true;
		}
	}
	if (!restTapOverlayActive()) {
		if (rest_tap_gap_ticks > 0) {
			rest_tap_gap_ticks--;
		} else if (!restTapQueueEmpty()) {
			startRestTap(rest_tap_queue[rest_tap_tail]);
			rest_tap_tail++;
			if (rest_tap_tail >= REST_TAP_QUEUE_SIZE) {
				rest_tap_tail = 0;
			}
			changed = true;
		}
	}
	if (changed) {
		applyMatrixState();
	}
	portEXIT_CRITICAL();
}

void Keyboard_USB :: setMatrix(volatile uint8_t *matrix)
{
	if (this->matrix) {
		for (int i=0; i<8; i++) {
			matrix[i] = 0x00;
		}
	}

	this->matrix = matrix;
	memset(matrix_state, 0, sizeof(matrix_state));
	memset(injected_matrix_state, 0, sizeof(injected_matrix_state));
	injected_matrix_hold = 0;

	if (this->matrix) {
		for (int i=0; i<8; i++) {
			matrix[i] = 0x00;
		}
	}
	applyMatrixState();
}

void Keyboard_USB :: enableMatrix(bool enable)
{
	matrixEnabled = enable;
	if (!enable) {
		clearInjectedMatrixState();
	}
	applyMatrixState();
}
