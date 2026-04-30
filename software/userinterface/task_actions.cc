#include <string.h>

#include "task_actions.h"
#include "menu.h"
#include "tasks_collection.h"

namespace {

bool task_actions_created = false;

}

void ensure_task_actions_created(bool writablePath)
{
    IndexedList<ObjectWithMenu*> *objects = ObjectWithMenu :: getObjectsWithMenu();

    if (!task_actions_created) {
        for (int i = 0; i < objects->get_elements(); i++) {
            (*objects)[i]->create_task_items();
        }
        TasksCollection::sort();
        task_actions_created = true;

        IndexedList<TaskCategory *> *categories = TasksCollection::getCategories();
        for (int i = 0; i < categories->get_elements(); i++) {
            TaskCategory *cat = (*categories)[i];
            IndexedList<Action *> *catActions = cat->getActions();
            for (int j = 0; j < catActions->get_elements(); j++) {
                Action *action = (*catActions)[j];
                action->setPersistent();
            }
        }
    }

    for (int i = 0; i < objects->get_elements(); i++) {
        (*objects)[i]->update_task_items(writablePath);
    }
}

Action *find_task_action(int subsysId, const char *actionName)
{
    IndexedList<TaskCategory *> *categories = TasksCollection::getCategories();
    Action *fallback = NULL;

    for (int i = 0; i < categories->get_elements(); i++) {
        TaskCategory *category = (*categories)[i];
        if (!category) {
            continue;
        }

        IndexedList<Action *> *actions = category->getActions();
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