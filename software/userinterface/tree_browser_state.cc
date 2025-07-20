/* ========================================================================== */
/*                                                                            */
/*   tree_browser_state.cc                                                    */
/*   (c) 2010 Gideon Zweijtzer                                                */
/*                                                                            */
/* ========================================================================== */
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "config_menu.h"
#include "context_menu.h"
#include "task_menu.h"
#include "filemanager.h"

IndexedList<Browsable *>emptyList(0, NULL);

/*****************************/
/* Tree Browser State Object */
/*****************************/
TreeBrowserState :: TreeBrowserState(Browsable *n, TreeBrowser *b, int lev)
{
	browser = b;
	level = lev;
	node = n;

	first_item_on_screen = -1;
    selected_line = 0;

    cursor_pos = 0;
    under_cursor = NULL;

    refresh = true;
    needs_reload = false;
    initial_index = -1;
    previous = NULL;
    deeper = NULL;
    children = &emptyList;

//    reload();
//    printf("Constructor tree browser state: Node = %p\n", node);
}

TreeBrowserState :: ~TreeBrowserState()
{
	printf("Deleting TBS Level %d (%s)\n", level, node->getName());
	cleanup();
	if(previous)
		delete previous;
}

void TreeBrowserState :: cleanup()
{
	node->killChildren();
}

/*
 * State functions
 */
void TreeBrowserState :: do_refresh()
{
//	printf("RefreshIn.");
    if(!(browser->window)) {
        printf("INTERNAL ERROR: NO WINDOW TO DO REFRESH ON!!\n");
        return;
    }

    if(needs_reload) {
    	reload();
    }

    cursor_pos = first_item_on_screen + selected_line;
    if (cursor_pos >= children->get_elements()) {
        cursor_pos = children->get_elements() - 1;
    }
    if (cursor_pos < 0) {
        cursor_pos = 0;
    }
    do {
        under_cursor = (*children)[cursor_pos];
        if (!under_cursor) {
            break;
        }
        if (under_cursor->isSelectable()) {
            break;
        }
        cursor_pos++;
    } while(1);

    if(!under_cursor) { // initial state or reset state
        browser->reset_quick_seek();
        // draw();
        int target_index = 0;
        if(initial_index > 0)
        	target_index = initial_index;

        for(int i=target_index;i<children->get_elements();i++) {
			if ((*children)[i]->isSelectable()) {
				target_index = i;
				break;
			}
		}
		move_to_index(target_index);
    } else {
        move_to_index(cursor_pos); // just draw.. we don't need to move anything
    }
	refresh = false;
//	printf("RefreshOut.");
}

void TreeBrowserState :: draw()
{
	if(!browser->window) {
		printf("Draw. No window to draw on.\n");
		return;
	}

    browser->window->set_color(11);
    browser->window->set_background(0);
    browser->window->getScreen()->move_cursor(0, browser->window->getScreen()->get_size_y()-1);
    browser->window->getScreen()->output_fixed_length(browser->path->get_path(), 0, browser->window->getScreen()->get_size_x()-9);

    //	printf("Draw. First=%d. Selected_line=%d. Number of el=%d\n", first_item_on_screen, selected_line, children->get_elements());
//	printf("Window = %p. WindowBase: %p\n", browser->window, browser->window->get_pointer());
	// this functions initializes the screen
    browser->window->set_color(browser->user_interface->color_fg);
    browser->window->reverse_mode(0);

    if(children->get_elements() == 0) {
		browser->window->clear();
    	browser->window->move_cursor(0, 0);
    	browser->window->output("\eF< No Items >");
    	under_cursor = NULL;
    	return;
    }

    int y = browser->window->get_size_y(); // how many can I show?
//    printf("TreeBrowserState::Draw: first=%d, y=%d, selected_line=%d\n", first_item_on_screen, y, selected_line);

    if(first_item_on_screen < 0)
        return;
    
    Browsable *t;

    for(int i=0;i<y;i++) {
    	t = (*children)[i+first_item_on_screen];
		draw_item(t, i, (i + first_item_on_screen) == cursor_pos);
    }

    if(selected_line < 0) {
        printf("error! %d", selected_line);
        selected_line = 0;
    }
    cursor_pos = first_item_on_screen + selected_line;
    under_cursor = (*children)[cursor_pos];
}

void TreeBrowserState :: draw_item(Browsable *t, int line, bool selected)
{
    char buffer[96];

    browser->window->move_cursor(0, line);
    if (t) {
		if (selected) {
			browser->window->set_color(browser->user_interface->color_sel);
            browser->window->set_background(browser->user_interface->color_sel_bg);
		} else if(t->isSelectable()) {
			browser->window->set_color(browser->user_interface->color_fg);
            browser->window->set_background(0);
		} else { // non selectable item
			browser->window->set_color(12); // TODO
		}
		int squeeze_type = browser->user_interface->filename_overflow_squeeze;
		t->getDisplayString(buffer, browser->window->get_size_x(), squeeze_type);
		browser->window->output_line(buffer);
        browser->window->set_background(0);
    } else {
		// draw an empty line
		browser->window->output_line("");
    }
}

void TreeBrowserState :: update_selected(void)
{
    if(!under_cursor)
        return;

    char buffer[96];

    browser->window->move_cursor(0, selected_line);
    browser->window->set_color(browser->user_interface->color_sel); // highlighted
    browser->window->set_background(browser->user_interface->color_sel_bg);
    int squeeze_type = browser->user_interface->filename_overflow_squeeze;
    under_cursor->getDisplayString(buffer, browser->window->get_size_x(), squeeze_type);
    browser->window->output_line(buffer);
}
    
void TreeBrowserState :: up(int num)
{
	int original = cursor_pos;
	int previous = cursor_pos;

	while(num--) {
		cursor_pos--;
		if (cursor_pos < 0) {
			cursor_pos = previous;
			break;
		}
		if ((*children)[cursor_pos]->isSelectable()) {
			previous = cursor_pos; // store last selectable item
		} else {
			num++; // try to jump by moving one extra step
		}
	}
	move_to_index(cursor_pos);
}

void TreeBrowserState :: down(int num)
{
	int original = cursor_pos;
	int previous = cursor_pos;

	while(num--) {
		cursor_pos++;
		if (cursor_pos >= children->get_elements()) {
			cursor_pos = previous;
			break;
		}
		if ((*children)[cursor_pos]->isSelectable()) {
			previous = cursor_pos; // store last selectable item
		} else {
			num++; // try to jump by moving one extra step
		}
	}
	move_to_index(cursor_pos);
}

void TreeBrowserState :: reload(void)
{
	cleanup();
	int error;
	children = node->getSubItems(error);
	printf("State %s reloaded. # of children = %d\n", node->getName(), children->get_elements());
	needs_reload = false;
	refresh = true;
}

void TreeBrowserState :: into(void)
{
	if(!under_cursor)
		return;

	deeper = new TreeBrowserState(under_cursor, browser, level+1);

	int error;
	deeper->children = under_cursor->getSubItems(error);
    if(error < 0) {
    	delete deeper;
    	return;
    }
	browser->path->cd(under_cursor->getName());
    printf("%d children fetched from %s.\n", deeper->children->get_elements(), under_cursor->getName());

	//user_interface->set_path(under_cursor);
    browser->state = deeper;
    deeper->previous = this;
}

bool TreeBrowserState :: into2(void)
{
	// return True if should show context menu instead
	if(!under_cursor)
		return(false);

	deeper = new TreeBrowserState(under_cursor, browser, level+1);

    int error;
	deeper->children = under_cursor->getSubItems(error);

    if(error < 0) {
    	delete deeper;
    	deeper = NULL;
    	return true;
    }
	browser->path->cd(under_cursor->getName());
    printf("%d children fetched from %s.\n", deeper->children->get_elements(), under_cursor->getName());

    browser->state = deeper;
    deeper->previous = this;
	return false;
}

// step into browsable by name, used by TreeBrowser::cd(char* path)
void TreeBrowserState :: into3(const char* name)
{
    Browsable *browsable = 0;
    
    if (needs_reload) {
        reload();
    }

    for(int i=0; i<children->get_elements(); i++) {
        if(pattern_match(name, (*children)[i]->getName(), false)) {
            browsable = (*children)[i];
            break;
        }
    }
    if(!browsable)
        return;
    
    deeper = new TreeBrowserState(browsable, browser, level+1);
    
    int error;
    deeper->children = browsable->getSubItems(error);
    if(error < 0) {
        delete deeper;
        return;
    }
    browser->path->cd(browsable->getName());
    printf("%d children fetched from %s.\n", deeper->children->get_elements(), browsable->getName());
    
    browser->state = deeper;
    deeper->previous = this;
}

void TreeBrowserState :: level_up(void)
{
    if(!previous)
        return;

    cleanup();
    browser->state = previous;
    printf("Path before: %s\n", browser->path->get_path());
    browser->path->cd("..");
    printf("Path after: %s\n", browser->path->get_path());

    previous->refresh = true;
    previous = NULL; // unlink;
    browser->state->deeper = NULL;
    delete this;
}

void TreeBrowserState :: select_one(void)
{
	if(!under_cursor)
		return;
	if (under_cursor->isSelectable()) {
		under_cursor->setSelection(!under_cursor->getSelection());
		update_selected();
	}
}

void TreeBrowserState :: select_all(bool yesno)
{
	for (int i=0;i<children->get_elements();i++) {
		Browsable *t = (*children)[i];
		if (t->isSelectable()) {
			t->setSelection(yesno);
		}
	}
	draw();
}

void TreeBrowserState :: move_to_index(int idx)
{
	int num_el = children->get_elements();
	if (idx > num_el) {
		idx = num_el;
	}
	cursor_pos = idx;

	if(num_el == 0) {
		first_item_on_screen = -1;
		draw();
		return;
	}

//	if((first_item_on_screen + selected_line)==idx) // duh!
//        return;
	int previous_first_item_on_screen = first_item_on_screen;
	int previous_selected = selected_line;

    // Try to determine the first item on the screen, by
    // stepping half of the screen size up.
	int y = (browser->window->get_size_y() / 2);
	if(idx < y) {
		first_item_on_screen = 0;
		selected_line = idx;
	} else if(num_el <= browser->window->get_size_y()) {
		first_item_on_screen = 0;
		selected_line = idx;
	} else {
		first_item_on_screen = idx - y;

		if(first_item_on_screen + browser->window->get_size_y() >= num_el)
			first_item_on_screen = num_el - browser->window->get_size_y();

		selected_line = idx - first_item_on_screen;
	}
	if ((first_item_on_screen != previous_first_item_on_screen)||(refresh)) { // scroll!
		draw();
	} else {
		draw_item(under_cursor, previous_selected, false);
		under_cursor = (*children)[idx];
		draw_item(under_cursor, selected_line, true);
	}
}
