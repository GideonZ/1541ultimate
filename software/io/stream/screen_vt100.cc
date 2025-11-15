/*
 * screen_vt100.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#include "screen_vt100.h"

Screen_VT100::Screen_VT100(Stream *m) {

	stream = m;
	draw_mode = false;
	expect_color = false;
	stream->write("\377\376\042\377\373\001", 6);
	stream->write("\ec", 2);
	set_color(15);
    move_cursor(0, 0);
}

Screen_VT100::~Screen_VT100() {

}

void Screen_VT100::set_color(int c)
{
	color = c;

	const char *set_color[] = { "\e[0;30m", "\e[0;37;1m", "\e[0;31m", "\e[0;36m", "\e[0;35m", "\e[0;32m", "\e[0;34m", "\e[0;33m",
								"\e[0;33;2m", "\e[0;31;2m", "\e[0;31;1m", "\e[0;31;2m", "\e[0;31;2m", "\e[0;32;1m", "\e[0;34;1m", "\e[0;37;2m" };

	if (c == 'R') {
		reverse_mode(1);
		return;
	} else if (c == 'r') {
		reverse_mode(0);
		return;
	}
	const char *sequence = set_color[c & 15];
	stream->write(sequence, strlen(sequence));
}

void Screen_VT100::reverse_mode(int r)
{
	if (r)
		stream->write("\e[7m", 4);
	else
		stream->write("\e[27m", 5);
}

void Screen_VT100::scroll_mode(bool b)
{
	if (b) {
		stream->write("\e[r", 3);
	}
}

// draw functions
void Screen_VT100::clear()
{
	stream->write("\ec", 2);
	set_color(color);
	move_cursor(0, 0);
}

void Screen_VT100::move_cursor(int x, int y)
{
	stream->format("\e[%d;%dH", y+1, x+1);
}

int  Screen_VT100::output(char c)
{
	//                         123456789abcdef0123456789abcdef
    const char mapping[33] = " lqkxmjlwktsuvmjABo`DEFGHIJKLMNO";

	if (c == 27) { // escape = set color
		expect_color = true;
		return 0;
	}
	if (expect_color) {
		expect_color = false;
		this->set_color(c);
		return 0;
	}
	if (((c >= 32) && (c <= 127)) || (c < 0) || (c == 13) || (c == 10)) { //} || (c == 9)) {
		if (draw_mode) {
			stream->write("\e(B", 3);
			draw_mode = false;
		}
		stream->charout((int)c);
	} else {
		if (!draw_mode) {
			stream->write("\e(0", 3);
			draw_mode = true;
		}
		stream->charout(mapping[c & 0x1F]);
	}
	return 1;
}

int  Screen_VT100::output(const char *c)
{
	int h = 0;
	while(*c) {
		h += output(*(c++));
	}
	return h;
}

void Screen_VT100::repeat(char c, int rep)
{
	for(int i=0;i<rep;i++) {
		output((char)c);
	}
}

void Screen_VT100::output_fixed_length(const char *string, int offset_x, int width)
{
/*
	if (offset_x)
		stream->format("\r\e[%dC", offset_x);
	else
		stream->format("\r");
*/

	//repeat(' ', offset_x);
	while(width > 0) {
		if (*string == 0) {
			break;
		}
		width -= output(*(string++));
	}
	if (width > 0) {
		repeat(' ', width);
	}
}

void Screen_VT100::sync(void)
{
	stream->sync();
}

void Screen_VT100::restore_terminal(void) {
    stream->write("\ec\e[2J", 6);  // RIS (Reset Initial State) + ED2 (Clear entire screen)
    move_cursor(0, 0);
    sync();
}
