/*
 * vt100_parser.cc
 *
 *  Created on: May 24, 2015
 *      Author: Gideon
 */

#include <vt100_parser.h>
#include <stdio.h>

const short bright_map[16] = { 0, 1, 10, 3, 10, 13, 14, 7, 7, 8, 10, 15, 15, 13, 14,  1 };
const short dim_map[16]    = { 0, 15, 2, 3,  4,  5,  6, 8, 9, 9,  2, 11, 11,  5,  6, 15 };

void VT100_Parser :: doAttr(int val)
{
	switch(val) {
	case 0:
		owner->set_color(15);
		owner->reverse_mode(0);
		break;
	case 1:
		owner->set_color(bright_map[owner->get_color() & 15]);
		break;
	case 2:
		owner->set_color(dim_map[owner->get_color() & 15]);
		break;
	case 27:
		owner->reverse_mode(0);
		break;
	case 7:
		owner->reverse_mode(1);
		break;
	case 30: owner->set_color(0); break;
	case 31: owner->set_color(2); break;
	case 32: owner->set_color(5); break;
	case 33: owner->set_color(7); break;
	case 34: owner->set_color(6); break;
	case 35: owner->set_color(4); break;
	case 36: owner->set_color(3); break;
	case 37: owner->set_color(1); break;
	default:
		printf("Unhandled VT100 attribute %d\n", val);
		break;
	}
}

void VT100_Parser :: parseAttr()
{
	int value = 0;
	for(int i=0;i<escape_cmd_pos;i++) {
		char c = escape_cmd_data[i];
		if ((c >= '0') && (c <= '9')) {
			value *= 10;
			value += (c - '0');
		} else if (c == ';') {
			doAttr(value);
			value = 0;
		}
	}
	doAttr(value);
}

int VT100_Parser :: processChar(char c)
{
	if(escape) {
		if ((!escape_cmd) && (c == '[')) {
			escape_cmd_pos = 0;
			escape_cmd = true;
			return 0;
		}

		if (((c >= 'A')&&(c <= 'Z')) || ((c >= 'a')&&(c <= 'z')) || (escape_cmd_pos == 15)) {
			escape = false;
			escape_cmd = false;
			if (c == 'm') {
				parseAttr();
			}
		} else {
			escape_cmd_data[escape_cmd_pos++] = c;
		}
		return 0;
	} else if(c == 0x1B) {
		escape = true;
		return 0;
	}
	owner->output_raw(c);
	return 1;
}
