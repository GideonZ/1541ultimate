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
		else if (charin == 0x12)
			// Ctrl+R, the monitor's reset shortcut, as one keystroke.
			//
			// 0x12 is also the value of KEY_DOWN, and passing it through
			// unchanged is what made Ctrl+R move the cursor down on a Telnet
			// session. The two are distinguishable here even though they are
			// not further up: a terminal's down arrow arrives as ESC [ B and
			// is decoded by the e_esc_bracket case below, so the only thing
			// that produces a bare 0x12 is somebody actually holding Ctrl and
			// pressing R. Mapping it to KEY_CTRL_R therefore costs the cursor
			// nothing and gives this transport the same single keystroke the
			// USB keyboard has.
			//
			// Flow control cannot reach this either. The bytes a terminal
			// sends unprompted are DC1 at 0x11 and DC3 at 0x13, XON and XOFF.
			// DC2 at 0x12 is not used that way.
			//
			// All three transports converge on KEY_CTRL_R, 0xBA, by the time
			// the monitor sees the key: Telnet by this translation, the C64
			// and USB keyboards by allocation in their keymaps. The keymaps
			// cannot simply use 0x12, because there it would alias KEY_DOWN
			// for every screen in the user interface.
			ret = KEY_CTRL_R;
		else  // -1 is also else
			ret = charin;
		break;
	case e_esc_escape:
		charin = stream->get_char();
		if (charin == -1) {
			// Nothing followed the ESC within the stream's read timeout, so
			// the ESC was the key itself rather than the start of a sequence.
			// SocketStream::get_char() recv()s on a socket whose SO_RCVTIMEO
			// is 200ms, and a terminal writes a whole sequence in one go, so a
			// real sequence never reaches here between its own bytes.
			escape_state = e_esc_idle;
			ret = '\e';
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
			if (charin != '\e') {
				escape_state = e_esc_idle;
				// Hand it back rather than drop it. This is a keystroke a
				// person pressed, and swallowing it is how one key in a
				// session goes missing: ESC followed by anything used to
				// deliver the ESC and lose whatever came after it.
				if (charin > 0)
					push_head(charin);
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
		} else if (charin > 0) {
			push_head(charin);
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
			} else if (charin > 0) {
				push_head(charin);
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
