#include "stream_menu.h"
#include "task_menu.h"

#include "usb.h"

char *decimals = "0123456789";

StreamMenu :: StreamMenu(Stream *s, PathObject *n)
{
    stream = s;
    state = 0;
    node = n;
    menu = NULL;
    user_input[0] = 0;
    
    print_items(0, 9999);
}

void StreamMenu :: print_items(int start, int stop)
{
    PathObject *n = (menu) ? menu : node;
    if(stop > n->children.get_elements())
        stop = n->children.get_elements();
            
    if(n->children.get_elements() == 0) {
        stream->format("< No Items >\n");
    } else if(start >= stop) {
        stream->format("< No more items.. >\n");
    } else {
        for(int i=start;i<stop;i++) {
            stream->format("%3d. %s\n", 1+i, n->children[i]->get_display_string());
        }
    }
}

int StreamMenu :: poll(Event &e)
{
	if(e.type == e_browse_into) {
        into();
        print_items(0, 9999);
        return -1;
    } else if(e.type == e_invalidate) {
    	invalidate((PathObject *)e.object);
    	return -1;
    }
        
    if(stream->getstr(user_input, 31) >= 0) {
        stream->format("You entered: %s\n", user_input);
        int res = process_command(user_input);
        user_input[0] = '\0';
        return res;
    }
    return -1;
}

int StreamMenu :: into(void)
{
    if(menu) {
        delete menu;
        menu = NULL;
        state = 0;
    }
        
    if(selected > node->children.get_elements())
        return -1;
    if(selected < 1)
        return -1;
            
    node = node->children[selected-1];
    return node->fetch_children();
}

void StreamMenu :: invalidate(PathObject *obj)
{
    stream->format("Invalidation event: %s\n", obj->get_name());

    PathObject *n = node;
    bool found = false;
    while(n) {
        printf("Considering %s\n", n->get_name());
        if(n == obj) {
            found = true;
            break;
        }
        n = n->parent;
    }
    
    if(!found) {
        stream->format("Invalidate doesn't affect us.\n");
        return;
    }
    
    if(menu) {
        delete menu;
        menu = NULL;
        state = 0;
    }
    node = n->parent;
    node->fetch_children();
    print_items(0, 9999);
}
    
int StreamMenu :: process_command(char *command)
{
    int items;
    
    if (strcmp(command, "up")==0) {
        if(menu) { // if inside menu
            menu = NULL;
            state = 0;
        }
        else if(node->parent)
            node = node->parent;
        print_items(0, 9999);
    }
    else if ((strncmp(command, "usbstat", 7))==0) {
        usb.print_status();
    }
    else if ((strncmp(command, "into ", 5))==0) {
        selected = stream->convert_in(&command[5], 10, decimals); 
        items = into();
        if(items < 0) {
            stream->format("Invalid choice: %d\n", selected);
        } else {
            print_items(0, 9999);
        }                                    
    } 
    else if ((strcmp(command, "task")==0) and (state == 0)) {
        menu = new PathObject(NULL);
        items = node->fetch_task_items(menu->children);
        for(int i=0;i<main_menu_objects.get_elements();i++) {
        	items += main_menu_objects[i]->fetch_task_items(menu->children);
        }
        print_items(0, items);
        state = 1;
    }
    else { 
        selected = stream->convert_in(command, 10, decimals); 
        switch(state) {
            case 0: // default browse
                if((selected > 0)&&(selected <= node->children.get_elements())) {
                    stream->format("You selected %s. Creating context.\n", node->children[selected-1]->get_name());
                    menu = new PathObject(NULL);
                    items = node->children[selected-1]->fetch_context_items(menu->children);
                    if(items == 0) { // no context available; try to browse into
                        stream->format("No context items. Browsing into item..\n");
                        items = into();
                    } else {
                        state = 2;
                    }
                    print_items(0, items);
                } else {
                    stream->format("Invalid choice.. Try again.\n");
                    print_items(0, 9999);
                }
                break;
            case 1: // task menu
            case 2: // context menu
                if((selected > 0)&&(selected <= menu->children.get_elements())) {
                    // execute
                    menu->children[selected-1]->execute(0);
                    state = 0;
                    delete menu;
                    menu = NULL;
                } else {
                    stream->format("Invalid task or context choice.. Try again.\n");
                    print_items(0, 9999);
                }
                break;
            default:
                state = 0;
                stream->format("Recovered from invalid state.\n");
        }
    }
}
