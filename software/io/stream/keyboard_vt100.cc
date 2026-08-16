/*
 * keyboard_vt100.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#include "keyboard_vt100.h"
#include <stdio.h>


int Keyboard_VT100 :: getch()
{
	const short cursor[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
	const short numeric[] = {
			0, KEY_HOME, KEY_INSERT, 0, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN, 0, 0, 0,
			0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, 0, KEY_F6, KEY_F7, KEY_F8,
			KEY_F9, KEY_F10, 0, KEY_F11, KEY_F12, 0, 0, 0, 0, 0, 0 };
	const short function[] = { KEY_F1, KEY_F2, KEY_F3, KEY_F4 };

	int ret = -1;
	int charin;

	switch(escape_state) {
	case e_esc_idle:
        if (pending_char) {
            charin = pending_char;
            pending_char = 0;
        } else {
		    charin = stream->get_char();
        }
		if (charin == '\e')
			escape_state = e_esc_escape;
		else  // -1 is also else
			ret = charin;
		break;
	case e_esc_escape:
		charin = stream->get_char();
		if (charin == -1)
			break;

		if (charin == 'O') {
			escape_state = e_esc_o;
		} else if (charin == '[') {
			escape_state = e_esc_bracket;
			escape_value = 0;
		} else if (charin >= '0' && charin <= '9') {
			escape_state = e_esc_idle;
			ret = key_ctrl_digit(charin - '0');
		} else if (charin == 'b' || charin == 'B') {
			escape_state = e_esc_idle;
			ret = KEY_CTRL_B;
		} else if (charin == 0x12) {
			// C=+R, the monitor's reset shortcut. KEY_CTRL_R is 0xBA, and
			// SocketStream::get_char returns (int) of a plain char, so a byte
			// above 0x7F cannot be sent as itself over a Telnet session; an
			// escape sequence is the only way this transport can reach the key.
			//
			// The terminating byte is 0x12, Ctrl+R, and not the letter R as it
			// is for C=+B above. The reset is destructive and has no
			// confirmation on any interface, so it has to take a sequence a
			// user cannot arrive at while meaning something else. Two
			// properties of this driver make the letter form unsafe: an escape
			// that finds no byte yet leaves escape_state at e_esc_escape
			// indefinitely, so a pending ESC waits for whatever is typed next
			// however much later; and ESC is the monitor's Back key while R
			// starts Range, so "back out a layer, then start a range" is a
			// sequence a user types on purpose. No terminal emits a control
			// byte straight after ESC, so this form has no such neighbour.
			escape_state = e_esc_idle;
			ret = KEY_CTRL_R;
		} else {
			if (charin != '\e')
				escape_state = e_esc_idle;
			ret = '\e';
		}
		break;
	case e_esc_o:
		charin = stream->get_char();
		if (charin == -1)
			break;

		escape_state = e_esc_idle;
		if ((charin >= 'P') && (charin <= 'S')) {
			ret = function[charin - 'P'];
		}
		break;
	case e_esc_bracket:
		charin = stream->get_char();
		if (charin == -1)
			break;

		if ((charin >= '0') && (charin <= '9')) {
			escape_value *= 10;
			escape_value += (charin - '0');
		} else {
			//stream->format("escape sequence ended with %c. Value = %d\n", charin, escape_value);
			escape_state = e_esc_idle;
			if (charin == '~') {
				if (escape_value < 30) {
					ret = numeric[escape_value];
				}
			} else if ((charin >= 'A') && (charin <= 'D')) {
				ret = cursor[charin - 'A'];
			}
		}
		break;
	default:
		ret = -2;
	}
	return ret;
}

void Keyboard_VT100 :: push_head(int c)
{
    pending_char = c;
}

void Keyboard_VT100 :: wait_free(void)
{

}

void Keyboard_VT100 :: clear_buffer(void)
{
	escape_state = e_esc_idle;
	escape_value = 0;
}
