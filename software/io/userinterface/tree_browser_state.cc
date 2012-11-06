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

/*****************************/
/* Tree Browser State Object */
/*****************************/
TreeBrowserState :: TreeBrowserState(PathObject *n, TreeBrowser *b, int lev)
{
	browser = b;
	level = lev;
	node = n;
    first_item_on_screen = -1;
    selected_line = 0;
    selected = NULL;

    refresh = true;
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
        draw();
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
    browser->window->set_color(user_interface->color_fg);

    if(node->children.get_elements() == 0) {
		browser->window->clear();
    	browser->window->move_cursor(0, 0);
    	browser->window->output("\033\025< No Items >");
    	selected = NULL;
    	return;
    }

    int y = browser->window->get_size_y(); // how many can I show?
//    printf("TreeBrowserState::Draw: first=%d, y=%d, selected_line=%d\n", first_item_on_screen, y, selected_line);

    if(first_item_on_screen < 0)
        return;
    
    PathObject *t;
    for(int i=0;i<y;i++) {
    	t = node->children[i+first_item_on_screen];

        browser->window->move_cursor(0, i);
        if(t)
			browser->window->output_line(t->get_display_string());
		else
			browser->window->output_line("");
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
    browser->window->set_color(user_interface->color_sel); // highlighted
    browser->window->output_line(selected->get_display_string());
}
    
void TreeBrowserState :: unhighlight()
{
    browser->window->set_color(user_interface->color_fg, 0, selected_line, 30, 1, false);
//    window->reverse(0, selection_index, 40);
}
    
void TreeBrowserState :: highlight()
{
    browser->window->set_color(0x60+user_interface->color_sel, 0, selected_line, 30, 1, false);
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
			browser->window->set_color(user_interface->color_fg);
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
			browser->window->set_color(user_interface->color_fg);
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
    int header = node->get_header_lines();
// ####    
	if(selected_line + first_item_on_screen >= node->children.get_elements()) {
		move_to_index(node->children.get_elements()-1);
	}
	selected = node->children[selected_line + first_item_on_screen];
}

void TreeBrowserState :: reload(void)
{
//	printf("Reload. Before: \n");
//	root.dump();
	int child_count = node->children.get_elements();
	for(int i=0;i<child_count;i++) {
		node->children[i]->detach(true);
	}
//	printf("After detach of children:\n");
//	root.dump();
	node->cleanup_children();
//	printf("After cleanup of children:\n");
//	root.dump();
	node->fetch_children();
//	printf("After fetch of children:\n");
//	root.dump();
	child_count = node->children.get_elements();
	for(int i=0;i<child_count;i++) {
		node->children[i]->attach(true);
	}
	reselect();
	refresh = true;
}

void TreeBrowserState :: into(void)
{
	if(!selected)
		return;

	printf("Going deeper into = %s\n", selected->get_name());
    int child_count = selected->fetch_children();
    reselect(); // might have been promoted..
    if(child_count < 0)
        return;
        
    printf("%d children fetched.\n", child_count);

/*
    if(selected->children.is_empty())
        return;
*/
        
	child_count = selected->children.get_elements();
	for(int i=0;i<child_count;i++) {
		selected->children[i]->attach(true);
	}
	deeper = new TreeBrowserState(selected, browser, level+1);
    user_interface->set_path(selected);
    browser->state = deeper;
    deeper->previous = this;
}

void TreeBrowserState :: level_up(void)
{
    if(!previous)
        return;

	int child_count = previous->selected->children.get_elements();
	for(int i=0;i<child_count;i++) {
		previous->selected->children[i]->detach(true);
	}
	previous->selected->cleanup_children();
    browser->state = previous;
    user_interface->set_path(previous->node);
    previous->refresh = true;
    previous = NULL; // unlink;
    delete this;
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

	if(first_item_on_screen + browser->window->get_size_y() >= num_el)
		first_item_on_screen = num_el - browser->window->get_size_y();

	selected_line = idx - first_item_on_screen;

    draw();    
}
