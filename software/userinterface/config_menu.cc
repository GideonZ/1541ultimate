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
	window = new Window(screen, (screen->get_size_x() - 40) >> 1, 2, 40, screen->get_size_y()-3);
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

    int error;
    printf("Going deeper into = %s\n", under_cursor->getName());
	deeper->children = under_cursor->getSubItems(error);
	int child_count = deeper->children->get_elements();
    if(child_count < 1) {
    	browser->state = this;
    	delete deeper;
    }
}

void ConfigBrowserState :: level_up(void)
{
    if (level == 1) { // going to level 0, we need to store in flash
        ConfigStore *st = ((BrowsableConfigStore *) (previous->under_cursor))->getStore();
        st->at_close_config();
    }
    browser->state = previous;
    previous->refresh = true;
    previous = NULL; // unlink;
    delete this;
}
           
void ConfigBrowserState :: change(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    char buffer[80];
    int max;
    t_cfg_func func;

    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
            browser->context(it->getValue() - it->definition->min);
            break;
        case CFG_TYPE_STRING:
            max = it->definition->max;
            if (max > 79)
                max = 79;
            strncpy(buffer, it->getString(), max);
            if(browser->user_interface->string_box(it->get_item_name(), buffer, max)) {
                it->setString(buffer);
                update_selected();
            }
            break;
        case CFG_TYPE_FUNC:
            refresh = true;
            func = (t_cfg_func)(it->definition->items);
            func(browser->user_interface);
            break;
        default:
            break;
    }
}

void ConfigBrowserState :: increase(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    it->next(1);
    update_selected();
}
    
void ConfigBrowserState :: decrease(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    it->previous(1);
    update_selected();
}
    
void ConfigBrowser :: on_exit(void)
{
    if (user_interface->config_save == 0) {
        return;
    }
    bool stale = false;
    IndexedList<ConfigStore *> *storeList = ConfigManager :: getConfigManager()->getStores();
    for (int i=0; i < storeList->get_elements(); i++) {
        if ((*storeList)[i]->is_flash_stale()) {
            stale = true;
            break;
        }
    }
    if (!stale) {
        return;
    }
    bool write = false;
    if (user_interface->config_save == 1) { // ask
        if (user_interface->popup("Save changes to Flash?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            write = true;
        }
    } else {
        write = true; // must be 2 (always)
    }

    if (write) {
        for (int i=0; i < storeList->get_elements(); i++) {
            if ((*storeList)[i]->is_flash_stale()) {
                (*storeList)[i]->write();
                break;
            }
        }
    }
}

int ConfigBrowser :: handle_key(int c)
{
    int ret = 0;
    
    BrowsableConfigRoot *br;
    switch(c) {
        case KEY_F8: // exit
        case KEY_BREAK: // runstop
        case KEY_ESCAPE:
            if (state->level == 1) { // going to level 0
                ConfigStore *st = ((BrowsableConfigStore *) state->previous->under_cursor)->getStore();
                st->at_close_config();
            }
            // check if we need to save to flash
            on_exit();
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
            if(state->level==0) {
                on_exit();
                ret = -2; // leave
            } else
                state->decrease();
            break;
		case KEY_BACK: // del
            if(state->level==0) {
                on_exit();
                ret = -2; // leave
            } else
            	state->level_up();
			break;
        default:
            printf("Unhandled key: %b\n", c);
    }    
    return ret;
}

