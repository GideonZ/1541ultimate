/*
 * ui_elements.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */
#include "ui_elements.h"
#include <string.h>

const char *c_button_names[NUM_BUTTONS] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
const char c_button_keys[NUM_BUTTONS] = { 'o', 'y', 'n', 'c', 'a' };
const int c_button_widths[NUM_BUTTONS] = { 4, 5, 4, 5, 8 };

/* User Interface Objects */
/* Popup */
UIPopup :: UIPopup(char *msg, BYTE btns) : message(msg)
{
    buttons = btns;
    btns_active = 0;
    active_button = 0; // we can change this
    button_start_x = 0;
    window = 0;
    keyboard = 0;
}

void UIPopup :: init(Screen *screen, Keyboard *k)
{
    // first, determine how wide our popup needs to be
    int button_width = 0;
    int message_width = message.length();
    keyboard = k;

    BYTE b = buttons;
    for(int i=0;i<NUM_BUTTONS;i++) {
        if(b & 1) {
            btns_active ++;
            button_width += c_button_widths[i];
        }
        b >>= 1;
    }

    int window_width = message_width;
    if (button_width > message_width)
        window_width = button_width;

    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;
    int x_b = (window_width - button_width) / 2;
    button_start_x = x_b;

    window = new Window(screen, x1, y1, window_width+2, 5);
    window->draw_border();
    window->no_scroll();
    window->move_cursor(x_m, 0);
    window->output(message.c_str());

    active_button = 0; // we can change this
    draw_buttons();
}

void UIPopup :: draw_buttons()
{
    window->move_cursor(button_start_x, 2);
    int j=0;
    int b = buttons;
    for(int i=0;i<NUM_BUTTONS;i++) {
        if(b & 1) {
			window->reverse_mode((j == active_button)? 1 : 0);
        	window->output((char *)c_button_names[i]);
            button_key[j++] = c_button_keys[i];
        }
        b >>= 1;
    }

    keyboard->clear_buffer();
}

int UIPopup :: poll(int dummy, Event &e)
{
    BYTE c = keyboard->getch();

    for(int i=0;i<btns_active;i++) {
        if(c == button_key[i]) {
            return (1 << i);
        }
    }
    if((c == 0x0D)||(c == 0x20)) {
		for(int i=0,j=0;i<NUM_BUTTONS;i++) {
			if(buttons & (1 << i)) {
				if(active_button == j)
					return (1 << i);
				j++;
			}
		}
        return 0;
    }
    if(c == 0x1D) {
        active_button ++;
        if(active_button >= btns_active)
            active_button = btns_active-1;
        draw_buttons();
    } else if(c == 0x9D) {
        active_button --;
        if(active_button < 0)
            active_button = 0;
        draw_buttons();
    }
    return 0;
}

void UIPopup :: deinit()
{
	delete window;
}


UIStringBox :: UIStringBox(char *msg, char *buf, int max) : message(msg)
{
    buffer = buf;
    max_len = max;
    window = 0;
    keyboard = 0;
    cur = len = 0;
}

void UIStringBox :: init(Screen *screen, Keyboard *keyb)
{
    int message_width = message.length();
    int window_width = message_width;
    if (max_len > message_width)
        window_width = max_len;
    window_width += 2; // compensate for border

    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    keyboard = keyb;
    window = new Window(screen, x1, y1, window_width, 5);
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output_line(message.c_str());

    window->move_cursor(0, 2);
    //scr = window->get_pointer();

/// Now prefill the box...
    len = 0;
    cur = 0;

/// Default to old string
    cur = strlen(buffer); // assume it is prefilled, set cursor at the end.
    if(cur > max_len) {
        buffer[cur]=0;
        cur = max_len;
    }
    len = cur;
/// Default to old string
    window->output_length(buffer, len);
    window->move_cursor(cur, 2);
}

int UIStringBox :: poll(int dummy, Event &e)
{
    BYTE key;
    int i;

    key = keyboard->getch();
    if(!key)
        return 0;

    switch(key) {
    case 0x0D: // CR
        if(!len)
            return -1; // cancel
        return 1; // done
    case 0x9D: // left
    	if (cur > 0) {
            cur--;
        	window->move_cursor(cur, 2);
        }
        break;
    case 0x1D: // right
        if (cur < len) {
            cur++;
        	window->move_cursor(cur, 2);
        }
        break;
    case 0x14: // backspace
        if (cur > 0) {
            cur--;
            len--;
            for(i=cur;i<len;i++) {
                buffer[i] = buffer[i+1];
            } buffer[i] = 0;
//            window->move_cursor(0, 2);
            window->output_length(buffer, len);
            window->move_cursor(cur, 2);
        }
        break;
    case 0x93: // clear
        len = 0;
        cur = 0;
        window->move_cursor(0, 2);
        window->repeat(' ', max_len);
        window->move_cursor(cur, 2);
        break;
    case 0x94: // del
        if(cur < len) {
            len--;
            for(i=cur;i<len;i++) {
            	buffer[i] = buffer[i+1];
            } buffer[i] = 0x20;
            window->output_length(buffer, len);
            window->move_cursor(cur, 2);
        }
        break;
    case 0x13: // home
        cur = 0;
        window->move_cursor(cur, 2);
        break;
    case 0x11: // down = end
        cur = len;
        window->move_cursor(cur, 2);
        break;
    case 0x03: // break
        return -1; // cancel
    default:
        if ((key < 32)||(key > 127)) {
            break;
        }
        if (len < max_len) {
            for(i=len; i>=cur; i--) { // insert if necessary
                buffer[i+1] = buffer[i];
            }
            buffer[cur] = key;
            cur++;
            len++;
            window->move_cursor(0, 2);
            window->output_length(buffer, len);
            window->move_cursor(cur, 2);
        }
        break;
    }
    return 0; // still busy
}

void UIStringBox :: deinit(void)
{
	if (window)
		delete window;
}

/* Status Box */
UIStatusBox :: UIStatusBox(char *msg, int steps) : message(msg)
{
    total_steps = steps;
    progress = 0;
    window = 0;
}

void UIStatusBox :: init(Screen *screen)
{
    int window_width = 32;
    int message_width = message.length();
    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    window = new Window(screen, x1, y1, window_width, 5);
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output_line(message.c_str());
    window->move_cursor(0, 2);
}

void UIStatusBox :: deinit(void)
{
    delete window;
}

void UIStatusBox :: update(char *msg, int steps)
{
    static char percent[8];
    static char bar[40];
    progress += steps;

    if(msg) {
        message = msg;
        window->clear();
        window->output_line(message.c_str());
    }

    memset(bar, 32, 36);
    window->move_cursor(0, 2);
    window->reverse_mode(1);
    int terminate = (32 * progress) / total_steps;
    if(terminate > 32)
        terminate = 32;
    bar[terminate] = 0; // terminate
    window->output_line(bar);
}

