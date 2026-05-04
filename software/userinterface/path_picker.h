/*
 * path_picker.h
 *
 * Modal path picker: browse directories visually or type a path.
 * Cursor keys navigate the tree; any printable key switches to text input.
 */
#ifndef PATH_PICKER_H
#define PATH_PICKER_H

#include "ui_elements.h"

class TreeBrowser;
class Browsable;

class UIPathPicker : public UIObject
{
    // Result buffer (caller-owned)
    char *result_buf;
    int   result_max;

    // UI elements
    Window   *window;
    Screen   *screen;
    Keyboard *keyboard;
    TreeBrowser *browser;
    Browsable   *root;
    int  tree_rows;
    int  tree_width;

    // Mode
    bool text_mode;
    UIStringEdit edit;

    // Internal helpers
    void draw_path_line(void);
    void enter_text_mode(int first_key);
    void update_result_from_browser(void);

public:
    UIPathPicker(UserInterface *ui, char *buf, int maxlen);
    ~UIPathPicker();

    void init();
    void deinit(void);
    int  poll(int);
};

#endif
