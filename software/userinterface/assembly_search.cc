#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "assembly_search.h"

/****************************/
/* AssemblySearch UI Object */
/****************************/
/*
 * The first screen consists of a series of query fields.
 * Each of these fields are single line, thus a short description on the left
 * with a string on the right. There are two types of entries; the string
 * edit fields, and the drop down fields. For the drop down fields, the
 * context menus are used, just like in the config browser.
 * Some unselectable lines can be used for spacing.
 * A special third type of entry exits the screen to perform the search.
 * - BrowsableQueryField, with subtype: string entry, and select (drop down)
 * 
 * The second screen shows the results of the search. This screen will be 
 * populated with the 'entries'. These entries show the name and group,
 * maybe year of the release. These entries are a special type of
 * Browsable; which holds the reference to the ID and Category number
 * as listed on the Assembly server. When entering on such entry, the
 * downloadable items are fetched and shown on the third screen.
 * 
 * - BrowsableAssemblyEntry (second screen)
 * - BrowsableAssemblyItem (third screen), which may be a derivative of
 *      BrowsableDirEntry, such that the filetypes would work. However,
 *      the file must still be downloaded to the cache, and the path
 *      reference must be set to the cache for the commands to work. 
 */
Assembly assembly;

AssemblySearch :: AssemblySearch(UserInterface *ui, Browsable *root) : TreeBrowser(ui, root)
{
    setCleanup();
}

AssemblySearch :: ~AssemblySearch()
{
    printf("And there goes our Assembly browser!"); 
}

void AssemblySearch :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;
	window = new Window(screen, (screen->get_size_x() - 40) >> 1, 2, 40, screen->get_size_y()-3);
	window->draw_border();
	keyb = k;
    state = new AssemblySearchState(root, this, 0);
    state->reload();
	state->do_refresh();
}

static const char *queryhelp = 
"Help!";

int AssemblySearch :: handle_key(int c)
{
    int ret = 0;
    
    switch(c) {
        case KEY_F8: // exit
        case KEY_BREAK: // runstop
        case KEY_ESCAPE:
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
        case KEY_F3: // F3 -> help
            reset_quick_seek();
            state->refresh = true;
            user_interface->run_editor(queryhelp, strlen(queryhelp));
            break;
        case KEY_SPACE: // space = select
        case KEY_RETURN: // CR = select
            if(state->level!=0)
                state->into();
            else
                state->change();
            break;
        case KEY_RIGHT: // right
            if(state->level!=0)
                state->into();
            else
                state->increase();
            break;
        case '+':
            if(state->level==0)
                state->increase();
            break;
        case '-':
            if(state->level==0)
                state->decrease();
            break;
        case KEY_LEFT: // left
		case KEY_BACK: // del
            if(state->level==0) {
                ret = -2; // leave
            } else {
                state->level_up();
            }
            break;
        default:
            printf("Unhandled key: %b\n", c);
    }    
    return ret;
}


AssemblySearchState :: AssemblySearchState(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

AssemblySearchState :: ~AssemblySearchState()
{

}

void AssemblySearchState :: change(void)
{
	if(!under_cursor)
		return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;

    char buffer[32];
    if (field->isDropDown()) {
        browser->context(0);
        // refresh will take place, because the context menu disappears and refresh flag is set
    } else {
        strcpy(buffer, field->getStringValue());
        browser->user_interface->string_edit(buffer, 26, browser->window, 10, this->cursor_pos);
        field->setStringValue(buffer);
        // explicit refresh
        refresh = true;
        down(1);
    }
}


/*
void AssemblySearchState :: into(void)
{
	if(!under_cursor)
		return;

	deeper = new AssemblySearchState(under_cursor, browser, level+1);
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

void AssemblySearchState :: level_up(void)
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
           

void AssemblySearchState :: increase(void)
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
    
void AssemblySearchState :: decrease(void)
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
*/

