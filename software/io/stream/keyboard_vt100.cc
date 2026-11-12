/*
 * keyboard_vt100.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#include "keyboard_vt100.h"
#include "itu.h"
#include <stdio.h>


int Keyboard_VT100 :: getch()
{
	const short cursor[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
	const short numeric[] = {
			0, KEY_HOME, KEY_INSERT, KEY_DELETE, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN, 0, 0, 0,
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
		if (charin == 0x7F)
			// A terminal sends DEL for its Backspace key; forward Delete is
			// ESC [ 3 ~, mapped above. As KEY_DELETE this edits ahead of the
			// cursor, and nothing is ahead of it at the end of a field.
			ret = KEY_BACK;
		else if (charin == '\e') {
			escape_state = e_esc_escape;
			escape_started_ms = getMsTimer();
		}
		else if (charin == 0x12)
			// 0x12 is also KEY_DOWN's byte, but unambiguous here: a real down
			// arrow arrives as ESC [ B (e_esc_bracket below), so a bare 0x12 can
			// only be Ctrl+R. All three transports converge on KEY_CTRL_R (0xBA)
			// by the time the monitor sees the key.
			ret = KEY_CTRL_R;
		else  // -1 is also else
			ret = charin;
		break;
	case e_esc_escape:
		charin = stream->get_char();
		if (charin == -1) {
			// Only the elapsed gap can tell a lone ESC from a sequence still
			// arriving: -1 cannot, as a polled UART returns it constantly and
			// SocketStream::get_char() returns it per swallowed Telnet IAC byte.
			if ((uint16_t)(getMsTimer() - escape_started_ms) >= VT100_ESCAPE_ALONE_MS) {
				escape_state = e_esc_idle;
				ret = '\e';
			}
			break;
		}

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
		} else {
			if (charin == '\e') {
				// A second ESC: the first is delivered now, the second gets
				// its own gap.
				escape_started_ms = getMsTimer();
			} else {
				escape_state = e_esc_idle;
				// Not part of any sequence this decoder knows, so it is a key
				// pressed straight after ESC. Hand it back rather than drop it.
				return_unused(charin);
			}
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

// Give back a byte getch() could not use, without displacing the single slot:
// a deliberate push_head() outranks a handed-back byte.
void Keyboard_VT100 :: return_unused(int c)
{
    if (c > 0 && !pending_char)
        pending_char = c;
}

void Keyboard_VT100 :: wait_free(void)
{

}

void Keyboard_VT100 :: clear_buffer(void)
{
	escape_state = e_esc_idle;
	escape_value = 0;
	escape_started_ms = 0;
}
