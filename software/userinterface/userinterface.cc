#include "userinterface.h"

/* Configuration */
const char *colors[] = { "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
                         "Orange", "Brown", "Pink", "Dark Grey", "Mid Grey", "Light Green", "Light Blue", "Light Grey" };
                          
#define CFG_USERIF_BACKGROUND 0x01
#define CFG_USERIF_BORDER     0x02
#define CFG_USERIF_FOREGROUND 0x03
#define CFG_USERIF_SELECTED   0x04
#define CFG_USERIF_WORDWRAP   0x05

struct t_cfg_definition user_if_config[] = {
    { CFG_USERIF_BACKGROUND, CFG_TYPE_ENUM,   "Background color",     "%s", colors,  0, 15, 0 },
    { CFG_USERIF_BORDER,     CFG_TYPE_ENUM,   "Border color",         "%s", colors,  0, 15, 0 },
    { CFG_USERIF_FOREGROUND, CFG_TYPE_ENUM,   "Foreground color",     "%s", colors,  0, 15, 15 },
    { CFG_USERIF_SELECTED,   CFG_TYPE_ENUM,   "Selected Item color",  "%s", colors,  0, 15, 1 },
//    { CFG_USERIF_WORDWRAP,   CFG_TYPE_ENUM,   "Wordwrap text viewer", "%s", en_dis,  0,  1, 1 },
    { CFG_TYPE_END,           CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }         
};


IndexedList<UserInterface *>user_interfaces(2, 0);

void poll_user_interface(Event &e)
{
	for(int i=0;i<user_interfaces.get_elements();i++) {
		user_interfaces[i]->handle_event(e);
	}
}

UserInterface :: UserInterface()
{
    initialized = false;
    focus = -1;
    state = ui_idle;
    host = NULL;
    keyboard = NULL;
    screen = NULL;

    MainLoop :: removePollFunction(poll_user_interface); // quick hack to make sure there is only one in the list
    MainLoop :: addPollFunction(poll_user_interface);
    user_interfaces.append(this);

    register_store(0x47454E2E, "User Interface Settings", user_if_config);
    effectuate_settings();
}

UserInterface :: ~UserInterface()
{
	printf("Destructing user interface..\n");
    MainLoop :: removePollFunction(poll_user_interface);
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

    push_event(e_refresh_browser);
}
    
void UserInterface :: init(GenericHost *h)
{
    host = h;
    keyboard = h->getKeyboard();
    screen = NULL;
    initialized = true;
}

void UserInterface :: set_screen(Screen *s)
{
    screen = s;
}

void UserInterface :: handle_event(Event &e)
{
/*
    if(!initialized)
        return;
*/
    int ret, i;

    switch(state) {
        case ui_idle:
        	if ((e.type == e_button_press)||(e.type == e_freeze)) {
        		host->take_ownership(this);
                host->set_colors(color_bg, color_border);
                screen = host->getScreen();
                set_screen_title();
                for(i=0;i<=focus;i++) {  // build up
                    //printf("Going to (re)init objects %d.\n", i);
                    ui_objects[i]->init(screen, keyboard);
                }
                if(e.param)
                    state = ui_host_permanent;
                else
                    state = ui_host_owned;

                push_event(e_enter_menu, 0);
            }
            else if((e.type == e_invalidate) && (focus >= 0)) { // even if we are inactive, the tree browser should execute this!!
            	ui_objects[0]->poll(0, e);
            }
            break;

        case ui_host_owned:
        	if ((e.type == e_button_press)||(e.type == e_unfreeze)) {
        		release_host(); // FIXME: No longer a way to pass cartridge mode here
                host->release_ownership();
                break;
            }
        // fall through from host_owned:
        case ui_host_permanent:
            ret = 0;
            do {
                ret = ui_objects[focus]->poll(ret, e); // param pass chain
                if(!ret) // return value of 0 keeps us in the same state
                    break;
                printf("Object level %d returned %d.\n", focus, ret);
                ui_objects[focus]->deinit();
                if(focus) {
                    focus--;
                    //printf("Restored focus to level %d.\n", focus);
                }
                else {
                    host->releaseScreen();
                    host->release_ownership();
                    state = ui_idle;
                    break;
                }
            } while(1);    
            break;
        default:
            break;
    }            
}

void UserInterface :: release_host(void)
{
	for(int i=focus;i>=0;i--) {  // tear down
        ui_objects[i]->deinit();
    }
    host->releaseScreen();
    state = ui_idle;
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
    static char title[48];
    // precondition: screen is cleared.  // \020 = alpha \021 = beta
    sprintf(title, "\eA    **** 1541 Ultimate %s (%b) ****\eO", APPL_VERSION, getFpgaVersion());
    screen->move_cursor(0,0);
    screen->output(title);
    screen->move_cursor(0,1);
    for(int i=0;i<40;i++)
        screen->output('\002');
    screen->move_cursor(0,24);
	screen->scroll_mode(false);
    for(int i=0;i<40;i++)
        screen->output('\002');
    screen->move_cursor(32,24);
	screen->output("\eAF3=Help\eO");
}
    
/* Blocking variants of our simple objects follow: */
int  UserInterface :: popup(const char *msg, uint8_t flags)
{
	Event e(e_nop, 0, 0);
    UIPopup *pop = new UIPopup(msg, flags);
    pop->init(screen, keyboard);
    int ret;
    do {
        ret = pop->poll(0, e);
    } while(!ret);
    pop->deinit();
    return ret;
}
    
int UserInterface :: string_box(const char *msg, char *buffer, int maxlen)
{
	Event e(e_nop, 0, 0);
    UIStringBox *box = new UIStringBox(msg, buffer, maxlen);
    box->init(screen, keyboard);
    screen->cursor_visible(1);
    int ret;
    do {
        ret = box->poll(0, e);
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
	Event e(e_nop, 0, 0);
    Editor *edit = new Editor(this, text_buf);
    edit->init(screen, keyboard);
    int ret;
    do {
        ret = edit->poll(0, e);
    } while(!ret);
    edit->deinit();
}
