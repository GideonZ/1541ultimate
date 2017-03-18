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
		charin = stream->get_char();
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


void Keyboard_VT100 :: wait_free(void)
{

}

void Keyboard_VT100 :: clear_buffer(void)
{
	escape_state = e_esc_idle;
	escape_value = 0;
}
