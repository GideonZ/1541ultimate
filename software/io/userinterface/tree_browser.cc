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

/***********************/
/* Tree Browser Object */
/***********************/
TreeBrowser :: TreeBrowser()
{
	// initialize state
    window = NULL;
    keyb = NULL;
    state = new TreeBrowserState(&root, this, 0);
    menu_node = NULL;
    menu_browser = NULL;
    quick_seek_length = 0;
    quick_seek_string[0] = '\0';
}

TreeBrowser :: ~TreeBrowser()
{
	delete state;

	if(menu_node) {
		delete menu_node;
	}
//	deinit();
}

void TreeBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	window = new Screen(screen, 0, 2, 40, 22);
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
        
    menu_browser = new ConfigBrowser();
    menu_node    = new PathObject(NULL, "Dummy node for attaching Config Menu");
    menu_browser->init(window, keyb);
    user_interface->activate_uiobject(menu_browser);
    // from this moment on, we loose focus.. polls will go directly to config menu!
}

void TreeBrowser :: context(int initial)
{
	if(!state->selected)
		return;

    printf("Creating context menu for %s\n", state->selected->get_name());
    menu_node    = new PathObject(NULL);
    menu_browser = new ContextMenu(menu_node, state->selected, initial, state->selected_line);
    menu_browser->init(window, keyb);
    state->reselect();
    user_interface->activate_uiobject(menu_browser);
    // from this moment on, we loose focus.. polls will go directly to context menu!
}

void TreeBrowser :: task_menu(void)
{
	if(!state->node)
		return;
    printf("Creating task menu for %s\n", state->node->get_name());
    menu_node    = new PathObject(NULL);
    menu_browser = new TaskMenu(menu_node, state->node);
    menu_browser->init(window, keyb);
//    state->reselect();
    user_interface->activate_uiobject(menu_browser);
    // from this moment on, we loose focus.. polls will go directly to menu!

//    printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
}

void TreeBrowser :: test_editor(void)
{
	Event e(e_nop, 0, 0);
    Editor *edit = new Editor(NULL); // use built-in text
    edit->init(window, keyb);
    int ret;
    do {
        ret = edit->poll(0, e);
    } while(!ret);
    edit->deinit();
}
    

int TreeBrowser :: poll(int sub_returned, Event &e) // call on root possible
{
    char c;
    int ret = 0;

    if(e.type == e_invalidate) {
    	invalidate((PathObject *)e.object);
    	return 0;
    } else if(e.type == e_refresh_browser) {
    	PathObject *obj = (PathObject *)e.object;
    	if(!obj || (obj == state->node)) {
    		state->refresh = true;
    	}
	}
	
    if(menu_node) {
        if(sub_returned < 0) {
        	delete menu_browser;
            delete menu_node;
            menu_node = NULL;
        } else if(sub_returned > 0) {
            // printf("Pointer to selected context/menu item: %p\n", menu_browser->state->selected);
            // 0 is dummy, bec it is of the type ConfigItem. ConfigItem
            // itself knows the value that the actual object (= selected of THIS
            // browser!) needs to be called with. It would be better to just
            // create a return value of a GUI object, and call execute
            // with that immediately.
//            printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
//            dump_hex(menu_node, 0x80);
            menu_browser->state->selected->execute(0);
//            printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
//            dump_hex(menu_node, 0x80);
            state->update_selected(); //refresh = true;
            state->reselect();
//            printf("A");
            delete menu_browser;
//            printf("B");
            delete menu_node;
//            printf("C");
            menu_node = NULL;
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

int TreeBrowser :: handle_key(char c)
{           
    int ret = 0;
    
    switch(c) {
        case 0x03: // runstop
            push_event(e_unfreeze);
            break;
        case 0x8C: // exit
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
//        case 0x86: // F3 -> RUN
//            test_editor();
//            ret = 0; // ## TODO
//            break;
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
            state->into();
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
    if(!state->selected)
        return false;

    quick_seek_string[quick_seek_length] = '*';
    quick_seek_string[quick_seek_length+1] = 0;
    printf("Performing seek: '%s'\n", quick_seek_string);

    int num_el = state->node->children.get_elements();
    for(int i=0;i<num_el;i++) {
    	PathObject *t = state->node->children[i];
		if(pattern_match(quick_seek_string, t->get_name(), false)) {
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

void TreeBrowser :: invalidate(PathObject *obj)
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

	printf("The object to be invalidated is: %s\n", obj->get_name());

	TreeBrowserState *st, *found;
	st = state;
	found = NULL;
	while(st) {
		printf("checking %s...\n", st->node->get_name());
		if(st->node == obj) {
			found = st;
			break;
		}
		st = st->previous;
	}

	if(found) { // need to roll back
		printf("Rolling back browser...");
		if(menu_node) {
			printf("(first, remove open windows)...");
			menu_browser->deinit();
			user_interface->focus--;
			delete menu_browser;
			delete menu_node;
			menu_node = NULL;
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
