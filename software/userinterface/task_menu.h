#ifndef TASK_MENU_H
#define TASK_MENU_H

#include <string.h>

#include "menu.h"
#include "browsable.h"
#include "context_menu.h"
#include "path.h"

class TreeBrowserState;

class TaskMenu : public ContextMenu
{
    static bool& task_actions_created()
    {
        static bool created = false;
        return created;
    }
    TreeBrowserState *state;
	Browsable *browsable;
	Path *path;
public:    
    TaskMenu(UserInterface *ui, TreeBrowserState *state, Path *p);
    ~TaskMenu(void);
    
    void init(Window *pwin, Keyboard *keyb);
    int select_item(void);
    static void ensure_task_actions_created(bool writablePath);
    static Action *find_task_action(int subsysId, const char *actionName);
};

class TaskSubMenu : public ContextMenu
{
    TaskCategory *category;
public:
    TaskSubMenu(UserInterface *ui, TreeBrowserState *state, TaskCategory *cat, int first, int item);
    ~TaskSubMenu() { }
    int get_items(void);
};

inline void TaskMenu :: ensure_task_actions_created(bool writablePath)
{
    IndexedList<ObjectWithMenu*> *objects = ObjectWithMenu :: getObjectsWithMenu();

    if (!task_actions_created()) {
        for(int i=0;i<objects->get_elements();i++) {
            (*objects)[i]->create_task_items();
        }
        TasksCollection::sort();
        task_actions_created() = true;

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

    for(int i=0;i<objects->get_elements();i++) {
        (*objects)[i]->update_task_items(writablePath);
    }
}

inline Action *TaskMenu :: find_task_action(int subsysId, const char *actionName)
{
    IndexedList<TaskCategory *> *categories = TasksCollection::getCategories();
    Action *fallback = NULL;

    for (int i = 0; i < categories->get_elements(); i++) {
        TaskCategory *cat = (*categories)[i];
        if (!cat) {
            continue;
        }
        IndexedList<Action *> *actions = cat->getActions();
        for (int j = 0; j < actions->get_elements(); j++) {
            Action *action = (*actions)[j];
            if (action && action->isShown() && action->isEnabled() && (strcmp(action->getName(), actionName) == 0)) {
                if (action->subsys == subsysId) {
                    return action;
                }
                if (!fallback && (action->func || action->direct_func)) {
                    fallback = action;
                }
            }
        }
    }
    return fallback;
}

#endif
