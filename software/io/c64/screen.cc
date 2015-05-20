#include "screen.h"
#include <stdarg.h>
#include "small_printf.h"

/*
#include "integer.h"

extern "C" {
    #include "itu.h"
}
*/

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

/**
 * Basic screen implementation, on a memory mapped character grid and a memory mapped attribute grid
 */
Screen :: Screen()
{
	term = 0;
}

Screen :: ~Screen()
{
	if (term)
		delete term;
}

void Screen :: enableVT100Parsing()
{
	term = new VT100_Parser(this);
}


Screen_MemMappedCharMatrix :: Screen_MemMappedCharMatrix(char *b, char *c, int sx, int sy)
{
	char_base  = b;
    color_base = c;

    size_x     = sx;
    size_y     = sy;

    color      = 15;
    reverse    = 0;
    allow_scroll = true;
    cursor_on	 = 0;

    cursor_x = 0;
    cursor_y = 0;
    pointer = 0;
}

void  Screen_MemMappedCharMatrix :: cursor_visible(int a) {
	if (cursor_on != a) {
		char *p = char_base + pointer;
		*p ^= 0x80; // remove or place cursor
	}
	cursor_on = a;
}

void Screen_MemMappedCharMatrix :: move_cursor(int x, int y)
{
	if(cursor_on)
		char_base[pointer] ^= 0x80;

	cursor_x = x;
	cursor_y = y;
	pointer = (y * size_x) + x;

	if(cursor_on)
		char_base[pointer] ^= 0x80;

}

void Screen_MemMappedCharMatrix :: scroll_up()
{
    char *b = char_base;
    char *c = color_base;
    for(int y=0;y<size_y-1;y++) {
        for(int x=0;x<size_x;x++) {
            b[x] = b[x+size_x];
            c[x] = c[x+size_x];
        }
        b+=size_x;
        c+=size_x;
    }
    for(int x=0;x<size_x;x++)
        b[x] = 0;
}

void Screen_MemMappedCharMatrix :: scroll_down()
{
    char *b = &char_base[(size_y-2)*size_x];
    char *c = &color_base[(size_y-2)*size_x];
    for(int y=0;y<size_y-1;y++) {
        for(int x=0;x<size_x;x++) {
            b[x+size_x] = b[x];
            c[x+size_x] = c[x];
        }
        b-=size_x;
        c-=size_x;
    }
    for(int x=0;x<size_x;x++)
        b[x+size_x] = 0;
}

void Screen_MemMappedCharMatrix :: repeat(char a, int len)
{
	if(cursor_on)
		char_base[pointer] ^= 0x80;

	for(int i=0;i<len;i++) {
		char_base[pointer++] = a;
	}

	if(cursor_on)
		char_base[pointer] ^= 0x80;
}

int  Screen_MemMappedCharMatrix :: output(char c)
{
	if (!term) {
		output_raw(c);
		return 1;
	}
	return term ->processChar(c);
}

int  Screen_MemMappedCharMatrix :: output(const char *c) {
	int h = 0;
	while(*c) {
		h += output(*(c++));
	}
	return h;
}

void Screen_MemMappedCharMatrix :: output_raw(char c)
{
	if (cursor_on) {
		char *p = char_base + pointer;
		*p ^= 0x80; // unplace cursor
	}
	switch(c) {
        case 0x0A:
            if(cursor_y == size_y-1) {
                if(allow_scroll)
                    scroll_up();
            } else {
                pointer += size_x;
                cursor_y++;
            }
            pointer -= cursor_x;
            cursor_x = 0;
            break;
        case 0x0D:
            pointer -= cursor_x;
            cursor_x = 0;
            break;
        case 0x08:
            if (pointer > 0) {
            	pointer --;
            	cursor_x --;
            	if (cursor_x < 0) {
            		cursor_x = size_x - 1;
            		cursor_y --;
            	}
            	char_base[pointer] = 0;
            }
            break;
        default:
            if(reverse)
                char_base[pointer] = c | 0x80;
            else
                char_base[pointer] = c;

            color_base[pointer] = (char)color;
            pointer ++;
            cursor_x++;

            if(cursor_x == size_x) {
                cursor_x = 0;
                pointer -= size_x;
                if(cursor_y == size_y-1) {
                    if(allow_scroll)
                        scroll_up();
                } else {
                    pointer += size_x;
                    cursor_y++;
                }
            }
    }
	if (cursor_on) {
		char *p = char_base + pointer;
		*p ^= 0x80; // place cursor
	}
}

void Screen_MemMappedCharMatrix :: output_fixed_length(const char *string, int offset_x, int width)
{
	if (cursor_on) {
		char *p = char_base + pointer;
		*p ^= 0x80; // unplace cursor
	}
	pointer = (cursor_y * size_x) + offset_x;
	cursor_x = offset_x;

	char c;
	int chars_placed = 0;
    for(int i=0;i<width;i++,string++) {
        c = *string;
        if(!c) {
        	repeat(0, width - chars_placed);
			break;
        }

        chars_placed += output(c);
    }
	if (cursor_on) {
		char *p = char_base + pointer;
		*p ^= 0x80; // place cursor
	}
}

/**
 * A simple window overlay, that maintains the offset relative to a screen position, and is capable of
 * drawing a border.
 */
Window :: Window(Screen *parent, int x1, int y1, int sx, int sy)
{
	window_x   = sx;
    window_y   = sy;
    offset_x   = x1;
    offset_y   = y1;
    cursor_x   = 0;
    cursor_y   = 0;
    border_h   = 0;
    border_v   = 0;
    this->parent = parent;
    parent->set_color(15); // default
}

Window :: ~Window()
{
}

void Window :: set_color(int c)
{
    parent->set_color(c);
}

void Window :: no_scroll(void)
{
    parent->scroll_mode(false);
}

void Window :: move_cursor(int x, int y)
{
	if(x >= window_x)
        x = window_x-1;
    if(y >= window_y)
        y = window_y-1;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
        
    cursor_x = x;
    cursor_y = y;

    parent->move_cursor(x + offset_x, y + offset_y);
}

void Window :: clear(void)
{
    for(int y=0;y<window_y;y++) {
    	parent->move_cursor(offset_x, offset_y+y);
    	parent->repeat(' ', window_x);
    }
    cursor_x = cursor_y = 0;
    parent->reverse_mode(0);
    parent->scroll_mode(true);
    parent->move_cursor(offset_x, offset_y);
}


void Window :: reverse_mode(int r)
{
	parent->reverse_mode(r);
}

void Window :: output(char c) {
	parent->output(c);
}

void Window :: output(const char *string)
{
    while(*string) {
        output(*(string++));
    }
}

void Window :: repeat(char a, int len)
{
	parent->repeat(a, len);
}


void Window :: output_line(const char *string)
{
	parent->output_fixed_length(string, offset_x, window_x);
}

void Window :: output_length(const char *string, int len)
{
	parent->output_fixed_length(string, offset_x, len);
}

void Window :: draw_border(void)
{
    parent->move_cursor(offset_x, offset_y);
    parent->output(1);
    parent->repeat(2, window_x-2);
    parent->output(3);
    
    parent->move_cursor(offset_x, offset_y + window_y - 1);
    parent->output(5);
    parent->repeat(2, window_x-2);
    parent->output(6);

    for(int i=1;i<window_y-1;i++) {
    	parent->move_cursor(offset_x, offset_y + i);
    	parent->output(4);
    	parent->move_cursor(offset_x + window_x - 1, offset_y + i);
    	parent->output(4);
    }

    offset_x ++;
    offset_y ++;
    window_x  -= 2;
    window_y  -= 2;
    cursor_x   = 0;
    cursor_y   = 0;
    border_h ++;
    border_v ++;
    move_cursor(0, 0);
}

void Window :: draw_border_horiz(void)
{
    parent->move_cursor(offset_x, offset_y);
    parent->repeat(2, window_x);
    parent->move_cursor(offset_x, offset_y + window_y-1);
    parent->repeat(2, window_x);
    
    offset_y ++;
    window_y -=2;
    cursor_x = cursor_y = 0;
    border_h ++;
}

int Window :: get_size_x(void)
{
    return window_x;
}

int Window :: get_size_y(void)
{
    return window_y;
}

void Window :: set_char(int x, int y, char c)
{
	parent->move_cursor(x + offset_x - border_v, y + offset_y - border_h);
	parent->output(c);
}
    
int console_print(Screen *s, const char *fmt, ...)
{
    va_list ap;
    int ret;
	
    va_start(ap, fmt);
	if(s) {
	    ret = _my_vprintf(Screen :: putc, (void **)s, fmt, ap);
	}
    va_end(ap);
    return (ret);
}
