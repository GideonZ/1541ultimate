#include "context_menu.h"
#include "subsys.h"
#include <string.h>
#include "tree_browser.h"
#include "tree_browser_state.h"

ContextMenu :: ContextMenu(UserInterface *ui, TreeBrowserState *state, int initial, int y) : actions(2, 0)
{
	user_interface = ui;
	this->state = state;

	if (state)
		contextable = state->under_cursor;
	else
		contextable = NULL;

    context_state = e_new;
    screen = NULL;
    keyb = NULL;
    window = NULL;
    y_offs = y-1;
    corner = y;
    first = 0;
    hook_y = 0;
    quick_seek_length = 0;
    item_index = initial;
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

ContextMenu :: ~ContextMenu(void)
{
	for(int i=0;i<actions.get_elements();i++) {
		delete actions[i];
	}
}

void ContextMenu :: init(Window *parwin, Keyboard *key)
{
	Screen *scr = parwin->getScreen();
	int len, max_len;
    int rows, size_y;

    keyb = key;

    int ox;
    parwin->getOffsets(ox, hook_y);

    if(context_state == e_new) {
    	contextable->fetch_context_items(actions);

    	int item_count = actions.get_elements();
    	if(!item_count) {
            printf("No items.. exit.\n");
            context_state = e_finished;
            return;
        }
        rows = item_count + 2;
        size_y = parwin->get_size_y();
        if(rows > size_y) {
            rows = size_y;
        }
        if((rows + y_offs) > size_y)
            y_offs = size_y - rows;

        if(y_offs < 0)
            y_offs = 0;
        
        y_offs += hook_y;

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
    window = new Window(scr, ox + parwin->get_size_x()-2-max_len, y_offs, max_len+2, rows);
    context_state = e_active;
    redraw();
}

void ContextMenu :: deinit()
{
    if (window) {
        window->reset_border();
    }
}

void ContextMenu :: executeAction()
{
	Action *act = actions[item_index];
	const char *filename = (contextable)?(contextable->getName()):"";
	TreeBrowser *bb = state->browser;
	SubsysCommand *cmd = new SubsysCommand(user_interface, act, bb->getPath(), filename);
	cmd->execute();
	// TODO: do something with return value / data
}

int ContextMenu :: poll(int dummy)
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
            } else if(c == -2) {
            	ret = -1;
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
    int newpos;
    
    switch(c) {
        case KEY_LEFT: // left
        case KEY_BREAK: // runstop
        case KEY_ESCAPE:
        	ret = -1;
            break;
        case KEY_F8: // exit
            ret = -1;
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
        case KEY_F1: // page up
        case KEY_PAGEUP:
            newpos = item_index - 10;
            if (newpos < 0) {
                newpos = 0;
            }
            if (newpos != item_index) {
                item_index = newpos;
                draw();
            }
            break;

        case KEY_F7: // page up
        case KEY_PAGEDOWN:
            newpos = item_index + 10;
            if (newpos >  actions.get_elements()-1) {
                newpos = actions.get_elements()-1;
            }
            if (newpos != item_index) {
                item_index = newpos;
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

void ContextMenu :: redraw()
{
    window->set_color(user_interface->color_fg);
    window->set_background(user_interface->color_bg);

    // for the hook, use the old size parameters
    int rows = window->get_size_y();
    int oy = y_offs - hook_y;

    window->draw_border();
    if((corner == oy)||(corner == (oy+rows-1))) {
        window->set_char(0, corner-oy, 2);
    } else if(corner == 0) {
        window->set_char(0, corner-oy, 8);
    } else {
        window->set_char(0, corner-oy, 3);
    }
    draw();
}

void ContextMenu :: draw()
{
	if ((item_index - first) >= window->get_size_y()) {
		first = item_index - window->get_size_y() + 1;
	} else if (item_index < first) {
		first = item_index;
	}

	for (int i=0;i<window->get_size_y();i++) {
		Action *t = actions[i + first];
		window->move_cursor(0, i);
		if ((i + first) == item_index) {
			window->set_color(user_interface->color_sel);
			window->set_background(user_interface->color_sel_bg);
		} else {
			window->set_color(user_interface->color_fg);
            window->set_background(0);
		}
		window->output_line(t->getName());
	}
}
