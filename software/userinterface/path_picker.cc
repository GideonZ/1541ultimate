/*
 * path_picker.cc
 *
 * Modal path picker built on top of TreeBrowser, with text fallback.
 */
#include "path_picker.h"
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "browsable_root.h"
#include "userinterface.h"
#include <string.h>

namespace {

class PathPickerBrowsableWrapper;
static void path_picker_filter_children(IndexedList<Browsable *> *children,
                                        bool keep_nonfile_entries,
                                        bool add_current_entry);
static bool path_picker_can_descend(Browsable *entry);
static bool path_picker_is_browser_key(int key);
static bool path_picker_hide_root_entry(Browsable *entry);

static bool path_picker_is_container_extension(const char *extension)
{
    if (!extension || !extension[0]) {
        return false;
    }
    return (strcmp(extension, "D64") == 0) ||
           (strcmp(extension, "D71") == 0) ||
           (strcmp(extension, "D81") == 0) ||
           (strcmp(extension, "DNP") == 0) ||
           (strcmp(extension, "G64") == 0) ||
           (strcmp(extension, "G71") == 0) ||
           (strcmp(extension, "T64") == 0) ||
           (strcmp(extension, "ISO") == 0) ||
           (strcmp(extension, "FAT") == 0);
}

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

class PathPickerBrowsableWrapper : public Browsable
{
    Browsable *wrapped;

public:
    PathPickerBrowsableWrapper(Browsable *entry)
        : wrapped(entry)
    {
        selectable = entry ? entry->isSelectable() : false;
    }

    ~PathPickerBrowsableWrapper()
    {
        delete wrapped;
    }

    void setSelection(bool s)
    {
        if (wrapped) {
            wrapped->setSelection(s);
        }
    }

    bool getSelection()
    {
        return wrapped ? wrapped->getSelection() : false;
    }

    bool isSelectable()
    {
        return wrapped ? wrapped->isSelectable() : false;
    }

    bool isPathPickerWrapper()
    {
        return true;
    }

    void allowSelectable(bool b)
    {
        selectable = b;
        if (wrapped) {
            wrapped->allowSelectable(b);
        }
    }

    FileInfo *getFileInfo()
    {
        return wrapped ? wrapped->getFileInfo() : NULL;
    }

    int getSortOrder(void)
    {
        return wrapped ? wrapped->getSortOrder() : 0;
    }

    Browsable *getParent()
    {
        return wrapped ? wrapped->getParent() : NULL;
    }

    const char *getName()
    {
        return wrapped ? wrapped->getName() : "";
    }

    void getDisplayString(char *buffer, int width)
    {
        if (wrapped) {
            wrapped->getDisplayString(buffer, width);
        } else if (width > 0) {
            buffer[0] = 0;
        }
    }

    void getDisplayString(char *buffer, int width, int squeeze_option)
    {
        if (wrapped) {
            wrapped->getDisplayString(buffer, width, squeeze_option);
        } else if (width > 0) {
            buffer[0] = 0;
        }
    }

    void fetch_context_items(IndexedList<Action *>&items)
    {
        if (wrapped) {
            wrapped->fetch_context_items(items);
        }
    }

    IndexedList<Browsable *> *getSubItems(int &error)
    {
        IndexedList<Browsable *> *list = wrapped ? wrapped->getSubItems(error) : NULL;
        if (error == 0) {
            path_picker_filter_children(list, false, true);
        }
        return list;
    }
};

static void path_picker_filter_children(IndexedList<Browsable *> *children,
                                        bool keep_nonfile_entries,
                                        bool add_current_entry)
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
                if (!entry->isPathPickerWrapper()) {
                    children->replace_idx(i, new PathPickerBrowsableWrapper(entry));
                }
                continue;
            }
            delete entry;
            children->remove_idx(i);
            continue;
        }
        if (!entry->isPathPickerWrapper()) {
            children->replace_idx(i, new PathPickerBrowsableWrapper(entry));
        }
    }
    if (add_current_entry && !already_has_current) {
        children->append(new PathPickerSelectFolderEntry());
        for (int i = children->get_elements() - 1; i > 0; i--) {
            children->swap(i, i - 1);
        }
    }
}

static bool path_picker_can_descend(Browsable *entry)
{
    if (!entry || entry->pickAsCurrentPath()) {
        return false;
    }
    FileInfo *info = entry->getFileInfo();
    if (!info) {
        return true;
    }
    if (path_picker_is_container_extension(info->extension)) {
        return false;
    }
    return (info->attrib & AM_DIR) != 0;
}

static bool path_picker_is_browser_key(int key)
{
    switch (key) {
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_PAGEUP:
        case KEY_PAGEDOWN:
        case KEY_HELP:
        case KEY_BREAK:
        case KEY_F8:
        case KEY_F10:
        case KEY_ESCAPE:
        case KEY_SCRLOCK:
        case KEY_MENU:
            return true;
        default:
            return false;
    }
}

static bool path_picker_hide_root_entry(Browsable *entry)
{
    if (!entry) {
        return false;
    }
    const char *name = entry->getName();
    if (!name || !name[0]) {
        return false;
    }
    if ((strcmp(name, "ftp") == 0) || (strcmp(name, "FTP") == 0) ||
        (strcmp(name, "WiFi") == 0)) {
        return true;
    }
    return (strncmp(name, "Net", 3) == 0);
}

class PathPickerRoot : public BrowsableRoot
{
public:
    IndexedList<Browsable *> *getSubItems(int &error)
    {
        IndexedList<Browsable *> *list = BrowsableRoot::getSubItems(error);
        if (error == 0) {
            // Root entries are not always backed by FileInfo. The current branch
            // uses network browsables there, and feature_ftp adds mounted FTP
            // roots via BrowsableRoot extensions. Keep only the local filesystem
            // roots in the FTP destination picker.
            path_picker_filter_children(list, true, false);
            for (int i = list->get_elements() - 1; i >= 0; i--) {
                Browsable *entry = (*list)[i];
                if (path_picker_hide_root_entry(entry)) {
                    delete entry;
                    list->remove_idx(i);
                }
            }
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
        if (!state) {
            return 0;
        }
        switch (c) {
            case KEY_UP:
                reset_quick_seek();
                state->up(1);
                return 0;
            case KEY_DOWN:
                reset_quick_seek();
                state->down(1);
                return 0;
            case KEY_PAGEUP:
                reset_quick_seek();
                state->up(window ? (window->get_size_y() / 2) : 1);
                return 0;
            case KEY_PAGEDOWN:
                reset_quick_seek();
                state->down(window ? (window->get_size_y() / 2) : 1);
                return 0;
            case KEY_HELP:
                reset_quick_seek();
                state->refresh = true;
                user_interface->help();
                if (frame) {
                    frame->reset_border();
                    frame->draw_border();
                    frame->clear();
                    frame->move_cursor(1, 0);
                    frame->output("Select destination:");
                }
                if (window) {
                    window->clear();
                }
                return 0;
            case KEY_LEFT:
                if (!state->previous && allow_exit) {
                    return MENU_CLOSE;
                }
                state->level_up();
                return 0;
            case KEY_RIGHT:
            case KEY_RETURN:
                reset_quick_seek();
                if (!state->under_cursor) {
                    return 0;
                }
                if (state->under_cursor->pickAsCurrentPath()) {
                    return pick_current();
                }
                if (path_picker_can_descend(state->under_cursor)) {
                    state->into2();
                }
                return 0;
            case KEY_BREAK:
            case KEY_F8:
            case KEY_F10:
            case KEY_ESCAPE:
            case KEY_SCRLOCK:
            case KEY_MENU:
                picked = false;
                return MENU_CLOSE;
            default:
                return 0;
        }
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
    screen->cursor_visible(0);
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
    browser->checkFileManagerEvent();
    if (browser->state && browser->state->refresh) {
        browser->state->do_refresh();
    } else {
        browser->redraw();
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
    (void)dummy;

    int raw = keyboard->getch();
    if (raw == -1) {
        return 0;
    }
    if (raw == -2) {
        screen->cursor_visible(0);
        return -1;
    }
    int key = get_ui()->keymapper(raw, e_keymap_default);

    if (text_mode) {
        if (path_picker_is_browser_key(key)) {
            screen->cursor_visible(0);
            text_mode = false;
            if (browser && strcmp(browser->getPath(), result_buf) != 0) {
                browser->cd(result_buf);
                browser->checkFileManagerEvent();
                if (browser->state && browser->state->refresh) {
                    browser->state->do_refresh();
                } else {
                    browser->redraw();
                }
            }
            int ret = browser ? browser->handle_key(key) : 0;
            if (browser && browser->state && browser->state->refresh) {
                browser->state->do_refresh();
            }
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

        keyboard->push_head(key);
        int ret = edit.poll(0);
        if (ret == 1) {
            screen->cursor_visible(0);
            text_mode = false;
            if (browser && strcmp(browser->getPath(), result_buf) != 0) {
                browser->cd(result_buf);
                browser->checkFileManagerEvent();
                if (browser->state && browser->state->refresh) {
                    browser->state->do_refresh();
                } else {
                    browser->redraw();
                }
            }
            update_result_from_browser();
            draw_path_line();
            return 0;
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
        if (browser && strcmp(browser->getPath(), result_buf) != 0) {
            browser->cd(result_buf);
            browser->checkFileManagerEvent();
            if (browser->state && browser->state->refresh) {
                browser->state->do_refresh();
            } else {
                browser->redraw();
            }
            edit.init(window, keyboard, 0, tree_rows + 1, tree_width);
            screen->cursor_visible(1);
        }
        return 0;
    }
    if (key >= 32 && key < 127) {
        enter_text_mode(key);
        return 0;
    }

    keyboard->push_head(raw);
    int ret = browser ? browser->poll(0) : 0;
    if (browser && browser->state && browser->state->refresh) {
        browser->state->do_refresh();
    }
    screen->cursor_visible(0);
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
