#include "task_menu.h"
#include "globals.h"
#include <string.h>

TaskMenu :: TaskMenu() : ContextMenu(NULL, 0, 0)
{
    screen = NULL;
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
    screen = scr;
    keyb = key;

    int len, max_len;
    int rows, size_y;

    IndexedList<ObjectWithMenu*> *objects = Globals :: getObjectsWithMenu();

    if(context_state == e_new) {
        for(int i=0;i<objects->get_elements();i++) {
        	(*objects)[i]->fetch_task_items(actions);
        }
        // debug
        // items += root.fetch_task_items(state->node->children);
        // end debug
        int items = actions.get_elements();
        if(!items) {
            printf("No items.. exit.\n");
            context_state = e_finished;
            return;
        } else {
            printf("Number of items: %d\n", items);
        }
        rows = items + 2;
        size_y = screen->get_size_y();
        if(rows > size_y) {
            rows = size_y;
        }
		y_offs = (size_y - rows) >> 1;
        
        max_len = 0;
        for(int i=0;i<items;i++) {
        	Action *a = actions[i];
            len = strlen(a->getName());
            if(len > max_len)
                max_len = len;
        }
        if(max_len > 30)
            max_len = 30;
    }

    window = new Window(screen, 19-(max_len>>1), y_offs+2, max_len+2, rows);
    window->set_color(user_interface->color_fg);
    window->draw_border();
    context_state = e_active;

    draw();
}
