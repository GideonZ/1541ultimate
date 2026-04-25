#include "userinterface.h"
#include <stdlib.h>
#include <string.h>
#include "mystring.h" // my string class
#include "form.h"
#include "browsable_field.h"

/******************/
/* Form UI Object */
/******************/
/*
 * This is a UI object that implements a form to fill out
 * Each of these fields are single line, thus a short description on the left
 * with a string on the right. There are two types of entries; the string
 * edit fields, and the drop down fields. For the drop down fields, the
 * context menus are used, just like in the config browser.
 * Some unselectable lines can be used for spacing.
 * A special third type of entry exits the screen to perform the search.
 * - BrowsableQueryField, with subtype: string entry, and select (drop down)
 */


FormUI :: FormUI(UserInterface *ui, int size_x, int size_y, const char *title, JSON_Object *fields)
    : form(title, fields), size_x(size_x), size_y(size_y), TreeBrowser(ui, root)
{
    setCleanup();
    state = new FormUIState(&form, this, 0);
    state->reload();
}

FormUI :: ~FormUI()
{
    printf("And there goes our Form!"); 
}

void FormUI :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;
	window = new Window(screen, (screen->get_size_x() - size_x) >> 1, (screen->get_size_y() - size_y) >> 1, size_x, size_y);
	window->draw_border();
	keyb = k;
	state->do_refresh();
}

// Using the base class function deinit, which destroys the window.

static const char *formhelp = 
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
        "            field.\n";

int FormUI :: handle_key(int c)
{
    int ret = 0;
    
    if ((c == KEY_BREAK) || (c == KEY_ESCAPE)) {
        return MENU_CLOSE; // independent of level, it closes the search.
        // if we'd have this handled by the tree browser, it would cause a HIDE instead
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
            user_interface->run_editor(formhelp, strlen(formhelp));
            break;
        case KEY_CLEAR: //
            state->reload();
            state->do_refresh();
            break;
        case KEY_HOME: // clear entry
            ((FormUIState *)state)->clear_entry();
            break;
        case KEY_SPACE: // space = select
        	state->select_one();
            break;
        case KEY_RETURN: // CR = select
            ret = ((FormUIState *)state)->edit();
            break;
        case KEY_RIGHT: // right
            break;
        case '+':
            state->increase();
            break;
        case '-':
            state->decrease();
            break;
        case KEY_LEFT: // left
		case KEY_BACK: // del
            ret = MENU_CLOSE; // leave
            break;

        default:
            if ((state->level == 0) && (
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9'))) {
                keyb->push_head(c);
                ret = ((FormUIState *)state)->edit();
            } else {
                printf("Unhandled key: %b\n", c);
            }
    }    
    if (ret == MENU_CLOSE) {
        form.read_fields();
    }
    return ret;
}


FormUIState :: FormUIState(Browsable *node, TreeBrowser *tb, int level) : TreeBrowserState(node, tb, level)
{
    //default_color = 7;
}

FormUIState :: ~FormUIState()
{

}

int FormUIState :: edit(void)
{
	if(!under_cursor)
		return MENU_NOP;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;

    char buffer[32];
    if ((field->getName())[0] == '$') {  // The dirtiest trick ever!
        submit();
        return MENU_CLOSE;
    } else if (field->isDropDown()) {
        browser->context(0);
        // refresh will take place, because the context menu disappears and refresh flag is set
    } else {
        strcpy(buffer, field->getStringValue());
        browser->window->set_color(1);
        browser->user_interface->string_edit(buffer, ((FormUI *)browser)->size_x - 12, browser->window, 10, this->cursor_pos);
        field->setStringValue(buffer);
        // explicit refresh
        refresh = true;
        down(1);
    }
    return MENU_NOP;
}

void FormUIState :: increase(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->updown(1);
    update_selected();
}

void FormUIState :: decrease(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->updown(-1);
    update_selected();
}

void FormUIState :: clear_entry(void)
{
    if(!under_cursor)
        return;

    BrowsableQueryField *field = (BrowsableQueryField *)under_cursor;
    field->reset();
    update_selected();
}
