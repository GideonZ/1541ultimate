#include "context_menu.h"
#include "subsys.h"
#include <string.h>
#include "tree_browser.h"
#include "tree_browser_state.h"

ContextMenu :: ContextMenu(UserInterface *ui, TreeBrowserState *state, int initial, int y, int when_done, int ind) : actions(2, 0)
{
	user_interface = ui;
	this->state = state;
    this->when_done = when_done;
	if (state)
		contextable = state->under_cursor;
	else
		contextable = NULL;

	selectedAction = NULL;
	context_state = e_new;
    screen = NULL;
    keyb = NULL;
    window = NULL;
    y_offs = y-1;
    corner = y;
    first = 0;
    hook_y = 0;
    indent = ind;
    quick_seek_length = 0;
    item_index = initial;
    subContext = NULL;
}

// y_offs = line above selected, relative to window
// corner = line of selected, relative to window

ContextMenu :: ~ContextMenu(void)
{
    for(int i=0;i<actions.get_elements();i++) {
        Action *act = actions[i];
        if (!(act->isPersistent())) {
            delete act;
        }
    }
}

int ContextMenu :: get_items(void)
{
    if(contextable) {
        contextable->fetch_context_items(actions);
    }
    return actions.get_elements();
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
        int item_count = get_items();

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
            if (len == 0) {
                len = 8;   // "" will be transformed to "- None -"
            }
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

void ContextMenu :: help()
{
    reset_quick_seek();
    if(state)
        state->refresh = true;
    //user_interface->run_editor(helptext, strlen(helptext));
}

int ContextMenu :: poll(int sub)
{
    int ret = 0;
    int c;
        
    if (subContext) {
        if (sub < 0) {
            delete subContext;
            subContext = NULL;
            draw();
            return 0;
        } else if (sub > 0) {
            selectedAction = subContext->getSelectedAction();
            delete subContext;
            subContext = NULL;
            draw();
        }
        return sub;
    } else if(sub < 0) {
        redraw();
    }
    if(!keyb) {
        printf("ContextMenu: Keyboard not initialized.. exit.\n");
        return when_done;
    }

    switch(context_state) {
        case e_active:
            c = keyb->getch();
            if(c > 0) {
                ret = handle_key(c);
                if(ret)
                    context_state = e_finished;
            } else if(c == -2) {
            	ret = when_done;
            }
            break;
        
        default:
            ret = when_done;
            break;
    }
    return ret;
}

void ContextMenu :: page_up(void)
{
    int newpos = item_index - 10;
    if (newpos < 0) {
        newpos = 0;
    }
    while(actions[newpos] && !actions[newpos]->isEnabled() && (newpos < actions.get_elements()-1)) {
        newpos++;
    }
    if (newpos != item_index) {
        item_index = newpos;
        reset_quick_seek();
        draw();
    }
}

void ContextMenu :: page_down(void)
{
    int newpos = item_index + 10;
    if (newpos >  actions.get_elements()-1) {
        newpos = actions.get_elements()-1;
    }
    while(actions[newpos] && !actions[newpos]->isEnabled() && (newpos > 0)) {
        newpos--;
    }
    if (newpos != item_index) {
        item_index = newpos;
        reset_quick_seek();
        draw();
    }
}

void ContextMenu :: seek_char(int c)
{
    if(quick_seek_length < (MAX_SEARCH_LEN-2)) {
        quick_seek_string[quick_seek_length++] = c;
        if(!perform_quick_seek()) {
            quick_seek_length--;
        } else {
            draw();
        }
    }
}

void ContextMenu :: down(void)
{
    reset_quick_seek();
    for (int i=item_index+1; i < actions.get_elements(); i++) {
        if (actions[i]->isEnabled()) {
            item_index = i;
            draw();
            break;
        }
    }
}

void ContextMenu :: up(void)
{
    reset_quick_seek();
    for (int i=item_index-1; i >= 0; i--) {
        if (actions[i]->isEnabled()) {
            item_index = i;
            draw();
            break;
        }
    }
}

int ContextMenu :: handle_key(int c)
{
    int ret = 0;
    Action *a;
    
    switch(c) {
        case KEY_LEFT:  // left
        case KEY_BREAK: // runstop
        case KEY_BACK:  // backspace
        case KEY_ESCAPE:
        case KEY_F8:    // exit
        	ret = when_done;
            break;

        case KEY_DOWN: // down
            down();
        	break;

        case KEY_UP: // up
            up();
        	break;

        case KEY_F1: // page up or menu
            if (user_interface->navmode == 0) {
                page_up();
            }
            break;

        case KEY_F3: // help or page up
            if (user_interface->navmode == 0) {
                help();
            } else {
                page_up();
            }
            break;

        case KEY_F5:
            if (user_interface->navmode == 0) {
                ;
            } else {
                page_down();
            }
            break;

        case KEY_F7: // page down or help
            if (user_interface->navmode == 0) {
                page_down();
            } else {
                help();
            }
            break;

        case KEY_PAGEDOWN: // page down
            page_down();
            break;

        case KEY_PAGEUP:
            page_up();
            break;

        case KEY_SPACE: // space
        case KEY_RETURN: // return
            ret = select_item();
            break;
            
        case KEY_RIGHT: // cursor
            a = actions[item_index];
            // only select if it has a submenu, otherwise require enter / space
            if (a && (a->getObject() || a->func || a->direct_func)) {
                ret = select_item();
            }
            break;

        case KEY_A:
            if (user_interface->navmode == 0) {
                seek_char('a');
            } else {
                ret = when_done;
            }
            break;
        case KEY_S:
            if (user_interface->navmode == 0) {
                seek_char('s');
            } else {
                down();
            }
            break;
        case KEY_D:
            if (user_interface->navmode == 0) {
                seek_char('d');
            } else {
                select_item();
            }
            break;
        case KEY_W:
            if (user_interface->navmode == 0) {
                seek_char('w');
            } else {
                up();
            }
            break;

        default:
            if ((user_interface->navmode == 0) && (c >= '!') && (c < 0x80)) {
                seek_char(c);
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
    //window->set_background(user_interface->color_bg);

    // for the hook, use the old size parameters
    int rows = window->get_size_y();
    int oy = y_offs - hook_y;

    window->draw_border();
    if((corner == oy)||(corner == (oy+rows-1))) {
        window->set_char(0, corner-oy, CHR_HORIZONTAL_LINE);
    } else if(corner == 0) {
        window->set_char(0, corner-oy, CHR_COLUMN_BAR_TOP);
    } else {
        window->set_char(0, corner-oy, CHR_LOWER_LEFT_CORNER);
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
		} else if(t && t->isEnabled()) {
			window->set_color(user_interface->color_fg);
            window->set_background(0);
		} else { // not enabled
	        window->set_color(11); // TODO
	        window->set_background(0);
		}
		if (t) {
			const char *string = t->getName();
			window->output_line(*string ? string : "- None -", indent);  // Action name or "- None -"
		} else {
			window->output_line("");
		}
	}
}

int ContextMenu :: select_item(void)
{
    selectedAction = actions[item_index];
    return 1;
}
