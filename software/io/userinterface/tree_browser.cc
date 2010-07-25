/* ========================================================================== */
/*                                                                            */
/*   TreeBrowser.cc                                                           */
/*   (c) 2010 Gideon Zweijtzer                                                */
/*                                                                            */
/* ========================================================================== */
#include "tree_browser.h"
#include "config_menu.h"
#include "context_menu.h"
#include "task_menu.h"
/*
#include "filemanager.h"
#include "config.h"
#include "dump_hex.h"
#include "task_menu.h"
*/

void tering(void)
{
    printf("Hello");
}
/***********************/
/* Tree Browser Object */
/***********************/
TreeBrowserState :: TreeBrowserState(PathObject *n, TreeBrowser *b, int lev)
{
	browser = b;
	level = lev;
	node = n;
    first_item_on_screen = -1;
    selected_line = 0;
    selected = NULL;

    refresh = true;
    default_color = 15;
    initial_index = -1;
    previous = NULL;
    deeper = NULL;
//    printf("Constructor tree browser state: Node = %p\n", node);
}

TreeBrowserState :: ~TreeBrowserState()
{
	if(previous)
		delete previous;
}

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

/*
 * State functions
 */
void TreeBrowserState :: do_refresh()
{
//	printf("RefreshIn.");
	refresh = false;
    if(!(browser->window)) {
        printf("INTERNAL ERROR: NO WINDOW TO DO REFRESH ON!!\n");
        return;
    }

    if(!selected) {
        browser->reset_quick_seek();

        if(initial_index >= 0) {
            move_to_index(initial_index);
        } else {
        	move_to_index(node->get_header_lines());
        }
    } else {
        draw(); // just draw.. we don't need to move anything
    }
//	printf("RefreshOut.");
}

void TreeBrowserState :: draw()
{
	if(!browser->window)
		return;
		
//	printf("Draw. First=%d. Selected_line=%d. Number of el=%d\n", first_item_on_screen, selected_line, node->children.get_elements());
//	printf("Window = %p. WindowBase: %p\n", browser->window, browser->window->get_pointer());
	// this functions initializes the screen
    browser->window->set_color(default_color);
    browser->window->clear();

    if(node->children.get_elements() == 0) {
    	browser->window->move_cursor(0, 0);
    	browser->window->output("\033\025< No Items >");
    	selected = NULL;
    	return;
    }

    if(first_item_on_screen < 0)
        return;

    int y = browser->window->get_size_y(); // how many can I show?
    
    PathObject *t;
    for(int i=0;i<y;i++) {
    	t = node->children[i+first_item_on_screen];
    	if(!t)
    		break;
/*
    	if(t == selected) {
            selected_line = i;
        }
*/
        browser->window->move_cursor(0, i);
        browser->window->output_line(t->get_display_string());
    }

    selected = node->children[first_item_on_screen + selected_line];

    if(selected_line < 0) {
        printf("error!");
    } else {
        highlight(); // highlight selected item
    }
}

void TreeBrowserState :: update_selected(void)
{
    if(!selected)
        return;

    browser->window->move_cursor(0, selected_line);
    browser->window->set_color(1); // highlighted
    browser->window->output_line(selected->get_display_string());
}
    
void TreeBrowserState :: unhighlight()
{
    browser->window->set_color(default_color, 0, selected_line, 40, 1, true);
//    window->reverse(0, selection_index, 40);
}
    
void TreeBrowserState :: highlight()
{
    browser->window->set_color(1, 0, selected_line, 40, 1, true);
//    window->reverse(0, selection_index, 40);
//    window->set_color(1, 0, selection_index, quick_seek_length, 1);
}

void TreeBrowserState :: up(int num)
{
    if(first_item_on_screen < 0)
        return;

    if(first_item_on_screen + selected_line <= node->get_header_lines())
    	return;

    // first, un-hilite the selection
    unhighlight();
    
    while(num--) {
        if(first_item_on_screen + selected_line <= node->get_header_lines())
        	break;

    	if(selected_line == 0) {
			browser->window->scroll_down();
			first_item_on_screen--;
			browser->window->move_cursor(0,0);
			browser->window->set_color(default_color);
			reselect();
			browser->window->output_line(selected->get_display_string());
		} else {
			selected_line--;
		}
    }
	reselect();
    // finally, highlight selection.
    highlight();
}

void TreeBrowserState :: down(int num)
{
    if(first_item_on_screen < 0)
        return;

    int last_el = node->children.get_elements()-1;
    if(first_item_on_screen + selected_line >= last_el)
    	return;

    // first, un-hilite the selection
    unhighlight();
    
    // determine at what index we need to scroll
    int max_y = browser->window->get_size_y() - 1;

    while(num--) {
        if(first_item_on_screen + selected_line >= last_el)
        	break;

        if(selected_line == max_y) {
			browser->window->scroll_up();
			first_item_on_screen++;
			browser->window->move_cursor(0,max_y);
			browser->window->set_color(default_color);
			reselect();
			browser->window->output_line(selected->get_display_string());
		} else {
			selected_line++;
		}
    }
	reselect();

    // finally, highlight selection.
    highlight();
}

void TreeBrowserState :: reselect(void)
{
	if(selected_line + first_item_on_screen >= node->children.get_elements()) {
		move_to_index(node->children.get_elements()-1);
	}
	selected = node->children[selected_line + first_item_on_screen];
}

void TreeBrowserState :: reload(void)
{
	node->fetch_children();
	reselect();
	refresh = true;
}

void TreeBrowserState :: into(void)
{
	if(!selected)
		return;

	printf("Going deeper into = %s\n", selected->get_name());
    int children = selected->fetch_children();
    if(children < 0)
        return;
        
    printf("%d children fetched.\n", children);
    reselect(); // might have been promoted..

/*
    if(selected->children.is_empty())
        return;
*/
        
    deeper = new TreeBrowserState(selected, browser, level+1);
    browser->state = deeper;
    deeper->previous = this;
}

void TreeBrowserState :: level_up(void)
{
    if(!previous)
        return;

    previous->selected->cleanup_children();
    browser->state = previous;
    previous->refresh = true;
    previous = NULL; // unlink;
    delete this;
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
    printf("Creating context menu for %s\n", state->node->get_name());
    menu_node    = new PathObject(NULL);
    menu_browser = new TaskMenu(menu_node, state->node);
    menu_browser->init(window, keyb);
    state->reselect();
    user_interface->activate_uiobject(menu_browser);
    // from this moment on, we loose focus.. polls will go directly to menu!

//    printf("Menu Node = %p. Menu_browser = %p.\n", menu_node, menu_browser);
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
            push_event(e_terminate);
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
            ret = 0; // ## TODO
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

void TreeBrowserState :: move_to_index(int idx)
{
	int num_el = node->children.get_elements();
	if(num_el == 0) {
		first_item_on_screen = -1;
		draw();
		return;
	}

	if((first_item_on_screen + selected_line)==idx) // duh!
        return;

    // Try to determine the first item on the screen, by
    // stepping half of the screen size up.
	int y = (browser->window->get_size_y() / 2);
	if(idx < y) {
		first_item_on_screen = 0;
		selected_line = idx;
	    draw();
		return;
	}
	if(num_el <= browser->window->get_size_y()) {
		first_item_on_screen = 0;
		selected_line = idx;
	    draw();
		return;
	}

	first_item_on_screen = idx - y;
	selected_line = y;
    draw();    
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
