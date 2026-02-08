#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "assembly_search.h"
#include "assembly_entry.h"
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
    state = new AssemblySearchForm(root, this, 0);
    state->reload();
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
	state->do_refresh();
}

// Using the base class function deinit, which destroys the window.

static const char *queryhelp = 
        "1. Query Screen:\n"
        "\n"
		"CRSR UP/DN: Select field\n"
		"CLEAR:      Clear all fields\n"
        "DEL:        Clear selected field\n"
        "RETURN:     Field: Edit\n"
        "            Search: Send Query\n"
        "+/-:        Change preset option.\n"
        "RUN/STOP:   Close Search\n"
        "CRSR LEFT:  Close Search\n"
        "\n"
		"Quick type: Use the keyboard to type\n"
		"            directly in current\n"
        "            field.\n"
        "\n"
        "2. Query Result Screen:\n"
		"CRSR UP/DN: Select title\n"
        "RETURN/->:  View Result entries\n"
        "CRSR LEFT:  Return to Query screen\n"
        "\n"
        "3. Result Entries:\n"
        "\nWorks like any directory. Please\n"
        "note that accessing disks or files\n"
        "introduces a delay as the data needs\n"
        "to be downloaded from the internet.";

int AssemblySearch :: handle_key(int c)
{
    int ret = 0;
    
    if ((c == KEY_BREAK) || (c == KEY_ESCAPE)) {
        return MENU_CLOSE; // independent of level, it closes the search.
        // if we'd have this handled by the tree browser, it would cause a HIDE instead
    }
    // For level 1 it's just like tree browser, with the exception of the return key / space
    // This could also be handled by the tree browser, if we check for context menus and do 'into' when none exist.
    if (((c == KEY_RETURN) || (c == KEY_SPACE)) && (state->level == 1)) {
        state->into();
        return 0;
    }
    if (state->level >= 1) {
        return TreeBrowser :: handle_key(c);
    }
    switch(c) {
        case KEY_F8: // exit
            ret = MENU_EXIT;
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
        case KEY_TASKS:
            ret = MENU_CLOSE; // do nothing in the non-commodore mode
            break;
        case KEY_HELP: 
            state->refresh = true;
            user_interface->run_editor(queryhelp, strlen(queryhelp));
            break;
        case KEY_CLEAR: //
            if(state->level == 0) {
                state->reload();
                state->do_refresh();
            }
            break;
        case KEY_HOME: // clear entry
            ((AssemblySearchForm *)state)->clear_entry();
            break;
        case KEY_SPACE: // space = select
        	state->select_one();
            break;
        case KEY_RETURN: // CR = select
            switch (state->level) {
            case 0:
                state->change();
                break;
            case 1:
                state->into();
                break;
            case 2:
                context(0);
                break;
            default:
                break;
            }
            break;
        case KEY_RIGHT: // right
            if(state->level!=0)
                state->into();
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
                ret = MENU_CLOSE; // leave
            } else {
                state->level_up();
            }
            break;

        default:
            if ((state->level == 0) && (
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9'))) {
                keyb->push_head(c);
                state->change();
            } else {
                printf("Unhandled key: %b\n", c);
            }
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
            const char *value = field->getAqlString();
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
            query += ":";
            if (!field->isDropDown()) {
                query += "\"";
            }
            if ((strcasecmp(name, "rating") == 0) && (value[0] != '>')) {
                query += ">=";
            }
            query += value;
            if (!field->isDropDown()) {
                query += "\"";
            }
            query += ")";
        }
    }
    if (!query.length()) {
        browser->user_interface->popup("Queries cannot be empty!", BUTTON_OK);
        return;
    }
    printf("Query:\n%s\n", query.c_str());

    // Let the user know we are busy
    browser->window->getScreen()->set_status("Sending query...", browser->user_interface->color_status);

    JSON *response = assembly.send_query(query.c_str());
    // if (response) {
    //     puts(response->render());
    // }
    if (response) {
        if (response->type() == eList) {
            printf("Creating results view...\n");
            BrowsableQueryResults *rb = new BrowsableQueryResults((JSON_List *)response);
            deeper = new AssemblyResultsView(rb, browser, 1);
            deeper->previous = this;
            int error;
        	deeper->children = rb->getSubItems(error);
        	int child_count = deeper->children->get_elements();
            printf("Number of results: %d.\n", child_count);
            browser->state = deeper;
        }
        delete response;
    } else {
        browser->window->getScreen()->set_status("** Connection FAILED **", 10);
    }
    t_BufferedBody *body = (t_BufferedBody *)assembly.get_user_context();
    if (body)
        delete body;
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

void AssemblySearchForm :: clear_entry(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->reset();
    update_selected();
}

AssemblyResultsView :: AssemblyResultsView(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

AssemblyResultsView :: ~AssemblyResultsView()
{
}

void AssemblyResultsView :: into()
{
    BrowsableQueryResult *item = (BrowsableQueryResult *)under_cursor;
    if (!item)
        return;

    deeper = new TreeBrowserState(item, browser, 2);
    deeper->previous = this;
    int error;
    deeper->children = item->getSubItems(error);
    browser->state = deeper;
    char cat[16];
    browser->path->cd("/a64");
    browser->path->cd(item->getId());
    sprintf(cat, "%d", item->getCategory());
    browser->path->cd(cat);
    int child_count = deeper->children->get_elements();
    printf("Number of entries: %d. Path = %s\n", child_count, browser->getPath());
}

void AssemblyResultsView :: get_entries()
{

}

void BrowsableAssemblyRoot :: fetchPresets(void)
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
                    if (el->type() == eObject) {
                        children.append(new BrowsableDirEntryAssembly(this, (JSON_Object *)el, id.c_str(), category));
                    }
                }
            }
        } else {
            error = 1;
        }
    }
    return &children;
}

AssemblyInGui assembly_gui;
