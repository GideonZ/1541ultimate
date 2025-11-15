/*
 * tasks_collection.h
 *
 *  Created on: Nov 28, 2020
 *      Author: gideon
 */

#ifndef TASKS_COLLECTION_H_
#define TASKS_COLLECTION_H_

#include <stdint.h>
#include "mystring.h"
#include "indexed_list.h"
#include "action.h"

class TaskCategory
{
    mstring name;
    const int sortOrder;
    IndexedList<Action *> actions;
public:
    TaskCategory(const char *catname, int so) : actions(8, NULL), name(catname), sortOrder(so) {

    }
    ~TaskCategory();

    const char *getName(void) { return name.c_str(); }
    const int getOrder(void) { return sortOrder; }
    void append(Action *a) { actions.append(a); }
    IndexedList<Action *> *getActions(void) { return &actions; }
    int countActive(void)
    {
        int c = 0;
        for(int i=0;i<actions.get_elements();i++) {
            if (actions[i]->isEnabled() && actions[i]->isShown()) c++;
        }
        return c;
    }
};

class TasksCollection
{
    IndexedList<TaskCategory *> categories;
    TasksCollection() : categories(8, NULL) { };
public:
    static TasksCollection *getTasksCollection(void) {
        static TasksCollection *col = NULL;
        if (!col) {
            col = new TasksCollection();
        }
        return col;
    }

    ~TasksCollection() {
        for (int i=0; i < categories.get_elements(); i++) {
            TaskCategory *c = categories[i];
            if (c) {
                delete c;
            }
        }
    }

    static TaskCategory *getCategory(const char *name, int sortOrder) {
        TasksCollection *col = getTasksCollection();
        for (int i=0; i < col->categories.get_elements(); i++) {
            TaskCategory *c = col->categories[i];
            if (c) {
                if (strcmp(c->getName(), name) == 0) {
                    return c;
                }
            }
        }
        TaskCategory *c = new TaskCategory(name, sortOrder);
        col->categories.append(c);
        return c;
    }

    static IndexedList<TaskCategory *> *getCategories() {
        TasksCollection *col = getTasksCollection();
        return &(col->categories);
    }

    static int compare(IndexedList<TaskCategory *> *list, int a, int b) {
        TaskCategory *ca = (*list)[a];
        TaskCategory *cb = (*list)[b];
        if (!ca || !cb) {
            return 0;
        }
        return ca->getOrder() - cb->getOrder();
    }

    static void sort(void) {
        TasksCollection *col = getTasksCollection();
        col->categories.sort(TasksCollection :: compare);
    }
};

#endif /* TASKS_COLLECTION_H_ */
