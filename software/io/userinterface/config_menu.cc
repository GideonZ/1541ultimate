extern "C" {
    #include "small_printf.h"
}
#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "config.h"
#include "config_menu.h"

/************************/
/* ConfigBrowser Object */
/************************/
ConfigBrowser :: ConfigBrowser()
{
    printf("Constructor config browser\n");
    delete state; // we don't use the standard state, but our own state.
    state = new ConfigBrowserState(&config_manager, this, 0);
}

ConfigBrowser :: ~ConfigBrowser()
{
    printf("Destructing config browser..\n");
}

void ConfigBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	window = new Screen(screen, 0, 2, 40, 20);
	window->draw_border();
	keyb = k;
	state->do_refresh();
}

ConfigBrowserState :: ConfigBrowserState(PathObject *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

ConfigBrowserState :: ~ConfigBrowserState()
{

}

void ConfigBrowserState :: into(void)
{
	selected->fetch_children();
	deeper = new ConfigBrowserState(selected, browser, level+1);
    browser->state = deeper;
    deeper->previous = this;
}

void ConfigBrowserState :: level_up(void)
{
	if(level == 1) { // going to level 0, we need to store in flash
		ConfigStore *st = (ConfigStore *)previous->selected;
		if(st->dirty) {
			st->write();
			st->effectuate();
		}
	}
    browser->state = previous;
    previous->refresh = true;
    previous = NULL; // unlink;
    delete this;
}
           
void ConfigBrowserState :: change(void)
{
    ConfigItem *it = (ConfigItem *)selected;
    it->store->dirty = true;
    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
            browser->context(it->value);
            break;
        case CFG_TYPE_STRING:
            if(user_interface->string_box(it->definition->item_text, it->string, it->definition->max))
            	update_selected();
            break;
        default:
            break;
    }
}

void ConfigBrowserState :: increase(void)
{
    ConfigItem *it = (ConfigItem *)selected;
    it->store->dirty = true;
    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
        case CFG_TYPE_VALUE:
            if(it->value < it->definition->max)
                it->value++;
            else
                it->value = it->definition->min; // circular
            update_selected();
            break;
            
        default:
            break;
    }
}
    
void ConfigBrowserState :: decrease(void)
{
    ConfigItem *it = (ConfigItem *)selected;
    it->store->dirty = true;
    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
        case CFG_TYPE_VALUE:
            if(it->value > it->definition->min)
                it->value--;
            else
                it->value = it->definition->max; // circular
            update_selected();
            break;
        default:
            //level_up();
            break;
    }
}
    
int ConfigBrowser :: handle_key(char c)
{
    int ret = 0;
    
    switch(c) {
        case 0x8C: // exit
        case 0x03: // runstop
        	if(state->level == 1) { // going to level 0, we need to store in flash
        		ConfigStore *st = (ConfigStore *)state->previous->selected;
        		if(st->dirty) {
        			st->write();
        			st->effectuate();
        		}
        	}
        	ret = -2;
            break;
//        case 0x8C: // exit
//            push_event(e_unfreeze, 0);
//            push_event(e_terminate, 0);
//            break;
        case 0x11: // down
            state->down(1);
            break;
        case 0x91: // up
            state->up(1);
            break;
        case 0x85: // F1 -> page up
            state->up(window->get_size_y()/2);
            break;
        case 0x88: // F7 -> page down
            state->down(window->get_size_y()/2);
            break;
        case 0x20: // space = select
        case 0x0D: // CR = select
            if(state->level==0)
                state->into();
            else
                state->change();
            break;
        case 0x1D: // right
            if(state->level==0)
                state->into();
            else
                state->increase();
            break;
        case 0x9D: // left
            if(state->level==0)
                ret = -1; // leave
            else
                state->decrease();
            break;
		case 0x14: // del
            if(state->level==0)
                ret = -1; // leave
            else
            	state->level_up();
			break;
        default:
            printf("Unhandled key: %b\n", c);
    }    
    return ret;
}

void ConfigBrowserState :: unhighlight()
{
    browser->window->move_cursor(0, selected_line);
    browser->window->set_color(user_interface->color_fg); // highlighted
    browser->window->output_line(selected->get_display_string());
}
    
void ConfigBrowserState :: highlight()
{
    browser->window->move_cursor(0, selected_line);
    browser->window->set_color(user_interface->color_sel); // highlighted
    browser->window->output_line(selected->get_display_string());
}
