#include "screen.h"
#include <string.h>
#include <stdarg.h>
#include "small_printf.h"

/*
#include "integer.h"

extern "C" {
    #include "itu.h"
}
*/


/**
 * Basic screen implementation, on a memory mapped character grid and a memory mapped attribute grid
 */
Screen_MemMappedCharMatrix :: Screen_MemMappedCharMatrix(char *b, char *c, int sx, int sy)
{
	escape = false;

	char_base  = b;
    color_base = c;

    size_x     = sx;
    size_y     = sy;

    color      = 15;
    background = 0;
    reverse    = 0;
    allow_scroll = true;
    cursor_on	 = 0;

    cursor_x = 0;
    cursor_y = 0;
    pointer = 0;

    backup_chars = 0;
    backup_color = 0;
    backup_x = backup_y = backup_size = 0;
}

void Screen_MemMappedCharMatrix :: update_size(int sx, int sy)
{
    size_x = sx;
    size_y = sy;
}

void Screen_MemMappedCharMatrix :: backup(void)
{
    if (backup_size) {
        restore();
    }
    backup_size = size_x * size_y;
	backup_chars = new char[backup_size];
	backup_color = new char[backup_size];
	backup_x = cursor_x;
	backup_y = cursor_y;
	memcpy(backup_chars, char_base, backup_size);
	memcpy(backup_color, color_base, backup_size);
}

void Screen_MemMappedCharMatrix :: restore(void)
{
    if(!backup_size) {
        return;
    }
	memcpy(char_base, backup_chars, backup_size);
	memcpy(color_base, backup_color, backup_size);
	move_cursor(backup_x, backup_y);
	delete[] backup_chars;
	delete[] backup_color;
    backup_size = 0;
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

	if (x > size_x-1)
		x = size_x-1;
	if (y > size_y-1)
		y = size_y-1;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	cursor_x = x;
	cursor_y = y;
	pointer = (y * size_x) + x;

	if(cursor_on)
		char_base[pointer] ^= 0x80;

}

void Screen_MemMappedCharMatrix :: scroll_up()
{
	if(!allow_scroll) {
		return;
	}
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

	bool cur = cursor_on;
	cursor_on = false;

	for(int i=0;i<len;i++) {
		output_raw(a);
	}
	cursor_on = cur;

	if(cursor_on)
		char_base[pointer] ^= 0x80;
}

int  Screen_MemMappedCharMatrix :: output(char c)
{
	if (escape) {
		if (c == 'R') {
			reverse_mode(1);
		} else if (c == 'r') {
			reverse_mode(0);
		} else if (c == 'B') {
		    set_background(c & 15); // funny, this will always result in a red background ;)
		} else {
			set_color(c & 15);
		}
		escape = false;
		return 0;
	}
	if (c == 27) {
		escape = true;
		return 0;
	}
	output_raw(c);
	return 1;
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
                char_base[pointer] = c & 0x7F;

            color_base[pointer] = (char)(color | background << 4);
            pointer ++;
            cursor_x++;

            if(cursor_x >= size_x) {
                pointer -= cursor_x;
                cursor_x = 0;
                if(cursor_y == size_y-1) {
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
	bool crsr = cursor_on;
	cursor_on = false;

	char c;
	int chars_placed = 0;
    while(chars_placed < width) {
        c = *(string++);
        if(!c) {
        	repeat(32, width - chars_placed);
			break;
        }

        chars_placed += output(c);
    }

    cursor_on = crsr;
    if (cursor_on) {
		char *p = char_base + pointer;
		*p ^= 0x80; // place cursor
	}
}

void Screen_MemMappedCharMatrix :: clear() {
	reverse_mode(0);
	set_color(15);
	int size = get_size_x() * get_size_y();
	memset(char_base, 32, size);
	memset(color_base, 15, size);
    move_cursor(0, 0);
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
    old_color = parent->get_color();
    parent->set_background(0); // default
    parent->set_color(15); // default
    parent->reverse_mode(0);
}

Window :: ~Window()
{
	parent->set_color(old_color);
	parent->reverse_mode(0);
}

void Window :: set_color(int c)
{
    parent->set_color(c);
}

void Window :: set_background(int c)
{
    parent->set_background(c);
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
    parent->reverse_mode(0);
    for(int y=0;y<window_y;y++) {
    	parent->move_cursor(offset_x, offset_y+y);
    	parent->repeat(' ', window_x);
    }
    cursor_x = cursor_y = 0;
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


void Window :: output_line(const char *string, int indent)
{
    parent->repeat(' ', indent);
	parent->output_fixed_length(string, offset_x, window_x-indent);
}

void Window :: output_length(const char *string, int len)
{
	parent->output_fixed_length(string, offset_x, len);
}

void Window :: draw_border(void)
{
    parent->move_cursor(offset_x, offset_y);
    parent->set_color(1);
    parent->output(BORD_LOWER_RIGHT_CORNER);
    parent->set_color(15);
    parent->repeat(CHR_HORIZONTAL_LINE, window_x-2);
    parent->output(BORD_LOWER_LEFT_CORNER);
    
    parent->move_cursor(offset_x, offset_y + window_y - 1);
    parent->output(BORD_UPPER_RIGHT_CORNER);
    parent->repeat(CHR_HORIZONTAL_LINE, window_x-2);
    parent->set_color(12);
    parent->output(BORD_UPPER_LEFT_CORNER);

    parent->set_color(15);
    for(int i=1;i<window_y-1;i++) {
    	parent->move_cursor(offset_x, offset_y + i);
    	parent->output(CHR_VERTICAL_LINE);
    	parent->move_cursor(offset_x + window_x - 1, offset_y + i);
    	parent->output(CHR_VERTICAL_LINE);
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

void Window :: reset_border(void)
{
    // Quick fix
    offset_x --;
    offset_y --;
    window_x += 2;
    window_y += 2;
    border_h --;
    border_v --;
}

void Window :: draw_border_horiz(void)
{
    parent->move_cursor(offset_x, offset_y);
    parent->repeat(CHR_HORIZONTAL_LINE, window_x);
    parent->move_cursor(offset_x, offset_y + window_y-1);
    parent->repeat(CHR_HORIZONTAL_LINE, window_x);
    
    offset_y ++;
    window_y -=2;
    cursor_x = cursor_y = 0;
    border_h ++;
}

void Window :: reset_border_horiz(void)
{
    offset_y --;
    window_y +=2;
    border_h --;
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
    int ret = -1;
	
    va_start(ap, fmt);
	if(s) {
	    ret = _my_vprintf(Screen :: _put, (void **)s, fmt, ap);
	}
    va_end(ap);
    return (ret);
}

void Screen :: set_status(const char *message, int color)
{
    int old = get_color();
    set_color(color);
    set_background(0);
    move_cursor(0, get_size_y()-1);
    output_fixed_length(message, 0, get_size_x()-9);
    set_color(old);
}
