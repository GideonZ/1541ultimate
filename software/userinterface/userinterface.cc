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

/* Configuration */
static const char *colors[] = { "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
                         "Orange", "Brown", "Pink", "Dark Grey", "Mid Grey", "Light Green", "Light Blue", "Light Grey" };
                          
static const char *en_dis4[]    = { "Disabled", "Enabled" };
static const char *itype[]      = { "Freeze", "Overlay on HDMI" };
static const char *cfg_save[]   = { "No", "Ask", "Yes" };

struct t_cfg_definition user_if_config[] = {
#if U64
    { CFG_USERIF_ITYPE,      CFG_TYPE_ENUM,   "Interface Type",       "%s", itype,   0,  1, 0 },
#endif
    { CFG_USERIF_BACKGROUND, CFG_TYPE_ENUM,   "Background color",     "%s", colors,  0, 15, 0 },
    { CFG_USERIF_BORDER,     CFG_TYPE_ENUM,   "Border color",         "%s", colors,  0, 15, 0 },
    { CFG_USERIF_FOREGROUND, CFG_TYPE_ENUM,   "Foreground color",     "%s", colors,  0, 15, 12 },
    { CFG_USERIF_SELECTED,   CFG_TYPE_ENUM,   "Selected Item color",  "%s", colors,  0, 15, 1 },
#if U64
    { CFG_USERIF_SELECTED_BG,CFG_TYPE_ENUM,   "Selected Backgr (Overlay)",  "%s", colors,  0, 15, 6 },
#endif
//    { CFG_USERIF_WORDWRAP,   CFG_TYPE_ENUM,   "Wordwrap text viewer", "%s", en_dis,  0,  1, 1 },
    { CFG_USERIF_HOME_DIR,   CFG_TYPE_STRING, "Home Directory",        "%s", NULL, 0, 31, (int)"" },
    { CFG_USERIF_START_HOME, CFG_TYPE_ENUM,   "Enter Home on Startup", "%s", en_dis4, 0,  1, 0 },
    { CFG_USERIF_CFG_SAVE,   CFG_TYPE_ENUM,   "Auto Save Config",      "%s", cfg_save, 0, 2, 1 },
    { CFG_USERIF_ULTICOPY_NAME, CFG_TYPE_ENUM, "Ulticopy Uses disk name", "%s", en_dis4, 0, 1, 1 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

UserInterface :: UserInterface(const char *title) : title(title)
{
    initialized = false;
    focus = -1;
    host = NULL;
    keyboard = NULL;
    alt_keyboard = NULL;
    screen = NULL;
    doBreak = false;
    available = false;
    color_sel_bg = 0;
    register_store(0x47454E2E, "User Interface Settings", user_if_config);
    effectuate_settings();
}

UserInterface :: ~UserInterface()
{
	printf("Destructing user interface..\n");
    do {
    	ui_objects[focus]->deinit();
    	delete ui_objects[focus--];
    } while(focus>=0);
    printf(" bye UI!\n");
}

void UserInterface :: effectuate_settings(void)
{
    color_border = cfg->get_value(CFG_USERIF_BORDER);
    color_fg     = cfg->get_value(CFG_USERIF_FOREGROUND);
    color_bg     = cfg->get_value(CFG_USERIF_BACKGROUND);
    color_sel    = cfg->get_value(CFG_USERIF_SELECTED);
#if U64
    color_sel_bg = cfg->get_value(CFG_USERIF_SELECTED_BG);
#endif
    config_save  = cfg->get_value(CFG_USERIF_CFG_SAVE);

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
        if (!pollFocussed()) {
            available = false;
            host->releaseScreen();
            break;
        }
        vTaskDelay(3);
    }
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
        } else if (!pollFocussed()) {
            available = false;
            host->releaseScreen();
            host->release_ownership();
            break;
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

int UserInterface :: pollInactive(void)
{
    return ui_objects[focus]->poll_inactive();
}

bool UserInterface :: pollFocussed(void)
{
	int ret = 0;
    do {
        ret = ui_objects[focus]->poll(ret); // param pass chain
        if(!ret) // return value of 0 keeps us in the same state
            break;
        printf("Object level %d returned %d.\n", focus, ret);

        if (host->is_permanent() && (!focus)) {
            return false;
        }
        ui_objects[focus]->deinit();
        if (ret == -2) {
            delete ui_objects[focus];
        }
        if(focus) {
            focus--;
        } else {
        	return false;
        }
    } while(1);
    return true;
}

void UserInterface :: appear(void)
{
	host->set_colors(color_bg, color_border);
	set_screen_title();
	for(int i=0;i<=focus;i++) {  // build up
		//printf("Going to (re)init objects %d.\n", i);
		ui_objects[i]->init(screen, keyboard);
		ui_objects[i]->redraw();
	}
}

void UserInterface :: release_host(void)
{
    for(int i=focus;i>=0;i--) {  // tear down
        ui_objects[i]->deinit();
    }
    host->releaseScreen();

    if (!host->is_permanent()) {
        doBreak = true;
    }
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

void UserInterface :: set_screen_title()
{
    int width = screen->get_size_x();
    int height = screen->get_size_y();

    int len = title.length()-4;
    int hpos = (width - len) / 2;

    screen->clear();
    screen->move_cursor(hpos, 0);
    screen->output(title.c_str());
    screen->move_cursor(0, 1);
	screen->repeat('\002', width);
    screen->move_cursor(0, height-1);
	screen->scroll_mode(false);
	screen->repeat('\002', width);
}
    
/* Blocking variants of our simple objects follow: */
int  UserInterface :: popup(const char *msg, uint8_t flags)
{
    UIPopup *pop = new UIPopup(msg, flags);
    pop->init(screen, keyboard);
    int ret;
    do {
        ret = pop->poll(0);
    } while(!ret);
    pop->deinit();
    return ret;
}
    
int UserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
    UIStringBox *box = new UIStringBox(msg, buffer, maxlen);
    box->init(screen, keyboard);
    screen->cursor_visible(1);
    int ret;
    do {
        ret = box->poll(0);
    } while(!ret);
    screen->cursor_visible(0);
    box->deinit();
    return ret;
}

void UserInterface :: show_progress(const char *msg, int steps)
{
    status_box = new UIStatusBox(msg, steps);
    status_box->init(screen);
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

void UserInterface :: run_editor(const char *text_buf)
{
    Editor *edit = new Editor(this, text_buf);
    edit->init(screen, keyboard);
    int ret;
    do {
        ret = edit->poll(0);
    } while(!ret);
    edit->deinit();
}

int UserInterface :: enterSelection()
{
#ifndef NO_FILE_ACCESS
	// because we know that the command can only be caused by a TreeBrowser, we can safely cast
	TreeBrowser *browser = (TreeBrowser *)(get_root_object());
	if (browser) {
		if (browser->state) {
			if(browser->state->into2()) {
			    return -1;
			}
			return 0;
		}
	}
#endif
	return -1;
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
