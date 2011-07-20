#include "menu.h"
#include "task_menu.h"
#include <string.h>

IndexedList<ObjectWithMenu*> main_menu_objects(16, NULL);

TaskMenu :: TaskMenu(PathObject *n, PathObject *obj) : ContextMenu(n, obj, 0, 0)
{
    parent_win = NULL;
    context_state = e_new;
    keyb = NULL;
    window = NULL;
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

TaskMenu :: ~TaskMenu(void)
{
}

void TaskMenu :: init(Screen *scr, Keyboard *key)
{
    parent_win = scr;
    keyb = key;

    if(context_state == e_new) {
    	items = object->fetch_task_items(state->node->children);
        for(int i=0;i<main_menu_objects.get_elements();i++) {
        	items += main_menu_objects[i]->fetch_task_items(state->node->children);
        }
        // debug
        // items += root.fetch_task_items(state->node->children);
        // end debug
        if(!items) {
            printf("No items.. exit.\n");
            context_state = e_finished;
            return;
        } else {
            printf("Number of items: %d\n", items);
        }
        rows = items + 2;
        size_y = parent_win->get_size_y();
        if(rows > size_y) {
            rows = size_y;
        }
		y_offs = (size_y - rows) >> 1;
        
        max_len = 0;
        for(int i=0;i<state->node->children.get_elements();i++) {
        	PathObject *it = state->node->children[i];
            len = strlen(it->get_name());
            if(len > max_len)
                max_len = len;
        }
        if(max_len > 30)
            max_len = 30;
    }/* else {
		printf("Task menu init called for a non-new structure, making a dump:\n");
		state->node->dump();
    } */

    window = new Screen(parent_win, 19-(max_len>>1), y_offs+2, max_len+2, rows);
    window->draw_border();
    context_state = e_active;
	state->do_refresh();
}

