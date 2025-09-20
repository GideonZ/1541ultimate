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
ConfigBrowser :: ConfigBrowser(UserInterface *ui, Browsable *root, int level) : TreeBrowser(ui, root)
{
    printf("Constructor config browser\n");
    setCleanup();
    has_path = false;
    start_level = level;
    state = new ConfigBrowserState(root, this, level);
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
    // printf("Going deeper into = %s\n", under_cursor->getName());
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
        previous->under_cursor->event(BR_EVENT_OUT);
    }
    browser->state = previous;
    if (previous) {
        previous->refresh = true;
        previous = NULL; // unlink;
    }
    delete this;
}
           
void ConfigBrowserState :: change(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    char buffer[80];
    int max;
    t_cfg_func func;

    if (!it->isEnabled()) {
        return;
    }

    switch(it->definition->type) {
        case CFG_TYPE_ENUM:
            browser->context(it->getValue() - it->definition->min);
            break;
        case CFG_TYPE_STRFUNC:
            browser->context(0);
            break;
        case CFG_TYPE_VALUE:
        	if ((it->definition->max - it->definition->min) < 40) { // was 30, but days of the month then becomes an exception.. :-/
                browser->context(it->getValue() - it->definition->min);
        	}
            break;
        case CFG_TYPE_STRING:
        case CFG_TYPE_STRPASS:
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
            func(browser->user_interface, it);
            break;
        default:
            break;
    }
}

void ConfigBrowserState :: increase(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    if (!it->isEnabled()) {
        return;
    }
    if (it->next(1)) {
        refresh = true;
    } else {
        update_selected();
    }
}
    
void ConfigBrowserState :: decrease(void)
{
    ConfigItem *it = ((BrowsableConfigItem *)under_cursor)->getItem();
    if (!it->isEnabled()) {
        return;
    }
    if (it->previous(1)) {
        refresh = true;
    } else {
        update_selected();
    }
}
    
void ConfigBrowserState :: on_close(void)
{
    if (level == 1) { // only at level 1, we know that our current node is of the type BrowsableConfigStore
        node->event(BR_EVENT_CLOSE);
    }
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
            }
        }
    }
}

static const char *helptext_ult =
        "Setup Menu Help\n\n"
        "Cursor Keys:Up/Left/Down/Right\n"
        "  (Up/Down) Selection up/down\n"
        "  (Left)    Go one level up\n"
        "            leave directory or disk\n"
        "  (Right)   Enter selected item\n\n"
        "SPACE /     Select / change item\n"
        "  RETURN\n"
        "+ / -       Increase/Decrease value\n"
        "F1          Page Up\n"
        "F7          Page Down\n"
        "F3          Show this help text\n"
        "RUN/STOP:   Exit\n";

static const char *helptext_wasd =
        "Setup Menu Help\n\n"
        "WASD:       Up/Left/Down/Right\n"
        "Cursor Keys:Up/Left/Down/Right\n"
        "  (Up/Down) Selection up/down\n"
        "  (Left)    Go one level up\n"
        "            leave directory or disk\n"
        "  (Right)   Enter selected item\n\n"
        "SPACE /     Select / change item\n"
        "  RETURN\n"
        "+ / -       Increase/Decrease value\n"
        "F3          Page Up\n"
        "F5          Page Down\n"
        "F7          Show this help text\n"
        "RUN/STOP:   Exit\n";

int ConfigBrowser :: handle_key(int c)
{
    int ret = 0;
    
    BrowsableConfigRoot *br;
    switch(c) {
        case KEY_F8: // exit
        case KEY_BREAK: // runstop
        case KEY_ESCAPE:
            if (state->level == 1) { // going to level 0
                ((ConfigBrowserState *)state)->on_close();
            }
            if(state->level == start_level) {
                // check if we need to save to flash
                on_exit();
                ret = MENU_CLOSE;
            } else {
                state->level_up();
            }
            break;
        case KEY_DOWN: // down
            state->down(1);
            break;
        case KEY_UP: // up
            state->up(1);
            break;
        case KEY_PAGEUP:
            state->up(window->get_size_y()/2);
            break;
        case KEY_PAGEDOWN:
            state->down(window->get_size_y()/2);
            break;
        case KEY_A:
            if (user_interface->navmode == 0) {
                // do nothing
            } else {
                if (state->level == 1) { // going to level 0
                    ((ConfigBrowserState *)state)->on_close();
                }
                if(state->level == start_level) {
                    on_exit();
                    ret = MENU_CLOSE; // leave
                } else {
                    state->level_up();
                }
            }
            break;
        case KEY_S:
            if (user_interface->navmode == 0) { 
                // do nothing
            } else {
                state->down(1);
            }
            break;
        case KEY_D:
            if (user_interface->navmode == 0) {
                // do nothing
            } else {
                if(state->level==0) {
                    state->into();
                } else {
                    state->change();
                }
            }
            break;
        case KEY_W:
            if (user_interface->navmode == 0) {
                // do nothing
            } else {
                state->up(1);
            }
            break;
        case KEY_F1: // F7 -> page down
            if (user_interface->navmode == 0) {
                state->up(window->get_size_y()-2);
            } else {
                ret = MENU_CLOSE; // do nothing in the non-commodore mode
            }
            break;
        case KEY_F3: // F3 -> help
            if (user_interface->navmode == 0) {
                reset_quick_seek();
                state->refresh = true;
                user_interface->run_editor(helptext_ult, strlen(helptext_ult));
            } else {
                state->up(window->get_size_y()-2);
            }
            break;
        case KEY_F5: // F5: Menu | Page down
            if (user_interface->navmode == 0) {
                // do nothing
            } else {
                state->down(window->get_size_y()-2);
            }
            break;
        case KEY_F7: // F7 -> page down or help
            if (user_interface->navmode == 0) {
                state->down(window->get_size_y()-2);
            } else {
                state->refresh = true;
                user_interface->run_editor(helptext_wasd, strlen(helptext_wasd));
            }
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
        case '+':
            if(state->level!=0)
                state->increase();
            break;
        case KEY_LEFT: // left
		case KEY_BACK: // del
            if (state->level == 1) { // going to level 0
                ((ConfigBrowserState *)state)->on_close();
            }
            if(state->level == start_level) {
                on_exit();
                ret = MENU_CLOSE; // leave
            } else {
                state->level_up();
            }
            break;
        case '-':
            if(state->level!=0)
                state->decrease();
            break;
        default:
            printf("Unhandled key: %b\n", c);
    }    
    return ret;
}

void ConfigBrowser :: checkFileManagerEvent(void)
{
    FileManagerEvent *event;
    while(1) {
        event = (FileManagerEvent *)observerQueue->waitForEvent(0);
        if (!event) {
            break;
        }

        switch (event->eventType) {
        case eRefreshDirectory:
            state->refresh = true;
            state->needs_reload = true;
            if (state->level == 1) { // this MUST be a BrowseableConfigStore FIXME!
                ((BrowsableConfigStore *)state->node)->getStore()->at_open_config();
            }
            break;

        default:
            break;
        }

        delete event;
    }
}
