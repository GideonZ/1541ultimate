/*
 * keyboard_vt100.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#include "keyboard_vt100.h"
#include <stdio.h>

#define ENTER_SAFE_SECTION // FIXME
#define LEAVE_SAFE_SECTION

int Keyboard_VT100 :: parseEscapes()
{
	const short cursor[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
	const short numeric[] = {
			0, KEY_HOME, KEY_INSERT, 0, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN, 0, 0, 0,
			0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, 0, KEY_F6, KEY_F7, KEY_F8,
			KEY_F9, KEY_F10, 0, KEY_F11, KEY_F12, 0, 0, 0, 0, 0, 0 };

	if (keyb_offset < 3) { // not enough information
		return -1;
	}
	char c = keyb_buffer[2];
	if ((c >= '0') && (c <= '9')) { // search for ~ terminator
		if (keyb_offset < 4) { // { not enough information
			return -1;
		}
		char c2 = keyb_buffer[3];
		int value = c - '0';
		if (c2 == '~') {
		    for(int i=0;i<keyb_offset-4;i++) {
		        keyb_buffer[i] = keyb_buffer[i+4];
		    }
			keyb_offset -= 4;
			return numeric[value];
		}
		if (keyb_offset < 5) {
			return -1;
		}
		char c3 = keyb_buffer[4];
		if (c3 != '~') {
			return 0; // wrong!
		}
		value = ((c - '0') * 10) + (c2 - '0');
		if (value >= 30) {
			return 0; // wrong;
		}
		value = numeric[value];
		if (value) {
		    for(int i=0;i<keyb_offset-5;i++) {
		        keyb_buffer[i] = keyb_buffer[i+5];
		    }
			keyb_offset -= 5; // eat it
		} else {
			printf("Key value %d unknown.\n", value);
		}
		return value;
	} else if((c >= 'A') && (c <= 'D')) {
	    for(int i=0;i<keyb_offset-3;i++) {
	        keyb_buffer[i] = keyb_buffer[i+3];
	    }
		keyb_offset -= 3;
		return cursor[c-'A'];
	}
	return 0;
}


int  Keyboard_VT100 :: getch(void)
{
	// suck characters from the stream
	if (keyb_offset < KEYB_BUFFER_SIZE) {
		int k = stream->get_char();
		if (k >= 0) {
			keyb_buffer[keyb_offset ++] = (char)k;
		}
	}
	// do we have anything to process?
	if(!keyb_offset) {
        return -1;
    }
    ENTER_SAFE_SECTION
	if (keyb_buffer[0] == '\e') {
		if (keyb_offset < 2) { // there is only an escape char in the buffer. We cannot handle it yet.
			LEAVE_SAFE_SECTION
			return -1;
		}
		int ch ;
		if (keyb_buffer[1] == '[') {
			ch = parseEscapes();
			if (ch != 0) { // -1 = not yet, 0 = no match
				LEAVE_SAFE_SECTION
				if (ch > 0) {
					printf("%02x\n", ch);
				}
				return ch;
			}
		}
	}
	char key = keyb_buffer[0];
    for(int i=0;i<keyb_offset-1;i++) {
        keyb_buffer[i] = keyb_buffer[i+1];
    }
    keyb_offset--;
    LEAVE_SAFE_SECTION

	if (key > 0) {
		printf("%02x\n", key);
	}

    return (int)key;
}

void Keyboard_VT100 :: wait_free(void)
{

}

void Keyboard_VT100 :: clear_buffer(void)
{
	keyb_offset = 0;
}
