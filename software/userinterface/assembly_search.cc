#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "assembly_search.h"
#include "assembly.h"

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
    state = new AssemblySearchForm(root, this, 0);
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


AssemblySearchForm :: AssemblySearchForm(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

AssemblySearchForm :: ~AssemblySearchForm()
{

}


void AssemblySearchForm :: send_query(void)
{
    mstring query;
    for(int i=0;i<children->get_elements();i++) {
        Browsable *b = (*children)[i];
        if (b->isSelectable()) {
            BrowsableQueryField *field = (BrowsableQueryField *)b;
            const char *name = field->getName();
            const char *value = field->getStringValue();
            if (strlen(value) == 0) {
                continue;
            }
            if (name[0] == '$') {
                continue;
            }
            if (query.length() > 0) {
                query += " & ";
            }
            query += "(";
            query += field->getName();
            query += " : ";
            if (!field->isDropDown()) {
                query += "\"";
            }
            query += value;
            if (!field->isDropDown()) {
                query += "\"";
            }
            query += ")";
        }
    }
    printf("Query:\n%s\n", query.c_str());

    JSON *response = assembly.send_query(query.c_str());
    if (response && response->type() == eList) {
        printf("Creating results view...\n");
        BrowsableQueryResults *rb = new BrowsableQueryResults((JSON_List *)response);
        deeper = new AssemblyResultsView(rb, browser, 1);
        deeper->previous = this;
        browser->state = deeper;
    }
}

void AssemblySearchForm :: change(void)
{
	if(!under_cursor)
		return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;

    char buffer[32];
    if ((field->getName())[0] == '$') {  // The dirtiest trick ever!
        send_query();
    } else if (field->isDropDown()) {
        browser->context(0);
        // refresh will take place, because the context menu disappears and refresh flag is set
    } else {
        strcpy(buffer, field->getStringValue());
        browser->window->set_color(1);
        browser->user_interface->string_edit(buffer, 26, browser->window, 10, this->cursor_pos);
        field->setStringValue(buffer);
        // explicit refresh
        refresh = true;
        down(1);
    }
}

void AssemblySearchForm :: increase(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->updown(1);
    update_selected();
}

void AssemblySearchForm :: decrease(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->updown(-1);
    update_selected();
}

AssemblyResultsView :: AssemblyResultsView(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

AssemblyResultsView :: ~AssemblyResultsView()
{

}


BrowsableAssemblyRoot :: BrowsableAssemblyRoot()
{
    presets = assembly.get_presets();
}

#include "browsable_root.h"
IndexedList<Browsable *> *BrowsableQueryResult :: getSubItems(int &error)
{
    // name, group, handle, event, date*, category*, subcat*, rating*, type*, repo*, latest, sort, order
    error = 0;
    if (children.get_elements() == 0) {
        JSON *j = assembly.request_entries(id.c_str(), category);
        if (j && j->type() == eObject) {
            JSON *content = ((JSON_Object *)j)->get("contentEntry");
            if (content && content->type() == eList) {
                JSON_List *list = (JSON_List *)content;
                for(int i=0; i < list->get_num_elements(); i++) {
                    JSON *el = (*list)[i];
                    puts(el->render());
                    // children.append(new BrowsableDirEntryAssembly(list[i]));
                }
            }
        } else {
            error = 1;
        }
    }
    return &children;
}
