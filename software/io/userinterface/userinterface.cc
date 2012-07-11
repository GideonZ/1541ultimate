#include "userinterface.h"
#include "editor.h"

/* Configuration */
char *colors[] = { "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow",
                   "Orange", "Brown", "Pink", "Dark Grey", "Mid Grey", "Light Green", "Light Blue", "Light Grey" };

                          
static char *en_dis[] = { "Disabled", "Enabled" };

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

const char *c_button_names[NUM_BUTTONS] = { " Ok ", " Yes ", " No ", " All ", " Cancel " };
const char c_button_keys[NUM_BUTTONS] = { 'o', 'y', 'n', 'c', 'a' };
const int c_button_widths[NUM_BUTTONS] = { 4, 5, 4, 5, 8 };

/* COMPILER BUG: static data members are not declared, and do not appear in the linker */

void poll_user_interface(Event &e)
{
	user_interface->handle_event(e);
}

UserInterface :: UserInterface()
{
    initialized = false;
    poll_list.append(&poll_user_interface);
    focus = -1;
    state = ui_idle;
    current_path = NULL;
    host = NULL;
    keyboard = NULL;
    screen = NULL;

    register_store(0x47454E2E, "User Interface Settings", user_if_config);
    effectuate_settings();
}

UserInterface :: ~UserInterface()
{
//	if(host->has_stopped())
//		printf("WARNING: Host is still frozen!!\n");

	printf("Destructing user interface..\n");
	poll_list.remove(&poll_user_interface);
    do {
    	ui_objects[focus]->deinit();
    	delete ui_objects[focus--];
    } while(focus>=0);

//    delete browser_path;
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
    
void UserInterface :: init(GenericHost *h, Keyboard *k)
{
    host = h;
    keyboard = k;
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
    static BYTE button_prev;
    int ret, i;

    switch(state) {
        case ui_idle:
            if (e.type == e_button_press) {
				// root.dump();
                host->freeze();
                host->set_colors(color_bg, color_border);
                screen = new Screen(host->get_screen(), host->get_color_map(), 40, 25);
                set_screen_title();
                for(i=0;i<=focus;i++) {  // build up
                    //printf("Going to (re)init objects %d.\n", i);
                    ui_objects[i]->init(screen, keyboard);
                }
                if(e.param)
                    state = ui_host_permanent;
                else
                    state = ui_host_owned;

                if(ITU_FPGA_VERSION < MINIMUM_FPGA_REV) {
                    popup("WARNING: FPGA too old..", BUTTON_OK);
                }
            }
            else if((e.type == e_invalidate) && (focus >= 0)) { // even if we are inactive, the tree browser should execute this!!
            	ui_objects[0]->poll(0, e);
            }
            break;

        case ui_host_owned:
        	if ((e.type == e_button_press)||(e.type == e_unfreeze)) {
                for(i=focus;i>=0;i--) {  // tear down
                    ui_objects[i]->deinit();
                }
                delete screen;
                host->unfreeze(e); // e.param, (cart_def *)e.object
                state = ui_idle;
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
                    delete screen;
                    host->unfreeze((Event &)c_empty_event);
                    state = ui_idle;
                    break;
                }
            } while(1);    
            break;
        default:
            break;
    }            

    BYTE buttons = ITU_IRQ_ACTIVE & ITU_BUTTONS;
    if((buttons & ~button_prev) & ITU_BUTTON1) {
        if(state == ui_idle) { // this is nasty, but at least we know that it's executed FIRST
            // and that it has no effect when no copper exists.
            push_event(e_copper_capture, 0);
        }
        push_event(e_button_press, 0);
    }
    button_prev = buttons;
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
    sprintf(title, "\033\021    **** 1541 Ultimate %s (%b) ****\033\037\n", APPL_VERSION, ITU_FPGA_VERSION);
    screen->move_cursor(0,0);
    screen->output(title);
    for(int i=0;i<40;i++)
        screen->output('\002');
}
    
/* Blocking variants of our simple objects follow: */
int  UserInterface :: popup(char *msg, BYTE flags)
{
	Event e(e_nop, 0, 0);
    UIPopup *pop = new UIPopup(msg, flags);
    printf("popup created.\n");
    pop->init(screen, keyboard);
    printf("popup initialized.\n");
    int ret;
    do {
        ret = pop->poll(0, e);
    } while(!ret);
    pop->deinit();
    return ret;
}
    
int UserInterface :: string_box(char *msg, char *buffer, int maxlen)
{
	Event e(e_nop, 0, 0);
    UIStringBox *box = new UIStringBox(msg, buffer, maxlen);
    box->init(screen, keyboard);
    int ret;
    do {
        ret = box->poll(0, e);
    } while(!ret);
    box->deinit();
    return ret;
}

int  UserInterface :: show_status(char *msg, int steps)
{
    status_box = new UIStatusBox(msg, steps);
    status_box->init(screen);
}

int  UserInterface :: update_status(char *msg, int steps)
{
    status_box->update(msg, steps);
}

int  UserInterface :: hide_status(void)
{
    status_box->deinit();
    delete status_box;
}

void UserInterface :: run_editor(char *text_buf)
{
	Event e(e_nop, 0, 0);
    Editor *edit = new Editor(text_buf);
    edit->init(screen, keyboard);
    int ret;
    do {
        ret = edit->poll(0, e);
    } while(!ret);
    edit->deinit();
}

/* User Interface Objects */
/* Popup */
UIPopup :: UIPopup(char *msg, BYTE btns) : message(msg)
{
    buttons = btns;
}    

void UIPopup :: init(Screen *screen, Keyboard *k)
{
    // first, determine how wide our popup needs to be
    int button_width = 0;
    int message_width = message.length();
    keyboard = k;

    BYTE b = buttons;
    btns_active = 0;
    for(int i=0;i<NUM_BUTTONS;i++) {
        if(b & 1) {
            btns_active ++;
            button_width += c_button_widths[i];
        }
        b >>= 1;
    }
        
/*
    if(!btns_active) {
        wait_ms(3000);
        done = true;
        return;
    }
*/

    int window_width = message_width;
    if (button_width > message_width)
        window_width = button_width;

    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;
    int x_b = (window_width - button_width) / 2;

    window = new Screen(screen, x1, y1, window_width+2, 5);
    window->draw_border();
    window->no_scroll();
    window->move_cursor(x_m, 0);
    window->output(message.c_str());
    window->move_cursor(x_b, 2);
        
    int j=0;
    b = buttons;
    for(int i=0;i<NUM_BUTTONS;i++) {
        if(b & 1) {
            window->output((char *)c_button_names[i]);
            button_pos[j] = x_b;
            x_b += c_button_widths[i];
            button_len[j] = c_button_widths[i];
            button_key[j++] = c_button_keys[i];
        }
        b >>= 1;
    }

    active_button = 0; // we can change this
    
    window->make_reverse(button_pos[active_button], 2, button_len[active_button]);
    keyboard->clear_buffer();
}

int UIPopup :: poll(int dummy, Event &e)
{
    char c = keyboard->getch();
    
    for(int i=0;i<btns_active;i++) {
        if(c == button_key[i]) {
            return (1 << i);
        }
    }
    if((c == 0x0D)||(c == 0x20)) {
		for(int i=0,j=0;i<NUM_BUTTONS;i++) {
			if(buttons & (1 << i)) {
				if(active_button == j)
					return (1 << i);
				j++;
			}
		}
        return 0;
    }
    if(c == 0x1D) {
        window->make_reverse(button_pos[active_button], 2, button_len[active_button]);
        active_button ++;
        if(active_button >= btns_active)
            active_button = btns_active-1;
        window->make_reverse(button_pos[active_button], 2, button_len[active_button]);
    } else if(c == 0x9D) {
        window->make_reverse(button_pos[active_button], 2, button_len[active_button]);
        active_button --;
        if(active_button < 0)
            active_button = 0;
        window->make_reverse(button_pos[active_button], 2, button_len[active_button]);
    }
    return 0;
}    

void UIPopup :: deinit()
{
	delete window;
}


UIStringBox :: UIStringBox(char *msg, char *buf, int max) : message(msg)
{
    buffer = buf;
    max_len = max;
}

void UIStringBox :: init(Screen *screen, Keyboard *keyb)
{
    int message_width = message.length();
    int window_width = message_width;
    if (max_len > message_width)
        window_width = max_len;
    window_width += 2; // compensate for border
    
    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    keyboard = keyb;
    window = new Screen(screen, x1, y1, window_width, 5);
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output_line(message.c_str());
    window->move_cursor(0, 2);

    scr = window->get_pointer();
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
    char *sp = buffer;
    char *dp = scr;
    for(;*sp;) {
        *(dp++) = *(sp++);
    } *(dp) = '\0';
//    strcpy(scr, buffer); // Goes wrong because of big-endianness CPU
/// Default to old string
    
    // place cursor
    scr[cur] |= 0x80;
}

int UIStringBox :: poll(int dummy, Event &e)
{
    char key;
    int i;

    key = keyboard->getch();
    if(!key)
        return 0;

    switch(key) {
    case 0x0D: // CR
        scr[cur] &= 0x7F;
        for(i=0;i<len;i++)
            buffer[i] = scr[i];
        buffer[i] = 0;
        if(!i)
            return -1; // cancel
        return 1; // done
    case 0x9D: // left
        if (cur > 0) {
            scr[cur] &= 0x7F;
            cur--;
            scr[cur] |= 0x80;
        }                
        break;
    case 0x1D: // right
        if (cur < len) {
            scr[cur] &= 0x7F;
            cur++;
            scr[cur] |= 0x80;
        }
        break;
    case 0x14: // backspace
        if (cur > 0) {
            scr[cur] &= 0x7F;
            cur--;
            len--;
            for(i=cur;i<len;i++) {
                scr[i] = scr[i+1];
            } scr[i] = 0x20;
            scr[cur] |= 0x80;
        }
        break;
    case 0x93: // clear
        for(i=0;i<max_len;i++)
            scr[i] = 0x20;
        len = 0;
        cur = 0;
        scr[cur] |= 0x80;
        break;
    case 0x94: // del
        if(cur < len) {
            len--;
            for(i=cur;i<len;i++) {
                scr[i] = scr[i+1];
            } scr[i] = 0x20;
            scr[cur] |= 0x80;
        }
        break;
    case 0x13: // home
        scr[cur] &= 0x7F;
        cur = 0;
        scr[cur] |= 0x80;
        break;        
    case 0x11: // down = end
        scr[cur] &= 0x7F;
        cur = len;
        scr[cur] |= 0x80;
        break;
    case 0x03: // break
        return -1; // cancel
    default:
        if ((key < 32)||(key > 127)) {
//                printf("unknown char: %02x.\n", key);
            break;
        }
        if (len < max_len) {
            scr[cur] &= 0x7F;
            for(i=len-1; i>=cur; i--) { // insert if necessary
                scr[i+1] = scr[i];
            }
            scr[cur] = key;
            cur++;
            scr[cur] |= 0x80;
            len++;
        }
        break;
    }
    return 0; // still busy
}

void UIStringBox :: deinit(void)
{
    delete window;
}

/* Status Box */
UIStatusBox :: UIStatusBox(char *msg, int steps) : message(msg)
{
    total_steps = steps;
    progress = 0;
}
    
void UIStatusBox :: init(Screen *screen)
{
    int window_width = 32;
    int message_width = message.length();
    int x1 = (screen->get_size_x() - window_width) / 2;
    int y1 = (screen->get_size_y() - 5) / 2;
    int x_m = (window_width - message_width) / 2;

    window = new Screen(screen, x1, y1, window_width, 5);
    window->draw_border();
    window->move_cursor(x_m, 0);
    window->output_line(message.c_str());
    window->move_cursor(0, 2);
}

void UIStatusBox :: deinit(void)
{
    delete window;
}

void UIStatusBox :: update(char *msg, int steps)
{
    static char percent[8];
    static char bar[40];
    progress += steps;

    if(msg) {
        message = msg;
        window->clear();
        window->output_line(message.c_str());
    }
    
    memset(bar, 32, 36);
    window->move_cursor(0, 2);
    window->reverse_mode(1);
    int terminate = (32 * progress) / total_steps;
    if(terminate > 32)
        terminate = 32;
    bar[terminate] = 0; // terminate
    window->output_line(bar);

//    sprintf(percent, "%d%", (100 * progress) / total_steps);
//    window->output(percent);
}
