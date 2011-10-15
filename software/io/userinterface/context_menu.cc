#include "context_menu.h"
#include <string.h>

ContextMenu :: ContextMenu(PathObject *n, PathObject *obj, int initial, int y)
{
    parent_win = NULL;
    object = obj;
    context_state = e_new;
    keyb = NULL;
    window = NULL;
    y_offs = y-1;
    corner = y;

    state->initial_index = initial;
    state->node = n;
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

ContextMenu :: ~ContextMenu(void)
{
}

void ContextMenu :: init(Screen *scr, Keyboard *key)
{
    parent_win = scr;
    keyb = key;

    if(context_state == e_new) {
        items = object->fetch_context_items(state->node->children);
        if(!items) {
            printf("No items.. exit.\n");
            context_state = e_finished;
            return;
        }
        rows = items + 2;
        size_y = parent_win->get_size_y();
        if(rows > size_y) {
            rows = size_y;
        }
        if((rows + y_offs) > size_y)
            y_offs = size_y - rows;
        if(y_offs < 0)
            y_offs = 0;
        
        max_len = 0;
        for(int i=0;i<state->node->children.get_elements();i++) {
        	PathObject *it = state->node->children[i];
			//it->attach();
            len = strlen(it->get_name());
            if(len > max_len)
                max_len = len;
        }
        if(max_len > 25)
            max_len = 25;
    }        

    window = new Screen(parent_win, parent_win->get_size_x()-2-max_len, y_offs+2, max_len+2, rows);
    window->move_cursor(0, corner-y_offs);
    char *hook = window->get_pointer();
    window->draw_border();
    if((corner == y_offs)||(corner == (y_offs+rows-1)))
        *hook = 2;
    else
        *hook = 3;
    context_state = e_active;
	state->do_refresh();
}
    

int ContextMenu :: poll(int dummy, Event &e)
{
    int ret = 0;
    char c;
        
    if(!keyb) {
        printf("ContextMenu: Keyboard not initialized.. exit.\n");
        return -1;
    }

    if(e.type == e_invalidate) {
    	invalidate((PathObject *)e.object);
    	return 0;
    }

    switch(context_state) {
        case e_active:
            if(state->refresh)
                state->do_refresh();

            c = keyb->getch();
            if(c) {
                printf("%c", c);
                ret = handle_key(c);
                if(ret)
                    context_state = e_finished;
            }
            break;
        
        default:
            ret = -1;
            break;
    }
    return ret;
}

int ContextMenu :: handle_key(char c)
{
    int ret = 0;
    
    switch(c) {
        case 0x9D: // left
        case 0x03: // runstop
            //clean_up();
            ret = -1;
            break;
        case 0x8C: // exit
            ret = -1;
            //clean_up();
            push_event(e_unfreeze, 0);
//            push_event(e_terminate, 0);
            break;
        case 0x11: // down
        	reset_quick_seek();
            state->down(1);
            break;
        case 0x91: // up
        	reset_quick_seek();
            state->up(1);
            break;
        case 0x14: // backspace
            if(quick_seek_length) {
                quick_seek_length--;
                perform_quick_seek();
            }
            break;
        case 0x20: // space
        case 0x0D: // return
            //clean_up();
            ret = 1;
            break;
            
        default:
            if((c >= '!')&&(c < 0x80)) {
                if(quick_seek_length < (MAX_SEARCH_LEN-2)) {
                    quick_seek_string[quick_seek_length++] = c;
                    if(!perform_quick_seek())
                        quick_seek_length--;
                }
            } else {
                printf("Unhandled context key: %b\n", c);
            }
    }    
    return ret;
}
