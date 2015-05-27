/* ========================================================================== */
/*                                                                            */
/*   TreeBrowser.cc                                                           */
/*   (c) 2010 Gideon Zweijtzer                                                */
/*                                                                            */
/* ========================================================================== */
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "config_menu.h"
#include "context_menu.h"
#include "task_menu.h"
#include "filemanager.h"
#include "editor.h"
#include "userinterface.h"
#include "browsable_root.h"

static char *helptext=
	"CRSR UP/DN: Selection up/down\n"
	"CRSR LEFT:  Go one level up\n"
	"            leave directory or disk\n"
	"CRSR RIGHT: Go one level down\n"
	"            enter directory or disk\n"
	"RETURN/SPC: Selection context menu\n"
	"RUN/STOP:   Leave menu / Back\n"
	"\n"
	"F1:         Selection Page up\n"
	"F7:         Selection Page down\n"
	"\n"
	"F2:         Enter the setup menu\n"
	"F5:         Action menu\n"
	"\n"
	"Quick seek: Use the keyboard to type\n"
	"            the name to search for.\n"
	"            You can use ? as a\n"
	"            wildcard.\n"
	"\nRUN/STOP to close this window.";

/***********************/
/* Tree Browser Object */
/***********************/
TreeBrowser :: TreeBrowser(Browsable *root)
{
	// initialize state
    window = NULL;
    keyb = NULL;
    contextMenu = NULL;
    configBrowser = NULL;
    quick_seek_length = 0;
    quick_seek_string[0] = '\0';
    this->root = root;
    state = 0;
    //path = file_manager.get_new_path("Tree Browser");
}

TreeBrowser :: ~TreeBrowser()
{
	if(state)
		delete state;
	//file_manager.release_path(path);
}

void TreeBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;
	window = new Window(screen, 0, 2, 40, 22);
	keyb = k;
	if(!state)
		state = new TreeBrowserState(root, this, 0);
    state->reload();
	// state->do_refresh();
}

void TreeBrowser :: deinit(void)
{
	if(window) {
		delete window;
		window = NULL;
	}
}

void TreeBrowser :: config(void)
{
    printf("Creating config menu...\n");
        
    Browsable *configRoot = new BrowsableConfigRoot();
    configBrowser = new ConfigBrowser(configRoot);
    configBrowser->init(screen, keyb);
    state->refresh = true; // refresh as soon as we come back
    user_interface->activate_uiobject(configBrowser);

    // from this moment on, we loose focus.. polls will go directly to config menu!
}

void TreeBrowser :: context(int initial)
{
	if(!state->under_cursor)
		return;

    printf("Creating context menu for %s\n", state->under_cursor->getName());
    contextMenu = new ContextMenu(state->under_cursor, initial, state->selected_line);
    contextMenu->init(window, keyb);
    user_interface->activate_uiobject(contextMenu);
    // from this moment on, we loose focus.. polls will go directly to context menu!
}

void TreeBrowser :: task_menu(void)
{
	if(!state->node)
		return;
    printf("Creating task menu for %s\n", state->node->getName());
    contextMenu = new TaskMenu();
    contextMenu->init(window, keyb);
    user_interface->activate_uiobject(contextMenu);
    // from this moment on, we loose focus.. polls will go directly to menu!
}

void TreeBrowser :: test_editor(void)
{
	Event e(e_nop, 0, 0);
    Editor *edit = new Editor(NULL); // use built-in text
    edit->init(screen, keyb);
    int ret;
    do {
        ret = edit->poll(0, e);
    } while(!ret);
    edit->deinit();
}
    

int TreeBrowser :: poll(int sub_returned, Event &e) // call on root possible
{
	int c;
    int ret = 0;


    if(e.type == e_invalidate) {
    	invalidate(e.object);
    	return 0;
    } else
    if(e.type == e_refresh_browser) {
		TreeBrowserState *st = state;
		while(st) {
			if (st->node->invalidateMatch(e.object)) {
				st->needs_reload = true;
				st->refresh = true;
			}
			st = st->previous;
		}
	}
	
    if(contextMenu) {
        if(sub_returned < 0) {
        	delete contextMenu;
            contextMenu = NULL;
            state->draw();
        } else if(sub_returned > 0) {
            // printf("Pointer to selected context/menu item: %p\n", menu_browser->state->selected);
            // 0 is dummy, bec it is of the type ConfigItem. ConfigItem
            // itself knows the value that the actual object (= selected of THIS
            // browser!) needs to be called with. It would be better to just
            // create a return value of a GUI object, and call execute
            // with that immediately.
            state->draw();
            contextMenu->executeAction();
            delete contextMenu;
            contextMenu = NULL;
        }
        return ret;
    }
        
	if(e.type == e_reload_browser)
		state->reload();

	if(e.type == e_browse_into) {
	    reset_quick_seek();
        state->into();
	}

	if(state->refresh) {
        state->do_refresh();
	}

    c = keyb->getch();
    if(c < 0)
        return 0;

    ret = handle_key(c);

    return ret;
}

int TreeBrowser :: handle_key(int c)
{           
    int ret = 0;
    
    switch(c) {
        case KEY_BREAK: // runstop
            push_event(e_unfreeze);
            break;
        case KEY_F8: // exit (F8)
            push_event(e_unfreeze);
//            push_event(e_terminate);
            break;
        case KEY_DOWN: // down
        	reset_quick_seek();
            state->down(1);
            break;
        case KEY_UP: // up
        	reset_quick_seek();
            state->up(1);
            break;
        case KEY_F1: // F1 -> page up
        case KEY_PAGEUP:
        	reset_quick_seek();
            state->up(window->get_size_y()/2);
            break;
        case KEY_F3: // F3 -> RUN
        	reset_quick_seek();
        	state->refresh = true;
        	user_interface->run_editor(helptext);
            break;
		case KEY_F5: // F5: Menu
			task_menu();
			break;
        case KEY_F7: // F7 -> page down
        case KEY_PAGEDOWN:
        	reset_quick_seek();
            state->down(window->get_size_y()/2);
            break;
        case KEY_F2: // F2 -> config
            config();
            break;
        case KEY_BACK: // backspace
            if(quick_seek_length) {
                quick_seek_length--;
                perform_quick_seek();
            }
            break;
        case KEY_SPACE: // space = select
        case KEY_RETURN: // CR = select
            reset_quick_seek();
            context(0);
            break;
        case KEY_RIGHT: // right
            reset_quick_seek();
			if (state->into2()) context(0);
            break;
        case KEY_LEFT: // left
        	state->level_up();
            break;
        default:
            if((c >= '!')&&(c < 0x80)) {
                if(quick_seek_length < (MAX_SEARCH_LEN-2)) {
                    quick_seek_string[quick_seek_length++] = c;
                    if(!perform_quick_seek())
                        quick_seek_length--;
                }
            } else {
                printf("Unhandled key: %b\n", c);
            }
    }    
    return ret;
}

bool TreeBrowser :: perform_quick_seek(void)
{
    if(!state->under_cursor)
        return false;

    quick_seek_string[quick_seek_length] = '*';
    quick_seek_string[quick_seek_length+1] = 0;
    printf("Performing seek: '%s'\n", quick_seek_string);

    int num_el = state->children.get_elements();
    for(int i=0;i<num_el;i++) {
    	Browsable *t = state->children[i];
		if(pattern_match(quick_seek_string, t->getName(), false)) {
			state->move_to_index(i);
			return true;
		}
    }
    return false;
}

void TreeBrowser :: reset_quick_seek(void)
{
    quick_seek_length = 0;
}

void TreeBrowser :: invalidate(void *obj)
{
	// we just have to take care of ourselves.. which means, that we have to check
	// if the current browser state is dependent on the object that is going to be
	// destroyed. If so, we will need to revert to the object that is not
	// dependent anymore (likely to be root).

	// Note!! This function CAN be called on a sub-browser, like context, menu or
	// even config. Therefore, we need to recall this function on browser 0!
	if(this != user_interface->get_root_object()) {
		((TreeBrowser *)(user_interface->get_root_object()))->invalidate(obj);
		return;
	}

	// printf("The object to be invalidated is: %s\n", obj->get_name());

	TreeBrowserState *st, *found;
	st = state;
	found = NULL;
	while(st) {
		printf("checking %s...\n", st->node->getName());
		if(st->node->invalidateMatch(obj)) {
			found = st;
			break;
		}
		st = st->previous;
	}

	if(found) { // need to roll back
		printf("Rolling back browser...");
		if(contextMenu) {
			printf("(first, remove open context)...");
			contextMenu->deinit();
			user_interface->focus--;
        	delete contextMenu;
            contextMenu = NULL;
		}
		if(configBrowser) {
			printf("(first, remove open config)...");
			configBrowser->deinit();
			user_interface->focus--;
        	delete configBrowser;
        	configBrowser = NULL;
		}

		do {
			st = state;
			state->level_up();
		} while(st != found);
		state->reload();
		printf(" done\n");
	} else {
		printf("Did not rewind, because the object invalidated is not in my path.\n");
		// still, the item might be somewhere else in the tree.
		st = state;
		while(st) {
			for(int i=0;i<st->children.get_elements();i++) {
				if (st->children[i]->invalidateMatch(obj)) {
					printf("Found invalidate match %s in %s.\n", st->children[i]->getName(), st->node->getName());
					st->needs_reload = true;
					st = 0; // break outer
					break;
				}
			}
			if (st)
				st = st->previous;
		}

	}
	printf("** There are now %d browsable objects.\n", *(Browsable :: getCount()));
}

