#include "context_menu.h"
#include <string.h>

ContextMenu :: ContextMenu(Contextable *node, int initial, int y) : actions(2, 0)
{
    contextable = node;
    context_state = e_new;
    screen = NULL;
    keyb = NULL;
    window = NULL;
    y_offs = y-1;
    corner = y+2;

    item_index = initial;
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

ContextMenu :: ~ContextMenu(void)
{
}

void ContextMenu :: init(Screen *scr, Keyboard *key)
{
    int len, max_len;
    int rows, size_y;

    keyb = key;

    if(context_state == e_new) {
    	contextable->fetch_context_items(actions);

    	int item_count = actions.get_elements();
    	if(!item_count) {
            printf("No items.. exit.\n");
            context_state = e_finished;
            return;
        }
        rows = item_count + 2;
        size_y = scr->get_size_y()-1;
        if(rows > size_y) {
            rows = size_y;
        }
        if((rows + y_offs) > size_y)
            y_offs = size_y - rows;
        if(y_offs < 2)
            y_offs = 2;
        
        max_len = 0;
        for(int i=0;i<actions.get_elements();i++) {
        	Action *it = actions[i];
            len = strlen(it->getName());
            if(len > max_len)
                max_len = len;
        }
        if(max_len > 25)
            max_len = 25;
    }        

    this->screen = scr;
    window = new Window(scr, scr->get_size_x()-2-max_len, y_offs, max_len+2, rows);
    window->set_color(user_interface->color_fg);
    window->draw_border();
    if((corner == y_offs)||(corner == (y_offs+rows-1))) {
        window->set_char(0, corner-y_offs, 2);
    } else if(corner == 2) {
    	window->set_char(0, corner-y_offs, 8);
    } else {
        window->set_char(0, corner-y_offs, 3);
    }
    context_state = e_active;

    draw();
}
    
void ContextMenu :: executeAction()
{
	actions[item_index]->execute();
}

int ContextMenu :: poll(int dummy, Event &e)
{
    int ret = 0;
    int c;
        
    if(!keyb) {
        printf("ContextMenu: Keyboard not initialized.. exit.\n");
        return -1;
    }

    switch(context_state) {
        case e_active:
            c = keyb->getch();
            if(c > 0) {
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

int ContextMenu :: handle_key(int c)
{
    int ret = 0;
    
    switch(c) {
        case KEY_LEFT: // left
        case KEY_BREAK: // runstop
            //clean_up();
            ret = -1;
            break;
        case KEY_F8: // exit
            ret = -1;
            //clean_up();
            push_event(e_unfreeze, 0);
//            push_event(e_terminate, 0);
            break;
        case KEY_DOWN: // down
        	//reset_quick_seek();
        	if (item_index < actions.get_elements()-1) {
            	item_index++;
            	draw();
        	}
            break;
        case KEY_UP: // up
        	// reset_quick_seek();
        	if (item_index > 0) {
        		item_index --;
        		draw();
        	}
        	break;
        case KEY_BACK: // backspace
/*
            if(quick_seek_length) {
                quick_seek_length--;
                perform_quick_seek();
            }
*/
            break;
        case KEY_SPACE: // space
        case KEY_RETURN: // return
            //clean_up();
            ret = 1;
            break;
            
        default:
            if((c >= '!')&&(c < 0x80)) {
                if(quick_seek_length < (MAX_SEARCH_LEN-2)) {
                    quick_seek_string[quick_seek_length++] = c;
                    if(!perform_quick_seek()) {
                        quick_seek_length--;
                    } else {
                    	draw();
                    }
                }
            } else {
                printf("Unhandled context key: %b\n", c);
            }
    }    
    return ret;
}

bool ContextMenu :: perform_quick_seek(void)
{
    quick_seek_string[quick_seek_length] = '*';
    quick_seek_string[quick_seek_length+1] = 0;
    printf("ContextMenu Performing seek: '%s'\n", quick_seek_string);

    int num_el = this->actions.get_elements();
    for(int i=0;i<num_el;i++) {
    	Action *t = actions[i];
		if(pattern_match(quick_seek_string, t->getName(), false)) {
			item_index = i;
			return true;
		}
    }
    return false;
}

void ContextMenu :: reset_quick_seek(void)
{
    quick_seek_length = 0;
}

void ContextMenu :: draw()
{
	printf("Drawing context menu. Window height = %d\n", window->get_size_y()); // TODO
	for (int i=0;i<window->get_size_y();i++) {
		Action *t = actions[i];
		window->move_cursor(0, i);
		if (i == item_index) {
			window->set_color(user_interface->color_sel);
		} else {
			window->set_color(user_interface->color_fg);
		}
		window->output_line(t->getName());
	}
}

