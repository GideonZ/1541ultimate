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
ConfigBrowser :: ConfigBrowser(UserInterface *ui, Browsable *root) : TreeBrowser(ui, root)
{
    printf("Constructor config browser\n");
}

ConfigBrowser :: ~ConfigBrowser()
{
    printf("Destructing config browser..\n");
}

void ConfigBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;
	window = new Window(screen, 0, 2, 40, 20);
	window->draw_border();
	keyb = k;
    state = new ConfigBrowserState(root, this, 0);
    state->reload();
	state->do_refresh();
}

ConfigBrowserState :: ConfigBrowserState(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

ConfigBrowserState :: ~ConfigBrowserState()
{

}

void ConfigBrowserState :: into(void)
{
	if(!under_cursor)
		return;

	deeper = new ConfigBrowserState(under_cursor, browser, level+1);
    browser->state = deeper;
    deeper->previous = this;

	printf("Going deeper into = %s\n", under_cursor->getName());
    int child_count = under_cursor->getSubItems(deeper->children);
    if(child_count < 1) {
    	browser->state = this;
    	delete deeper;
    }
}

void ConfigBrowserState :: level_up(void)
{
	if(level == 1) { // going to level 0, we need to store in flash
		ConfigStore *st = ((BrowsableConfigStore *)(previous->under_cursor))->getStore();
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
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    it->store->dirty = true;
    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
            browser->context(it->value);
            break;
        case CFG_TYPE_STRING:
            if(browser->user_interface->string_box(it->definition->item_text, it->string, it->definition->max))
            	update_selected();
            break;
        default:
            break;
    }
}

void ConfigBrowserState :: increase(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
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
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
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
    
int ConfigBrowser :: handle_key(int c)
{
    int ret = 0;
    
    switch(c) {
        case KEY_F8: // exit
        case KEY_BREAK: // runstop
        	if(state->level == 1) { // going to level 0, we need to store in flash
        		ConfigStore *st = ((BrowsableConfigStore *)state->previous->under_cursor)->getStore();
        		if(st->dirty) {
        			st->write();
        			st->effectuate();
        		}
        	}
        	ret = -2;
            break;
        case KEY_DOWN: // down
            state->down(1);
            break;
        case KEY_UP: // up
            state->up(1);
            break;
        case KEY_F1: // F1 -> page up
        case KEY_PAGEUP:
            state->up(window->get_size_y()/2);
            break;
        case KEY_F7: // F7 -> page down
        case KEY_PAGEDOWN:
            state->down(window->get_size_y()/2);
            break;
        case KEY_SPACE: // space = select
        case KEY_RETURN: // CR = select
            if(state->level==0)
                state->into();
            else
                state->change();
            break;
        case KEY_RIGHT: // right
            if(state->level==0)
                state->into();
            else
                state->increase();
            break;
        case KEY_LEFT: // left
            if(state->level==0)
                ret = -1; // leave
            else
                state->decrease();
            break;
		case KEY_BACK: // del
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

/*
// ConfigContextMenu
ConfigContextMenu :: ConfigContextMenu(UserInterface *ui, TreeBrowserState *state, int initial, int y) :
	ContextMenu(ui, state, initial, y)
{
}
*/
