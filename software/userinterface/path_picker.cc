/*
 * path_picker.cc
 *
 * Modal path picker with tree browsing and text fallback.
 */
#include "path_picker.h"
#include "userinterface.h"
#include <string.h>
#include <stdio.h>

UIPathPicker::UIPathPicker(UserInterface *ui, char *buf, int maxlen)
    : UIObject(ui), edit(buf, maxlen)
{
    result_buf = buf;
    result_max = maxlen;
    window = NULL;
    screen = NULL;
    keyboard = NULL;
    fm = FileManager::getFileManager();
    nav_path = fm->get_new_path("picker");
    nav_path->cd("/");
    dir_list = NULL;
    dir_count = 0;
    selected = 0;
    scroll_offset = 0;
    tree_rows = 0;
    tree_width = 0;
    text_mode = false;
}

UIPathPicker::~UIPathPicker()
{
    if (dir_list) {
        for (int i = 0; i < dir_list->get_elements(); i++) {
            delete (*dir_list)[i];
        }
        delete dir_list;
    }
    fm->release_path(nav_path);
}

void UIPathPicker::init()
{
    screen = get_ui()->get_screen();
    keyboard = get_ui()->get_keyboard();

    int sw = screen->get_size_x();
    int sh = screen->get_size_y();

    // Window: nearly full screen
    int ww = sw - 4;
    if (ww < 20) ww = 20;
    int wh = sh - 4;
    if (wh < 8) wh = 8;
    int x1 = (sw - ww) / 2;
    int y1 = (sh - wh) / 2;

    screen->backup();
    window = new Window(screen, x1, y1, ww, wh);
    window->clear();
    window->draw_border();

    // Layout: row 0 = title, row 1..tree_rows = tree, last row = path edit
    tree_rows = wh - 4; // border top + title + border bottom + path line
    tree_width = ww - 2;

    // Title
    window->move_cursor(1, 0);
    window->output("Select destination:");

    // Initial path in buffer
    update_result_from_nav();
    draw_path_line();
    load_directory();
    draw_tree();
}

void UIPathPicker::deinit()
{
    if (window) {
        delete window;
        window = NULL;
    }
    screen->restore();
}

void UIPathPicker::load_directory()
{
    if (dir_list) {
        for (int i = 0; i < dir_list->get_elements(); i++) {
            delete (*dir_list)[i];
        }
        delete dir_list;
    }
    dir_list = new IndexedList<FileInfo *>(32, NULL);
    fm->get_directory(nav_path, *dir_list, NULL);

    // Filter: keep only directories
    for (int i = dir_list->get_elements() - 1; i >= 0; i--) {
        FileInfo *fi = (*dir_list)[i];
        if (!(fi->attrib & AM_DIR)) {
            delete fi;
            dir_list->remove_idx(i);
        }
    }
    // +1 for the virtual "<select this folder>" entry at index 0
    dir_count = dir_list->get_elements() + 1;
    selected = 0;
    scroll_offset = 0;
}

void UIPathPicker::draw_tree()
{
    for (int row = 0; row < tree_rows; row++) {
        int idx = scroll_offset + row;
        window->move_cursor(0, row + 1);

        if (idx < dir_count) {
            char line[80];
            memset(line, ' ', tree_width);
            line[tree_width] = '\0';

            if (idx == 0) {
                // Virtual "<select this folder>" entry
                const char *label = " << Select this folder >>";
                int llen = strlen(label);
                if (llen > tree_width) llen = tree_width;
                memcpy(line, label, llen);
            } else {
                // Real directory entry (dir_list index = idx - 1)
                FileInfo *fi = (*dir_list)[idx - 1];
                const char *name = fi->lfname;
                int nlen = strlen(name);
                if (nlen > tree_width - 2) nlen = tree_width - 2;
                line[0] = '>';
                memcpy(line + 1, name, nlen);
            }

            if (idx == selected) {
                window->reverse_mode(1);
            }
            window->output_length(line, tree_width);
            if (idx == selected) {
                window->reverse_mode(0);
            }
        } else {
            // Empty row
            window->repeat(' ', tree_width);
        }
    }
}

void UIPathPicker::draw_path_line()
{
    int path_row = tree_rows + 1;
    window->move_cursor(0, path_row);
    window->reverse_mode(1);
    window->output_length(result_buf, tree_width);
    window->reverse_mode(0);
}

void UIPathPicker::update_result_from_nav()
{
    const char *p = nav_path->get_path();
    strncpy(result_buf, p, result_max);
    result_buf[result_max - 1] = '\0';
}

void UIPathPicker::enter_text_mode(int first_key)
{
    text_mode = true;

    // Set up the edit widget on the path line
    int path_row = tree_rows + 1;
    int max_chars = tree_width;
    edit.init(window, keyboard, 0, path_row, max_chars);
    screen->cursor_visible(1);

    // Feed the first key into the buffer if it's printable
    if (first_key >= 32 && first_key < 127) {
        // Append to current path
        int len = strlen(result_buf);
        if (len < result_max - 1) {
            result_buf[len] = (char)first_key;
            result_buf[len + 1] = '\0';
        }
        // Reinit edit to pick up new buffer content
        edit.init(window, keyboard, 0, path_row, max_chars);
    }
}

int UIPathPicker::poll(int dummy)
{
    if (text_mode) {
        int ret = edit.poll(dummy);
        if (ret == 1) {
            // RETURN in text mode: accept
            screen->cursor_visible(0);
            return 1;
        }
        if (ret == -1) {
            // ESCAPE/BREAK in text mode: go back to tree mode
            screen->cursor_visible(0);
            text_mode = false;
            update_result_from_nav();
            draw_path_line();
            draw_tree();
            return 0;
        }
        // Update path line display while editing
        draw_path_line();
        return 0;
    }

    // Tree mode
    int key = keyboard->getch();
    if (key == -1) return 0;
    if (key == -2) return -1;

    switch (key) {
    case KEY_UP:
        if (selected > 0) {
            selected--;
            if (selected < scroll_offset) {
                scroll_offset = selected;
            }
            draw_tree();
            update_result_from_nav();
            draw_path_line();
        }
        break;

    case KEY_DOWN:
        if (selected < dir_count - 1) {
            selected++;
            if (selected >= scroll_offset + tree_rows) {
                scroll_offset = selected - tree_rows + 1;
            }
            draw_tree();
            update_result_from_nav();
            draw_path_line();
        }
        break;

    case KEY_RIGHT:
    case KEY_RETURN:
        if (selected == 0) {
            // "<select this folder>" — accept current path
            return 1;
        }
        if (selected > 0 && selected < dir_count) {
            FileInfo *fi = (*dir_list)[selected - 1];
            const char *name = fi->lfname;
            nav_path->cd(name);
            update_result_from_nav();
            draw_path_line();
            load_directory();
            draw_tree();
        }
        break;

    case KEY_LEFT:
        nav_path->cd("..");
        update_result_from_nav();
        draw_path_line();
        load_directory();
        draw_tree();
        break;

    case KEY_BREAK:
    case KEY_ESCAPE:
        return -1; // cancel

    default:
        // Any printable key: switch to text edit mode
        if (key >= 32 && key < 127) {
            enter_text_mode(key);
        }
        break;
    }

    return 0;
}
