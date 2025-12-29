#include "userinterface.h"
#include <stdio.h>

#ifndef NO_FILE_ACCESS
#include "FreeRTOS.h"
#include "task.h"
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "path.h"
#include "keyboard_usb.h"
#ifndef UPDATER
#ifndef RECOVERYAPP
#include "c1541.h"
#endif // RECOVERYAPP
#endif // UPDATER
#endif // NO_FILE_ACCESS

/* Help */
static const char *helptext =
        "WASD:       Up/Left/Down/Right\n"
        "Cursor Keys:Up/Left/Down/Right\n"
        "  (Up/Down) Selection up/down\n"
        "  (Left)    Go one level up\n"
        "            leave directory or disk\n"
        "  (Right)   Go one level down\n"
        "            enter directory or disk\n"
        "RETURN:     Selection context menu\n"
        "RUN/STOP:   Leave menu / Back\n"
        "\n"
#if COMMODORE
        "F1:         Action Menu\n"
        "F3:         Page up\n"
        "F5:         Page down\n"
        "F7:         Help\n"
#else
        "F1:         Page up\n"
        "F3:         Help\n"
        "F5:         Action Menu\n"
        "F7:         Page down\n"
#endif
        "\n"
        "F2:         Enter Advanced Settings\n"
    #ifndef RECOVERYAPP
        "F4:         Show System Information\n"
        "F6:         CommoServe File Search\n"
    #endif
        "\n"
        "SPACE:      Select file / directory\n"
        "C= A:       Select all\n"
        "C= N:       Deselect all\n"
        "C= C:       Copy current selection\n"
        "C= V:       Paste selection here\n"
        "\n"
        "HOME:       Enter home directory\n"
        "C= HOME:    Set current dir as home\n"
        "INST:       Delete selected files\n"
        "\n"  
		"Quick seek: Use the keyboard to type\n"
		"            the name to search for:\n"
		"            You can use ? as a\n"
		"            wildcard. When WASD\n"
        "            cursors are enabled,\n"
        "            type with SHIFT pressed.\n"
        "\n"  
    #ifndef RECOVERYAPP
    #endif
        "C= L:       Show Debug Log\n"
#if U64 == 2
        "\nOutside menus the machine can be\n"
        "reset by holding the switch up\n"
        "for 1 second.\n"
#endif
		"\nRUN/STOP to close this window.";

/* Configuration */
static const char *colors[] = { "Commodore Blue", "Ultimate Black", "Commodore 1", "Commodore 2", "Commodore 3" };
                          
static const char *filename_overflow_squeeze[] = { "None", "Beginning", "Middle", "End" };
static const char *itype[]      = { "Freeze", "Overlay on HDMI" };
static const char *cfg_save[]   = { "No", "Ask", "Yes" };
static const char *navstyles[] = { "Quick Search", "WASD Cursors" };

struct t_cfg_definition user_if_config[] = {
#if U64
    { CFG_USERIF_ITYPE,      CFG_TYPE_ENUM,   "Interface Type",       "%s", itype,   0,  1, 0 },
#endif
#if COMMODORE && !RECOVERYAPP
    { CFG_USERIF_NAVIGATION, CFG_TYPE_ENUM,   "Navigation Style",     "%s", navstyles, 0,  1, 1 },
    { CFG_USERIF_COLORSCHEME,CFG_TYPE_ENUM,   "Color Scheme",         "%s", colors,  0,  4, 0 },
#else
    { CFG_USERIF_NAVIGATION, CFG_TYPE_ENUM,   "Navigation Style",     "%s", navstyles, 0,  1, 0 },
    { CFG_USERIF_COLORSCHEME,CFG_TYPE_ENUM,   "Color Scheme",         "%s", colors,  0,  4, 1 },
#endif
//    { CFG_USERIF_WORDWRAP,   CFG_TYPE_ENUM,   "Wordwrap text viewer", "%s", en_dis,  0,  1, 1 },

#ifndef COMMODORE
    { CFG_USERIF_START_HOME, CFG_TYPE_ENUM,   "Enter Home on Startup", "%s", en_dis, 0,  1, 0 },
    { CFG_USERIF_HOME_DIR,   CFG_TYPE_STRING, "Home Directory",        "%s", NULL, 0, 31, (int)"" },
#endif

    { CFG_USERIF_CFG_SAVE,   CFG_TYPE_ENUM,   "Auto Save Config",      "%s", cfg_save, 0, 2, 1 },
    { CFG_USERIF_ULTICOPY_NAME, CFG_TYPE_ENUM, "Ulticopy Uses disk name", "%s", en_dis, 0, 1, 1 },
    { CFG_USERIF_FILENAME_OVERFLOW_SQUEEZE, CFG_TYPE_ENUM, "Filename overflow squeeze", "%s", filename_overflow_squeeze, 0, 3, 0 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

UserInterface :: UserInterface(const char *title, bool use_logo) : title(title)
{
    initialized = false;
    focus = -1;
    host = NULL;
    keyboard = NULL;
    screen = NULL;
    doBreak = false;
    available = false;
    color_sel_bg = 0;
    filename_overflow_squeeze = 0;
    menu_response_to_action = MENU_NOP;
    logo = use_logo;
    register_store(0x47454E2E, "User Interface Settings", user_if_config);
    effectuate_settings();
}

UserInterface :: ~UserInterface()
{
	printf("Destructing user interface..\n");
    do {
        if (ui_objects[focus]) {
            ui_objects[focus]->deinit();
            delete ui_objects[focus];
        }
        focus--;
    } while(focus>=0);
    printf(" bye UI!\n");
}

typedef struct {
    int border;
    int background;
    int foreground;
    int selected;
    int selected_bg;
    int selected_rev;
} t_scheme_colors;

const t_scheme_colors schemes[] = {
    { 6, 14,  1, 1, 14, 1 },
    { 0,  0, 12, 1, 6,  0 },
    { 6, 14,  1, 0, 14, 0 },
    { 6, 14, 15, 1, 14, 0 },
    { 6, 14,  0, 1, 14, 0 },
};

void UserInterface :: effectuate_settings(void)
{
    const t_scheme_colors *scheme = logo ? &schemes[cfg->get_value(CFG_USERIF_COLORSCHEME)] : &schemes[1]; // for telnet always use black
    color_border = scheme->border;
    color_fg     = scheme->foreground;
    color_bg     = scheme->background;
    color_sel    = scheme->selected;
    reverse_sel  = scheme->selected_rev;

#if U64
    color_sel_bg = scheme->selected_bg;
#endif
    config_save  = cfg->get_value(CFG_USERIF_CFG_SAVE);
    filename_overflow_squeeze = cfg->get_value(CFG_USERIF_FILENAME_OVERFLOW_SQUEEZE);
    navmode      = cfg->get_value(CFG_USERIF_NAVIGATION);

    if(host && host->is_accessible())
        host->set_colors(color_bg, color_border);

    // push_event(e_refresh_browser); TODO
}
    
void UserInterface :: init(GenericHost *h)
{
    host = h;
    keyboard = h->getKeyboard();
	screen = h->getScreen();
    initialized = true;
    if (host->is_permanent()) {
    	appear();
    }
}

int UserInterface :: getPreferredType(void)
{
    return cfg->get_value(CFG_USERIF_ITYPE);
}

void UserInterface :: set_screen(Screen *s)
{
    screen = s;
}

void UserInterface :: run_remote(void)
{
    host->take_ownership(this);
    appear();
    available = true;
    while(1) {
        if (pollFocussed() == MENU_EXIT) {
            available = false;
            break;
        }
        vTaskDelay(3);
    }
    host->release_ownership();
}

void UserInterface :: run(void)
{
    while(1) {
        if (host->exists()) {
            host->checkButton();
            if (host->buttonPush()) {
                run_once();
                continue;
            }
            switch(system_usb_keyboard.getch()) {
            case KEY_SCRLOCK:
            case KEY_F10:
                run_once();
                continue;
            case 0x04: // CTRL-D
                swapDisk();
                break;
            }
        }
        vTaskDelay(3);
    }
}

void UserInterface :: run_once(void)
{
#ifndef NO_FILE_ACCESS

    if (!host->exists())
        return;

    if(buttonDownFor(1000)) {
        swapDisk();
        return;
    }

    host->take_ownership(this);
    if (!host->is_permanent()) {
        appear();
    }

    available = true;
    while(!doBreak) {
        host->checkButton();
        if (!host->exists()) {
            break;
        } else if (host->buttonPush()) {
            if (!host->is_permanent()) {
                available = false;
                release_host();
            }
            host->release_ownership();
            break;
        } else {
            int ret = pollFocussed();
            if (ret != 0)
                printf("Poll Focussed returned %d. DoBreak = %d.\n", ret, doBreak);
            switch(ret) {
            case MENU_NOP:
                break;
            case MENU_HIDE:
            case MENU_EXIT:
                available = false;
                doBreak = true;
                if (!host->is_permanent()) {
                    release_host();
                }
                host->release_ownership();
                break;
            default:
                break;
            }
        }
        vTaskDelay(3);
    }
    doBreak = false;
#endif
}


#ifndef RECOVERYAPP
bool UserInterface :: buttonDownFor(uint32_t ms)
{
    bool down = false;
#ifndef UPDATER
    uint8_t delay = 10;
    TickType_t ticks = delay / portTICK_PERIOD_MS;
    uint32_t elapsed = 0;
    
    while((ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS) & ITU_BUTTON1) {
        vTaskDelay(ticks);
        elapsed+=delay;
    
        if(elapsed > ms) {
            down = true;
            break;
        }
    }
#endif
    return down;
}
#else
bool UserInterface :: buttonDownFor(uint32_t ms)
{
	return false;
}
#endif

void UserInterface :: swapDisk(void)
{
#ifndef RECOVERYAPP
#ifndef UPDATER                  
	C1541* drive = C1541::get_last_mounted_drive();

    if(drive != NULL) {
      SubsysCommand* swap =
        new SubsysCommand(this, drive->getID(), MENU_1541_SWAP, 0,
                          (const char*)NULL, (const char*)NULL);

      swap->execute();
    }
#endif
#endif                                                                
}

void UserInterface :: send_keystroke(int key)
{
    UIObject *obj = ui_objects[focus];
    if (obj) {
        obj->send_keystroke(key);
    }
}

int UserInterface :: pollInactive(void)
{
    return ui_objects[focus]->poll_inactive();
}

void UserInterface :: peel_off(void)
{
    // Peel off will always keep the first object
    if (!focus) {
        return;
    }
    // The underlying object gets focus. If the top object has the cleanup
    // flag set, we destoy it here and it can not be referenced in the
    // calling object.
    ui_objects[focus]->deinit();
    // Note that the object itself still exists, and needs to be cleaned up by the one who created
    // it, unless the auto cleanup flag is set
    if (ui_objects[focus]->needCleanup()) {
        delete ui_objects[focus];
    }
    ui_objects[focus] = NULL;
    focus--;
}

int UserInterface :: pollFocussed(void)
{
	int ret = 0;
    do {
        ret = ui_objects[focus]->poll(ret); // param pass chain

        // Stay in the current window configuration
        if(ret == 0)
            break;

        printf("Object level %d returned %d.\n", focus, (int)ret);

        // Pass non-root positive results to underlying object
        if ((ret > 0) && (focus)) {
            peel_off();
            continue; // pass result
        }

        switch (ret) {
        case MENU_CLOSE:
            // close single layer window and pass to caller (might be cancel code)
            peel_off();
            continue; //  pass to upper layer

        case MENU_HIDE:
        case MENU_EXIT:
            printf("MENU HIDE / EXIT.\n");
            // deinitialize everything, and roll back. Caller solves this, as what happens depends on type of UI.
            return ret;
            
        default:
            printf("ERROR: Return code %d not expected here.\n", ret); // could happen for root
            ret = 0;
            break;
        }

    } while(1);
    return ret;
}

void UserInterface :: appear(void)
{
	host->set_colors(color_bg, color_border);
	set_screen_title();
	for(int i=0;i<=focus;i++) {  // build up
		//printf("Going to (re)init objects %d.\n", i);
		ui_objects[i]->init();
		ui_objects[i]->redraw();
	}
}

void UserInterface :: release_host(void)
{
    for(int i=focus;i>=0;i--) {  // tear down
        ui_objects[i]->deinit();
    }
    doBreak = true;
}

bool UserInterface :: is_available(void)
{
    return available;
}

int UserInterface :: activate_uiobject(UIObject *obj)
{
    if(focus < (MAX_UI_OBJECTS-1)) {
        focus++;
        ui_objects[focus] = obj;
        return 0;
    }

    return -1;
}

bool UserInterface :: has_focus(UIObject *obj)
{
    return (ui_objects[focus] == obj);
}

void UserInterface :: set_screen_title()
{
    int width = screen->get_size_x();
    int height = screen->get_size_y();

    if (logo) {
        screen->clear();
        screen->output("\e6\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x1c");
        screen->output("\x14\x15\x17");
        screen->output("\e1 COMMODORE 64 ");
        screen->output("\e6\x1e\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12");
        screen->move_cursor(0, 1);
        screen->output("\e2\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x1d");
        screen->output("\e6\x18\x16\e2\x19");
        screen->output(" \eR\e1\x1a ULTIMATE \x1a\er ");
        screen->output("\e2\x1f\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b");
    } else {
        int len = title.length();
        int hpos = (width - len) / 2;

        screen->clear();
        screen->move_cursor(hpos, 0);
        screen->output("\eA");
        screen->output(title.c_str());
        screen->output("\eO");
        screen->move_cursor(0, 1);
        screen->repeat('\002', width);
        screen->move_cursor(0, height-1);
        screen->scroll_mode(false);
        screen->repeat('\002', width);
    }
}

/* Blocking variants of our simple objects follow: */
int  UserInterface :: popup(const char *msg, uint8_t flags)
{
    const char *c_button_names[] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
    const char c_button_keys[] = { 'o', 'y', 'n', 'a', 'c' };

    UIPopup *pop = new UIPopup(this, msg, flags, 5, c_button_names, c_button_keys);
    pop->init();
    int ret;
    do {
        ret = pop->poll(0);
    } while(!ret);
    pop->deinit();
    delete pop;
    return ret;
}
    
int  UserInterface :: popup(const char *msg, int count, const char **names, const char *keys)
{
    UIPopup *pop = new UIPopup(this, msg, (1 << (count + 1))-1, count, names, keys);
    pop->init();
    int ret;
    do {
        ret = pop->poll(0);
    } while(!ret);
    pop->deinit();
    delete pop;
    return ret;
}

int UserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
    UIStringBox *box = new UIStringBox(this, msg, buffer, maxlen);
    box->init();
    screen->cursor_visible(1);
    int ret;
    do {
        ret = box->poll(0);
    } while(!ret);
    screen->cursor_visible(0);
    box->deinit();
    delete box;
    return ret;
}

int UserInterface :: string_edit(char *buffer, int maxlen, Window *w, int x, int y)
{
    UIStringEdit *edit = new UIStringEdit(buffer, maxlen);
    edit->init(w, keyboard, x, y, maxlen); // maybe the max len should be limited by the window!
    screen->cursor_visible(1);
    int ret;
    do {
        ret = edit->poll(0);
    } while(!ret);
    screen->cursor_visible(0);
    delete edit;
    return ret;
}

int UserInterface :: choice(const char *msg, const char **choices, int count)
{
    UIChoiceBox *box = new UIChoiceBox(this, msg, choices, count);
    box->init();
    screen->cursor_visible(0);
    int ret;
    do {
        ret = box->poll(0);
    } while(!ret);
    delete box;
    // Return values are 1 based, unless it's an error
    return (ret > 0) ? (ret - 1) : ret;
}

void UserInterface :: show_progress(const char *msg, int steps)
{
    status_box = new UIStatusBox(this, msg, steps);
    status_box->init();
}

void UserInterface :: update_progress(const char *msg, int steps)
{
    status_box->update(msg, steps);
}

void UserInterface :: hide_progress(void)
{
    status_box->deinit();
    delete status_box;
}

void UserInterface :: run_editor(const char *text_buf, int max_len)
{
    Editor *edit = new Editor(this, text_buf, max_len);
    edit->init(screen, keyboard);
    int ret;
    do {
        ret = edit->poll(0);
    } while(!ret);
    edit->deinit();
    delete edit;
}

QueueHandle_t userMessageQueue = 0;

void UserInterface :: postMessage(const char *msg)
{
    if (!userMessageQueue) {
        userMessageQueue = xQueueCreate(4, sizeof(void *));
    }
    if (!userMessageQueue) {
        return;
    }
    // Message handler becomes owner of object
    mstring *message = new mstring(msg);

    xQueueSend(userMessageQueue, &message, 0);
}

mstring *UserInterface :: getMessage(void)
{
    mstring *msg = NULL;

    if (!userMessageQueue) {
        userMessageQueue = xQueueCreate(4, sizeof(void *));
    }
    if (userMessageQueue) {
        xQueueReceive(userMessageQueue, &msg, 0);
    }
    return msg;
}

int UserInterface :: keymapper(int c, keymap_options_t map)
{
    if (navmode == 1) { // WASD cursors enabled
        if (c >= 'A' && c <= 'Z') {
            c |= 0x20; // make uppercase lowercase
        } else {
            switch(c) {
            case 'w': c = KEY_UP; break;
            case 'a': c = KEY_LEFT; break;
            case 's': c = KEY_DOWN; break;
            case 'd': c = KEY_RIGHT; break;
            }
        }
    }
#if COMMODORE
    switch(c) {
    case KEY_F1: c = KEY_TASKS; break;
    case KEY_F3: c = KEY_PAGEUP; break;
    case KEY_F5: c = KEY_PAGEDOWN; break;
    case KEY_F7: c = KEY_HELP; break;
    case KEY_F2: c = KEY_CONFIG; break;
    case KEY_F4: c = KEY_SYSINFO; break;
    case KEY_F6: c = KEY_SEARCH; break;
    }
#else
    switch(c) {
    case KEY_F1: c = KEY_PAGEUP; break;
    case KEY_F3: c = KEY_HELP; break;
    case KEY_F5: c = KEY_TASKS; break;
    case KEY_F7: c = KEY_PAGEDOWN; break;
    case KEY_F2: c = KEY_CONFIG; break;
    case KEY_F4: c = KEY_SYSINFO; break;
    case KEY_F6: c = KEY_SEARCH; break;
    }
#endif
    return c;
}

void UserInterface :: help()
{
    run_editor(helptext, strlen(helptext));
}
