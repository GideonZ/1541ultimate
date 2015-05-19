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
    initState(root);

    //path = file_manager.get_new_path("Tree Browser");
}

void TreeBrowser :: initState(Browsable *root)
{
    state = new TreeBrowserState(root, this, 0);
}

TreeBrowser :: ~TreeBrowser()
{
	delete state;
	//file_manager.release_path(path);
}

void TreeBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;
	window = new Window(screen, 0, 2, 40, 22);
	keyb = k;
	state->do_refresh();
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
    user_interface->activate_uiobject(configBrowser);
    // from this moment on, we loose focus.. polls will go directly to config menu!
}

void TreeBrowser :: context(int initial)
{
	if(!state->under_cursor)
		return;

    printf("Creating context menu for %s\n", state->under_cursor->getName());
    contextMenu = new ContextMenu(state->under_cursor, initial, state->selected_line);
    contextMenu->init(screen, keyb);
//    state->reselect();
    user_interface->activate_uiobject(contextMenu);
    // from this moment on, we loose focus.. polls will go directly to context menu!
}

void TreeBrowser :: task_menu(void)
{
	if(!state->node)
		return;
    printf("Creating task menu for %s\n", state->node->getName());
    contextMenu = new TaskMenu();
    contextMenu->init(screen, keyb);
//    state->reselect();
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
	BYTE c;
    int ret = 0;

/*
    if(e.type == e_invalidate) {
    	invalidate((CachedTreeNode *)e.object);
    	return 0;
    } else
*/

    if(e.type == e_refresh_browser) {
    	Browsable *obj = (Browsable *)e.object;
    	if(!obj || (obj == state->node)) {
    		state->refresh = true;
    	}
	}
	
    if(contextMenu) {
        if(sub_returned < 0) {
        	delete contextMenu;
            contextMenu = NULL;
        } else if(sub_returned > 0) {
            // printf("Pointer to selected context/menu item: %p\n", menu_browser->state->selected);
            // 0 is dummy, bec it is of the type ConfigItem. ConfigItem
            // itself knows the value that the actual object (= selected of THIS
            // browser!) needs to be called with. It would be better to just
            // create a return value of a GUI object, and call execute
            // with that immediately.
//            printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
//            dump_hex(menu_node, 0x80);
            contextMenu->executeAction();
//            printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
//            dump_hex(menu_node, 0x80);
            state->update_selected(); //refresh = true;
//            state->reselect();
//            printf("A");
            delete contextMenu;
//            printf("B");
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

	if(state->refresh)
        state->do_refresh();

    c = keyb->getch();
    if(!c)
        return 0;

    ret = handle_key(c);

    return ret;
}

int TreeBrowser :: handle_key(BYTE c)
{           
    int ret = 0;
    
    switch(c) {
        case 0x03: // runstop
            push_event(e_unfreeze);
            break;
        case 0x8C: // exit (F8)
            push_event(e_unfreeze);
//            push_event(e_terminate);
            break;
        case 0x11: // down
        	reset_quick_seek();
            state->down(1);
            break;
        case 0x91: // up
        	reset_quick_seek();
            state->up(1);
            break;
        case 0x85: // F1 -> page up
        	reset_quick_seek();
            state->up(window->get_size_y()/2);
            break;
        case 0x86: // F3 -> RUN
        	reset_quick_seek();
			user_interface->run_editor(helptext);
            break;
		case 0x87: // F5: Menu
			task_menu();
			break;
        case 0x88: // F7 -> page down
        	reset_quick_seek();
            state->down(window->get_size_y()/2);
            break;
        case 0x89: // F2 -> config
            config();
            break;
        case 0x14: // backspace
            if(quick_seek_length) {
                quick_seek_length--;
                perform_quick_seek();
            }
            break;
        case 0x20: // space = select
        case 0x0D: // CR = select
            reset_quick_seek();
            context(0);
            break;
        case 0x1D: // right
            reset_quick_seek();
			if (state->into2()) context(0);
            break;
        case 0x9D: // left
        	state->level_up();
            break;
//	        case 0x2F: // '/'
//              reset_quick_seek();
//	            menu_dir_depth = 1;
//	            dir_up();
//	            break;
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

/*
void TreeBrowser :: invalidate(Browsable *obj)
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

	printf("The object to be invalidated is: %s\n", obj->getName());

	TreeBrowserState *st, *found;
	st = state;
	found = NULL;
	while(st) {
		printf("checking %s...\n", st->node->getName());
		if(st->node == obj) {
			found = st;
			break;
		}
		st = st->previous;
	}

	if(found) { // need to roll back
		printf("Rolling back browser...");
		if(contextMenu) {
			printf("(first, remove open windows)...");
			contextMenu->deinit();
			user_interface->focus--;
			delete contextMenu;
			contextMenu = NULL;
		}
		do {
			st = state;
			state->level_up();
		} while(st != found);
		printf(" done");
		state->reselect();
		printf("...\n");
	} else {
		printf("Did not rewind, because the object invalidated is not in my path.\n");
	}
}
*/

