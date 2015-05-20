/*
 * screen_vt100.h
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */

#ifndef IO_STREAM_SCREEN_VT100_H_
#define IO_STREAM_SCREEN_VT100_H_

#include "screen.h"
#include "stream.h"

class Screen_VT100: public Screen {
	Stream *stream;
	int color;
	bool draw_mode;
public:
	Screen_VT100(Stream *s);
	virtual ~Screen_VT100();

    // functions called directly, or from a window
    int  get_color() { return color; }
    int   get_size_x(void) { return 40; }
    int   get_size_y(void) { return 25; }
    void  cursor_visible(int a) { }
    void set_color(int c);
    void reverse_mode(int r);
    void scroll_mode(bool b);

    // draw functions
    void clear();
    void move_cursor(int x, int y);
    int  output(char c);
    int  output(const char *c);
    void repeat(char c, int rep);
    void output_fixed_length(const char *string, int offset_x, int width);
};

#endif /* IO_STREAM_SCREEN_VT100_H_ */
