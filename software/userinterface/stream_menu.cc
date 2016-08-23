#include "menu.h"
#include "stream_menu.h"
#include "task_menu.h"
#include "filemanager.h"
#include "subsys.h"

IndexedList<Browsable *>emptyListB(0, NULL);

StreamMenu :: StreamMenu(UserInterface *ui, Stream *s, Browsable *n) : nodeStack(20, NULL), listStack(20, NULL), actionList(0, NULL)
{
	user_interface = ui;
	stream = s;
    state = 0;
    root = n;
    node = n;
    user_input[0] = 0;
    
    int error;
    currentList = n->getSubItems(error);
    // listStack.push(currentList);

    currentPath = FileManager :: getFileManager() -> get_new_path("Stream Menu");
    print_items(0, 9999);
}

StreamMenu :: ~StreamMenu()
{
	IndexedList<Browsable *> *list;
	root->killChildren();
	cleanupActions();
	FileManager :: getFileManager() -> release_path(currentPath);
}

void StreamMenu :: print_items(int start, int stop)
{
    if(stop > currentList->get_elements())
        stop = currentList->get_elements();
            
    char buffer[52];

    printf("%d-%d\n", start, stop);
    if(currentList->get_elements() == 0) {
        stream->format("< No Items >\n");
    } else if(start >= stop) {
        stream->format("< No more items.. >\n");
    } else {
        for(int i=start;i<stop;i++) {
        	(*currentList)[i]->getDisplayString(buffer, 40);
        	stream->format("%3d. %s\n", 1+i, buffer);
        }
    }
    printf("State = %d. Depth: %d\n", state, listStack.get_count());
}

void StreamMenu :: print_actions()
{
    for(int i=0;i<actionList.get_elements();i++) {
        stream->format("%3d. %s\n", 1+i, actionList[i]->getName());
    }
}

int StreamMenu :: poll()
{
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
    if(state > 0) {
    	cleanupActions();
        state = 0;
    }
        
    if(selected > currentList->get_elements())
        return -1;
    if(selected < 1)
        return -1;

    Browsable *selNode = (*currentList)[selected-1];

    int result;
    IndexedList<Browsable *>*newList;
    newList  = selNode->getSubItems(result);
    if (result < 0) {
    	printf("Browsing into %s is not possible.\n", selNode->getName());
    	return result;
    }
            
    currentPath->cd(selNode->getName());
    listStack.push(currentList);
    nodeStack.push(node);

    node = selNode;
    currentList = newList;
    return currentList->get_elements();
}

/*
void StreamMenu :: invalidate(CachedTreeNode *obj)
{
    stream->format("Invalidation event: %s\n", obj->get_name());

    CachedTreeNode *n = node;
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
*/
    
int StreamMenu :: process_command(char *command)
{
    int items, result;
    Action *act;
    SubsysCommand *cmd;
    
    if (strcmp(command, "up")==0) {
        if(state > 0) { // if inside menu
        	cleanupActions();
            state = 0;
        }
        else if(listStack.get_count() > 0) {
        	cleanupBrowsables(*currentList);
        	delete currentList;

        	currentList = listStack.pop();
        	node = nodeStack.pop();
        	currentPath->cd("..");

        	node->killChildren(); // reload
        	currentList = node->getSubItems(result); // reload
        }
		print_items(0, 9999);
    }
    else if ((strncmp(command, "into ", 5))==0) {
        sscanf(&command[5], "%d", &selected);
        items = into();
        if(items < 0) {
            stream->format("Invalid choice: %d\n", selected);
        } else {
            print_items(0, 9999);
        }                                    
    } 
    else if ((strcmp(command, "task")==0) and (state == 0)) {
        items = 0;// node->fetch_task_items(menu->children);
        IndexedList<ObjectWithMenu *>*objects = ObjectWithMenu ::getObjectsWithMenu();

        for(int i=0;i<objects->get_elements();i++) {
        	items += (*objects)[i]->fetch_task_items(currentPath, actionList);
        }
        print_actions();
        state = 1;
    }
    else { 
    	int value = 0;
    	sscanf(command, "%d", &value);
        switch(state) {
            case 0: // default browse
            	if((value > 0)&&(value <= currentList->get_elements())) {
                    selected = value;
                    Browsable *b = (*currentList)[selected-1];
                    stream->format("You selected %s. Creating context.\n", b->getName());
                    b->fetch_context_items(actionList);
                    items = actionList.get_elements();
                    if(items < 1) { // no context available; try to browse into
                        stream->format("No context items. Browsing into item..\n");
                        items = into();
                        print_items(0, items);
                    } else {
                        state = 2;
                        print_actions();
                    }
                } else {
                    stream->format("Invalid choice.. Try again.\n");
                    print_items(0, 9999);
                }
                break;
            case 1: // task menu
            case 2: // context menu
                if((value > 0)&&(value <= actionList.get_elements())) {
                	Browsable *b = (*currentList)[selected-1];
                	// execute
                	act = actionList[value-1];
                	SubsysCommand *cmd = new SubsysCommand(user_interface, act, currentPath->get_path(), b->getName());
                	cmd->print();
                	cmd->execute();
                    cleanupActions();
                    state = 0;
                } else {
                    stream->format("Invalid task or context choice.. Try again.\n");
                    print_actions();
                }
                break;
            default:
                state = 0;
                cleanupActions();
                stream->format("Recovered from invalid state.\n");
        }
    }
    return 1;
}

void StreamMenu :: cleanupBrowsables(IndexedList<Browsable *> &list)
{
	for (int i=0;i<list.get_elements();i++) {
		delete list[i];
	}
	list.clear_list();
}

void StreamMenu :: cleanupActions()
{
	for (int i=0;i<actionList.get_elements();i++) {
		delete actionList[i];
	}
	actionList.clear_list();
}

void StreamMenu :: testStack()
{
	printf("There are now %d elements on the stack.\n", nodeStack.get_count());
    Browsable *b1, *b2, *b3;
    b1 = nodeStack.pop();
    b2 = nodeStack.pop();
    b3 = nodeStack.pop();
    if (b1) { printf("B1: %s\n", b1->getName()); }
    if (b2) { printf("B2: %s\n", b2->getName()); }
    if (b3) { printf("B3: %s\n", b3->getName()); }
    if(b3) {
    	nodeStack.push(b3);
    }
    if(b2) {
    	nodeStack.push(b2);
    }
    if(b1) {
    	nodeStack.push(b1);
    }
}
