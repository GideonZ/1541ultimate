/*
 * path_picker.h
 *
 * Modal path picker: browse directories visually or type a path.
 * Cursor keys navigate the tree; any printable key switches to text input.
 */
#ifndef PATH_PICKER_H
#define PATH_PICKER_H

#include "ui_elements.h"
#include "browsable.h"
#include "filemanager.h"

class UIPathPicker : public UIObject
{
    // Result buffer (caller-owned)
    char *result_buf;
    int   result_max;

    // UI elements
    Window   *window;
    Screen   *screen;
    Keyboard *keyboard;

    // Tree browsing state
    FileManager *fm;
    Path *nav_path;
    IndexedList<FileInfo *> *dir_list;
    int  dir_count;
    int  selected;
    int  scroll_offset;
    int  tree_rows;      // visible rows for the tree
    int  tree_width;

    // Mode
    bool text_mode;      // false = tree, true = text edit
    UIStringEdit edit;   // reuse existing string edit widget

    // Internal helpers
    void load_directory(void);
    void draw_tree(void);
    void draw_path_line(void);
    void enter_text_mode(int first_key);
    void update_result_from_nav(void);

public:
    UIPathPicker(UserInterface *ui, char *buf, int maxlen);
    ~UIPathPicker();

    void init();
    void deinit(void);
    int  poll(int);
};

#endif
