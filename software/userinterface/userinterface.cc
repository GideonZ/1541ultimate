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
const char *colors[] = { "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
                         "Orange", "Brown", "Pink", "Dark Grey", "Mid Grey", "Light Green", "Light Blue", "Light Grey" };
                          
const char *en_dis4[]    = { "Disabled", "Enabled" };

struct t_cfg_definition user_if_config[] = {
    { CFG_USERIF_BACKGROUND, CFG_TYPE_ENUM,   "Background color",     "%s", colors,  0, 15, 0 },
    { CFG_USERIF_BORDER,     CFG_TYPE_ENUM,   "Border color",         "%s", colors,  0, 15, 0 },
    { CFG_USERIF_FOREGROUND, CFG_TYPE_ENUM,   "Foreground color",     "%s", colors,  0, 15, 15 },
    { CFG_USERIF_SELECTED,   CFG_TYPE_ENUM,   "Selected Item color",  "%s", colors,  0, 15, 1 },
//    { CFG_USERIF_WORDWRAP,   CFG_TYPE_ENUM,   "Wordwrap text viewer", "%s", en_dis,  0,  1, 1 },
    { CFG_USERIF_HOME_DIR,   CFG_TYPE_STRING, "Home Directory",        "%s", NULL, 0, 31, (int)"" },
    { CFG_USERIF_START_HOME, CFG_TYPE_ENUM,   "Enter Home on Startup", "%s", en_dis4, 0,  1, 0 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};

UserInterface :: UserInterface(const char *title) : title(title)
{
    initialized = false;
    focus = -1;
    state = ui_idle;
    host = NULL;
    keyboard = NULL;
    alt_keyboard = NULL;
    screen = NULL;

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
    	state = ui_host_permanent;
    	appear();
    }
}

void UserInterface :: set_screen(Screen *s)
{
    screen = s;
}

void UserInterface :: run(void)
{
#ifndef NO_FILE_ACCESS
    while(1) {
		switch(state) {
			case ui_idle:
				if (!host->hasButton()) {
					state = ui_host_remote;
					host->take_ownership(this);
					appear();
					break;
				}
				if (host->exists() && host->buttonPush()) {
                    if(buttonDownFor(1000)) {
                        swapDisk();
                    } else {
                		state = ui_host_owned;
                		host->take_ownership(this);
                        appear();
                    }
                    break;
				}
				switch(system_usb_keyboard.getch()) {
				case KEY_SCRLOCK:
					state = ui_host_owned;
					host->take_ownership(this);
                    appear();
                    break;
				case 0x04: // CTRL-D
                    swapDisk();
                    break;
				}
				break;

			case ui_host_owned:
				if (!host->exists()) {
					state = ui_idle;
				} else if (host->buttonPush()) {
					state = ui_idle;
					release_host();
					host->release_ownership();
				} else if (!pollFocussed()) {
					host->releaseScreen();
					host->release_ownership();
					state = ui_idle;
				}
				break;

			case ui_host_remote:
				if (!pollFocussed()) {
					host->releaseScreen();
					return; // User Interface ceases to exist
				}
				break;

			case ui_host_permanent:
				if (host->is_accessible()) {
					if (!pollFocussed()) {
						host->release_ownership();
					}
				} else {
					if (system_usb_keyboard.getch() == KEY_SCRLOCK) {
						host->take_ownership(0);
						appear();
					}
				}
				break;

			default:
				return; // User Interface ceases to exist
		}
		vTaskDelay(3);
    }
#endif
}

#ifndef RECOVERYAPP
bool UserInterface :: buttonDownFor(uint32_t ms)
{
    bool down = false;
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

bool UserInterface :: pollFocussed(void)
{
	int ret = 0;
    do {
        ret = ui_objects[focus]->poll(ret); // param pass chain
        if(!ret) // return value of 0 keeps us in the same state
            return true;
        printf("Object level %d returned %d.\n", focus, ret);
        ui_objects[focus]->deinit();
        if(focus) {
            focus--;
        } else {
        	return false;
        }
    } while(1);
}

void UserInterface :: appear(void)
{
	host->set_colors(color_bg, color_border);
	set_screen_title();
	for(int i=0;i<=focus;i++) {  // build up
		//printf("Going to (re)init objects %d.\n", i);
		ui_objects[i]->init(screen, keyboard);
	}
}

void UserInterface :: release_host(void)
{
	for(int i=focus;i>=0;i--) {  // tear down
        ui_objects[i]->deinit();
    }
    host->releaseScreen();

    // This bit of state machine needs to be here, because the
    // browser doesn't know that a command actually causes the
    // C64 to unfreeze.

    if (!host->hasButton()) {
    	state = ui_host_permanent;
    } else {
    	state = ui_idle;
    }

}

bool UserInterface :: is_available(void)
{
    return (state != ui_idle);
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
			browser->state->into2();
			return 0;
		}
	}
#endif
	return -1;
}
