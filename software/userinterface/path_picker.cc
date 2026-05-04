/*
 * path_picker.cc
 *
 * Modal path picker built on top of TreeBrowser, with text fallback.
 */
#include "path_picker.h"
#include "tree_browser.h"
#include "browsable_root.h"
#include "userinterface.h"
#include <string.h>

namespace {

class PathPickerSelectFolderEntry : public Browsable
{
public:
    PathPickerSelectFolderEntry()
    {
        selectable = true;
    }

    const char *getName()
    {
        return "<< Select this folder >>";
    }

    void getDisplayString(char *buffer, int width)
    {
        getDisplayString(buffer, width, 0);
    }

    void getDisplayString(char *buffer, int width, int)
    {
        if (width <= 0) {
            return;
        }
        const char *label = getName();
        int len = (int)strlen(label);
        if (len > width - 1) len = width - 1;
        memcpy(buffer, label, len);
        buffer[len] = 0;
    }

    bool pickAsCurrentPath()
    {
        return true;
    }
};

static void path_picker_filter_children(IndexedList<Browsable *> *children,
                                        bool keep_nonfile_entries)
{
    if (!children) {
        return;
    }
    bool already_has_current = false;
    for (int i = children->get_elements() - 1; i >= 0; i--) {
        Browsable *entry = (*children)[i];
        if (!entry) {
            continue;
        }
        if (entry->pickAsCurrentPath()) {
            already_has_current = true;
            continue;
        }
        FileInfo *info = entry->getFileInfo();
        if (!info) {
            if (keep_nonfile_entries) {
                continue;
            }
            delete entry;
            children->remove_idx(i);
            continue;
        }
        if (!(info->attrib & AM_DIR)) {
            delete entry;
            children->remove_idx(i);
        }
    }
    if (!already_has_current) {
        children->prepend(new PathPickerSelectFolderEntry());
    }
}

class PathPickerDirEntry : public BrowsableDirEntry
{
public:
    PathPickerDirEntry(Path *pp, Browsable *parent, FileInfo *info, bool sel)
        : BrowsableDirEntry(pp, parent, info, sel) {}

protected:
    Browsable *makeChildEntry(Path *parent_path, FileInfo *info, bool selectable)
    {
        return new PathPickerDirEntry(parent_path, this, info, selectable);
    }

public:
    IndexedList<Browsable *> *getSubItems(int &error)
    {
        IndexedList<Browsable *> *list = BrowsableDirEntry::getSubItems(error);
        if (error == 0) {
            path_picker_filter_children(list, false);
        }
        return list;
    }
};

class PathPickerRoot : public BrowsableRoot
{
protected:
    Browsable *makeChildEntry(Path *parent_path, FileInfo *info, bool selectable)
    {
        return new PathPickerDirEntry(parent_path, this, info, selectable);
    }

public:
    IndexedList<Browsable *> *getSubItems(int &error)
    {
        IndexedList<Browsable *> *list = BrowsableRoot::getSubItems(error);
        if (error == 0) {
            // Root entries are not always backed by FileInfo. The current branch
            // uses network browsables there, and feature_ftp adds mounted FTP
            // roots via BrowsableRoot extensions. Keep those enterable roots.
            path_picker_filter_children(list, true);
        }
        return list;
    }
};

class PathPickerTreeBrowser : public TreeBrowser
{
    Window *frame;
    Keyboard *picker_keyboard;

public:
    PathPickerTreeBrowser(UserInterface *ui, Browsable *root, Window *parent, Keyboard *keyb)
        : TreeBrowser(ui, root), frame(parent), picker_keyboard(keyb) {}

    void init()
    {
        int ox;
        int oy;

        screen = frame->getScreen();
        keyb = picker_keyboard;
        frame->getOffsets(ox, oy);
        window = new Window(screen, ox + 1, oy + 1, frame->get_size_x() - 2, frame->get_size_y() - 4);
        has_path = false;
        has_border = false;
        allow_exit = true;
        use_ui_focus_stack = false;
        pick_mode = PICK_SAVE;
        state->reload();
    }

    void deinit(void)
    {
        if (window) {
            delete window;
            window = NULL;
        }
    }

    int handle_key(int c)
    {
        if (pick_mode != PICK_NONE && c == KEY_RETURN) {
            reset_quick_seek();
            if (!state || !state->under_cursor) {
                return 0;
            }
            if (state->under_cursor->pickAsCurrentPath()) {
                return pick_current();
            }
            state->into2();
            return 0;
        }
        return TreeBrowser::handle_key(c);
    }
};

} // namespace

UIPathPicker::UIPathPicker(UserInterface *ui, char *buf, int maxlen)
    : UIObject(ui), edit(buf, maxlen)
{
    result_buf = buf;
    result_max = maxlen;
    window = NULL;
    screen = NULL;
    keyboard = NULL;
    browser = NULL;
    root = NULL;
    tree_rows = 0;
    tree_width = 0;
    text_mode = false;
}

UIPathPicker::~UIPathPicker()
{
    delete browser;
    delete root;
}

void UIPathPicker::init()
{
    screen = get_ui()->get_screen();
    keyboard = get_ui()->get_keyboard();

    int sw = screen->get_size_x();
    int sh = screen->get_size_y();
    int ww = sw - 4;
    int wh = sh - 4;

    if (ww < 20) ww = 20;
    if (wh < 8) wh = 8;

    screen->backup();
    window = new Window(screen, (sw - ww) / 2, (sh - wh) / 2, ww, wh);
    window->clear();
    window->draw_border();

    tree_rows = wh - 4;
    tree_width = ww - 2;

    window->move_cursor(1, 0);
    window->output("Select destination:");

    root = new PathPickerRoot();
    browser = new PathPickerTreeBrowser(get_ui(), root, window, keyboard);
    browser->init();
    if (result_buf && result_buf[0]) {
        browser->cd(result_buf);
    }
    update_result_from_browser();
    draw_path_line();
}

void UIPathPicker::deinit()
{
    if (browser) {
        browser->deinit();
    }
    if (window) {
        delete window;
        window = NULL;
    }
    if (screen) {
        screen->cursor_visible(0);
        screen->restore();
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

void UIPathPicker::update_result_from_browser()
{
    const char *path = browser ? browser->getPath() : "/";
    strncpy(result_buf, path, result_max);
    result_buf[result_max - 1] = 0;
}

void UIPathPicker::enter_text_mode(int first_key)
{
    text_mode = true;
    edit.init(window, keyboard, 0, tree_rows + 1, tree_width);
    screen->cursor_visible(1);

    if (first_key >= 32 && first_key < 127) {
        int len = strlen(result_buf);
        if (len < result_max - 1) {
            result_buf[len] = (char)first_key;
            result_buf[len + 1] = 0;
        }
        edit.init(window, keyboard, 0, tree_rows + 1, tree_width);
    }
}

int UIPathPicker::poll(int dummy)
{
    if (text_mode) {
        int ret = edit.poll(dummy);
        if (ret == 1) {
            screen->cursor_visible(0);
            return 1;
        }
        if (ret == -1) {
            screen->cursor_visible(0);
            text_mode = false;
            update_result_from_browser();
            draw_path_line();
            if (browser) {
                browser->redraw();
            }
            return 0;
        }
        draw_path_line();
        return 0;
    }

    int key = keyboard->getch();
    if (key == -1) {
        return 0;
    }
    if (key == -2) {
        return -1;
    }
    if (key >= 32 && key < 127) {
        enter_text_mode(key);
        return 0;
    }

    keyboard->push_head(key);
    int ret = browser ? browser->poll(0) : 0;
    update_result_from_browser();
    draw_path_line();
    if (ret == MENU_CLOSE) {
        if (browser && browser->picked) {
            strncpy(result_buf, browser->picked_path.c_str(), result_max);
            result_buf[result_max - 1] = 0;
            return 1;
        }
        return -1;
    }
    return 0;
}
