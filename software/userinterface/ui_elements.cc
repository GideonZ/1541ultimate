/*
 * ui_elements.cc
 *
 *  Created on: May 19, 2015
 *      Author: Gideon
 */
#include "ui_elements.h"
#include "userinterface.h"
#include <string.h>
#include <stdio.h>


/* User Interface Objects */
/* Popup */
UIPopup :: UIPopup(UserInterface *ui, const char *msg, uint8_t btns, int count, const char **names, const char *keys) : message(msg), button_count(count)
{
    user_interface = ui;
    button_names = names;
    button_keys = keys;
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

    uint8_t b = buttons;
    for(int i=0;i<button_count;i++) {
        if(b & 1) {
            btns_active ++;
            button_width += strlen(button_names[i]);
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

    screen->backup();
    window = new Window(screen, x1, y1, window_width+2, 5);
    window->clear();
    window->draw_border();
    // window->no_scroll();
    window->move_cursor(x_m, 0);
    window->set_color(user_interface->color_fg);
    window->output(message.c_str());

    active_button = 0; // we can change this
    keyboard->wait_free();
    draw_buttons();
}

void UIPopup :: draw_buttons()
{
    window->move_cursor(button_start_x, 2);
    int j=0;
    int b = buttons;
    for(int i=0;i<button_count;i++) {
        if(b & 1) {
			window->reverse_mode((j == active_button)? 1 : 0);
        	window->output((char *)button_names[i]);
            button_key[j++] = button_keys[i];
        }
        b >>= 1;
    }

    keyboard->clear_buffer();
}

int UIPopup :: poll(int dummy)
{
    int c = keyboard->getch();

    if (c == -1) // nothing pressed
    	return 0;
    if (c == -2) // error
    	return -1;

    int i;
    int selected_button = -1;
    if ((c == KEY_RETURN) || (c == KEY_SPACE)) {
        selected_button = active_button;
    }
    for (i=0; i < btns_active; i++) {
        if(c == button_key[i]) {
            selected_button = i;
	}
    }
    if (selected_button >= 0) {
        for(i=0; i < button_count; i++) {
            if (button_key[selected_button] == button_keys[i]) {
                return (1 << i);
            }
        }
    }

    if(c == KEY_RIGHT) {
        active_button ++;
        if(active_button >= btns_active)
            active_button = btns_active-1;
        draw_buttons();
    } else if(c == KEY_LEFT) {
        active_button --;
        if(active_button < 0)
            active_button = 0;
        draw_buttons();
    }
    return 0;
}

void UIPopup :: deinit()
{
    window->getScreen()->restore();
	delete window;
}

UIStringBox :: UIStringBox(const char *msg, char *buf, int max) : message(msg), edit(buf, max)
{
}

UIStringEdit :: UIStringEdit(char *buf, int max)
{
    buffer = buf;
    max_len = max;
    max_chars = 0;
    edit_offs = 0;
    window = 0;
    keyboard = 0;
    cur = len = 0;
    win_xoffs = 0;
    win_yoffs = 0;
}

void UIStringBox :: init(Screen *screen, Keyboard *keyb)
{
    int message_width = message.length();
    int window_width = message_width;
    if (edit.get_max_len() > message_width)
        window_width = edit.get_max_len();
    window_width += 3; // compensate for border, and the cursor

    // Maximize to screen width
    if (window_width >= screen->get_size_x()) {
        window_width = screen->get_size_x();
    }
    int max_chars = window_width - 2; // compensate for border. Total number of chars visible in string box

    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    screen->backup();
    window = new Window(screen, x1, y1, window_width, 5);
    window->clear();
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output(message.c_str());

    edit.init(window, keyb, 0, 2, max_chars);
}

void UIStringEdit :: init(Window *win, Keyboard *kb, int xo, int yo, int max_c)
{
    win_xoffs = xo;
    win_yoffs = yo;
    keyboard = kb;
    window = win;
    max_chars = max_c;

    window->move_cursor(xo, yo);
    keyboard->wait_free();

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
    if (len > (max_chars-1)) {
        edit_offs = 1 + len - max_chars;
    }
    window->output_length(buffer+edit_offs, (len < max_chars)?len : max_chars);
    window->move_cursor(win_xoffs+ cur-edit_offs, win_yoffs);
}

int UIStringEdit :: poll(int dummy)
{
    int key;
    int i;

    key = keyboard->getch();
    if (key == -1) // nothing pressed
		return 0;
	if (key == -2) // error
		return -1;

    switch(key) {
    case KEY_RETURN: // CR
        buffer[len] = 0;
        if(!len)
            return -1; // cancel
        return 1; // done
    case KEY_LEFT: // left
    	if (cur > 0) {
            cur--;
            if (cur-5 < edit_offs) { // scroll left?
                edit_offs -= 5;
                if (edit_offs < 0)
                    edit_offs = 0;
                window->move_cursor(win_xoffs, win_yoffs);
                window->output_length(buffer+edit_offs, max_chars);
            }
            window->move_cursor(win_xoffs + cur-edit_offs, win_yoffs);
        }
        break;
    case KEY_RIGHT: // right
        if (cur < len) {
            cur++;
            if ((cur-edit_offs) > (max_chars-1)) {
                edit_offs++;
                window->move_cursor(win_xoffs, win_yoffs);
                window->output_length(buffer+edit_offs,max_chars);
            }
        	window->move_cursor(win_xoffs + cur-edit_offs, win_yoffs);
        }
        break;
    case KEY_BACK: // backspace
        if (cur > 0) {
            cur--;
            len--;
            for(i=cur;i<len;i++) {
                buffer[i] = buffer[i+1];
            } buffer[i] = 0;

            if (cur-5 < edit_offs) { // scroll left?
                edit_offs -= 5;
                if (edit_offs < 0)
                    edit_offs = 0;
                window->move_cursor(win_xoffs, win_yoffs);
                window->output_length(buffer+edit_offs, max_chars);
                window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
            } else { // no scroll left, just redraw from cursor position
                window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
                window->output_length(buffer+cur, max_chars+edit_offs-cur);
                window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
            }
        }
        break;
    case KEY_CLEAR: // clear
        buffer[0] = 0;
        len = 0;
        cur = 0;
        edit_offs = 0;
        window->move_cursor(win_xoffs, win_yoffs);
        window->output_length(buffer+cur, max_chars+edit_offs-cur);
        window->move_cursor(win_xoffs, win_yoffs);
        break;
    case KEY_DELETE: // del
        if(cur < len) {
            len--;
            for(i=cur;i<len;i++) {
            	buffer[i] = buffer[i+1];
            } buffer[i] = 0;
            window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
            window->output_length(buffer+cur, max_chars+edit_offs-cur);  // cursor position = cur-edit_offs. remaining chars = max_chars-cursor_position = max_chars+edit_offs-cur
            window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
        }
        break;
    case KEY_HOME: // home
    case KEY_UP: // up = home
        cur = 0;
        if (edit_offs) { // scroll to the  beginning?
            edit_offs = 0;
            window->move_cursor(win_xoffs, win_yoffs);
            window->output_length(buffer, max_chars);
        }
        window->move_cursor(win_xoffs + cur, win_yoffs);
        break;
    case KEY_DOWN: // down = end
    case KEY_END:
        if (cur != len) {
            cur = len;
            if (cur >= (max_chars-1)) {
                edit_offs = 1 + cur - max_chars;
                window->move_cursor(win_xoffs, win_yoffs);
                window->output_length(buffer+edit_offs, max_chars);
            }
            window->move_cursor(win_xoffs + cur-edit_offs, win_yoffs);
        }
        break;
    case KEY_BREAK: // break
    case KEY_ESCAPE: // exit!
        return -1; // cancel
    default:
        if ((key < 32)||(key >= 127)) {
            printf("Unhandled key: %d\n", key);
            break;
        }
        if (len < max_len) {
            for(i=len; i>=cur; i--) { // insert if necessary
                buffer[i+1] = buffer[i];
            }
            buffer[cur] = key;
            cur++;
            len++;

            // When do we NOT need to redraw all? If cursor is not at the end of the edit box
            // then it is sufficient to redraw only from the cursor to the end of the string
            if (cur-edit_offs < max_chars) {
                // window->move_cursor(cur-edit_offs, 2);
                window->output_length(buffer+cur-1, max_chars+edit_offs-cur);
                window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
            } else {
                edit_offs++;
                window->move_cursor(win_xoffs, win_yoffs);
                window->output_length(buffer+edit_offs, max_chars);
                window->move_cursor(win_xoffs+cur-edit_offs, win_yoffs);
            }
        }
        break;
    }
    return 0; // still busy
}

void UIStringBox :: deinit(void)
{
	if (window) {
	    window->getScreen()->restore();
		delete window;
	}
}

/* Choice box */
UIChoiceBox :: UIChoiceBox(const char *msg, const char **choices, int count) : message(msg)
{
    this->choices = choices;
    this->count = count;
    this->keyboard = NULL;
    this->current = 0;
    this->color_fg = 15;
    this->color_bg = 0;
    this->color_sel_fg = 1;
    this->color_sel_bg = 6;
}

void UIChoiceBox :: init(Screen *screen, Keyboard *kb, int fg, int bg, int sel_fg, int sel_bg)
{
    this->color_fg = fg;
    this->color_bg = bg;
    this->color_sel_fg = sel_fg;
    this->color_sel_bg = sel_bg;

    int rows = 2 + count;
	int max_len = message.length();
    int len;
    for(int i=0;i<count;i++) {
        len = strlen(choices[i]);
        if (len > max_len) {
            max_len = len;
        }
    }
    if (max_len > 25) {
        max_len = 25;
    }
    keyboard = kb;

    screen->backup();
    int y_offs = (screen->get_size_y() - rows - 2) >> 1;
    window = new Window(screen, (screen->get_size_x() - max_len - 2) >> 1, y_offs, max_len+2, rows);
    window->set_color(color_fg);
    window->draw_border();
    redraw();
}

void UIChoiceBox :: redraw(void)
{
    window->move_cursor(0, 0);
    window->set_color(color_fg);
    window->set_background(color_bg);
    window->output_line(message.c_str());
    window->move_cursor(0, 1);
    window->output_line("");
    for(int i=0; i < count; i++) {
        window->move_cursor(0, i+2);
        if(i == current) {
            window->set_color(color_sel_fg);
            window->set_background(color_sel_bg);
        } else {
            window->set_color(color_fg);
            window->set_background(color_bg);
        }
        window->output_line(choices[i]);
    }
}

void UIChoiceBox :: deinit(void)
{
    window->getScreen()->restore();
    delete window;
}

int  UIChoiceBox :: poll(int)
{
    int ret = 0;
    int c;
        
    if(!keyboard) {
        printf("Choice picker: Keyboard not initialized.. exit.\n");
        return -1;
    }

    c = keyboard->getch();

    if (c == -1) // nothing pressed
    	return 0;
    if (c == -2) // error
    	return -1;
    switch(c) {
        case -1: return 0; // nothing pressed
        case -2: return -1; // error
        case KEY_UP:
            if (current > 0) {
                current--;
                redraw();
            }
            break;
        case KEY_DOWN:
            if (current < count-1) {
                current++;
                redraw();
            }
            break;
        case KEY_SPACE:
        case KEY_RETURN:
            return current;
        default:
            break;
    }
    return 0; // busy
}


/* Status Box */
UIStatusBox :: UIStatusBox(const char *msg, int steps) : message(msg)
{
    total_steps = steps;
    progress = 0;
    window = 0;
}

void UIStatusBox :: init(Screen *screen)
{
    int window_width = 34;
    int message_width = message.length();
    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    screen->backup();
    window = new Window(screen, x1, y1, window_width, 5);
    window->clear();
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output(message.c_str());
    window->move_cursor(0, 2);
    screen->sync();
}

void UIStatusBox :: deinit(void)
{
    window->getScreen()->restore();
    delete window;
}

void UIStatusBox :: update(const char *msg, int steps)
{
    static char bar[40];
    progress += steps;

    if(msg) {
        message = msg;
        int message_width = message.length();
        int x_m = (window->get_size_x() - message_width) / 2;
        window->clear();
        window->move_cursor(x_m, 0);
        window->output(message.c_str());
    }

    memset(bar, 32, 36);
    window->move_cursor(0, 2);
    window->reverse_mode(1);
    int terminate = (32 * progress) / total_steps;
    if(terminate > 32)
        terminate = 32;
    bar[terminate] = 0; // terminate
    window->output(bar);
    window->getScreen()->sync();
}
