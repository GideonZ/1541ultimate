#include <stdio.h>
#include "task_menu.h"
#include <string.h>
#include "tree_browser_state.h"
#include "filemanager.h"
bool TaskMenu :: actionsCreated = false;

TaskMenu :: TaskMenu(UserInterface *ui, TreeBrowserState *state, Path *p) : ContextMenu(ui, state, 0, 0)
{
	path = p;
	this->state = state;
	if(state)
		browsable = state->node;
	else
		browsable = NULL;
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

void TaskMenu :: init(Window *pwin, Keyboard *key)
{
    screen = pwin->getScreen();
    keyb = key;

    IndexedList<ObjectWithMenu*> *objects = ObjectWithMenu :: getObjectsWithMenu();

    // We do this only once
    if (!actionsCreated) {
        for(int i=0;i<objects->get_elements();i++) {
            (*objects)[i]->create_task_items();
        }
        TasksCollection::sort();
        actionsCreated = true;

        // Set all actions that we collect here to persistent, so that they won't get cleaned up
        IndexedList<TaskCategory *> *categories = TasksCollection::getCategories();
        for (int i=0; i < categories->get_elements(); i++) {
            TaskCategory *cat = (*categories)[i];
            IndexedList<Action *> *catActions = cat->getActions();
            for (int j=0; j < catActions->get_elements(); j++) {
                Action *a = (*catActions)[j];
                a->setPersistent();
            }
        }
    }

    int len, max_len;
    int rows, size_y;

    bool writablePath = FileManager :: getFileManager() -> is_path_writable(path);
    if(context_state == e_new) {

        for(int i=0;i<objects->get_elements();i++) {
        	(*objects)[i]->update_task_items(writablePath, path); // This should actually update them, according to the current state of each object
        }

        IndexedList<TaskCategory *> *categories = TasksCollection::getCategories();
        for (int i=0; i < categories->get_elements(); i++) {
            TaskCategory *cat = (*categories)[i];
            Action *catAction = new Action(cat->getName(), 0, 0, 0);
            catAction->setObject(cat);
            actions.append(catAction);
        }

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
        if(rows > size_y-4) {
            rows = size_y-4;
        } else if (rows < 8) {
        	rows = 8;
        }
		y_offs = (size_y - rows) >> 1;
        
/*
        max_len = 0;
        for(int i=0;i<items;i++) {
        	Action *a = actions[i];
            len = strlen(a->getName());
            if(len > max_len)
                max_len = len;
        }
        if(max_len > 30)
            max_len = 30;
*/
    }
    max_len = 28;

    window = new Window(screen, (screen->get_size_x() - max_len - 2) >> 1, y_offs, max_len+2, rows);
    window->set_color(user_interface->color_fg);
    window->draw_border();
    context_state = e_active;

    draw();
}

int TaskMenu :: select_item(void)
{
    Action *a = actions[item_index];
    TaskCategory *cat = (TaskCategory *)a->getObject();
    printf("Action Category selected: %s\n", actions[item_index]->getName());

    if (!a) {
        return 1;
    }
    int first_selectable_sub_item = 0;
    IndexedList <Action *> *actionlist = cat->getActions();
    int num_el = actionlist->get_elements();

    Action *singleAction = (*actionlist)[0];
    if ((num_el == 1) && (singleAction->isEnabled())) { // Only one enabled item, so we can just execute it
        selectedAction = singleAction;
        return 1; // done
    }

    for(int i=0; i < num_el; i++) {
        Action *a = (*actionlist)[i];
        if(a->isEnabled()) {
            first_selectable_sub_item = i;
            break;
        }
    }
    subContext = new TaskSubMenu(user_interface, state, cat, first_selectable_sub_item, item_index);
    subContext->init(window, keyb);
    user_interface->activate_uiobject(subContext);
    return 0;
}

TaskSubMenu :: TaskSubMenu(UserInterface *ui, TreeBrowserState *state, TaskCategory *cat, int first, int item) : ContextMenu(ui, state, first, item), category(cat)
{

}

int TaskSubMenu :: get_items(void)
{
    int itms = 0;
    int enabled = 0;
    IndexedList<Action *> *catActions = category->getActions();
    for (int j=0; j < catActions->get_elements(); j++) {
        Action *a = (*catActions)[j];
        if (a->isShown()) {
            appendAction(a);
            itms++;
            if (a->isEnabled()) {
                enabled++;
            }
        }
    }
    if (enabled == 0) {
        return 0;
    }
    return itms;
}
