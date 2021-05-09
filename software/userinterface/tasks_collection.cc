/*
 * tasks_collection.cc
 *
 *  Created on: Nov 28, 2020
 *      Author: gideon
 */


#include "tasks_collection.h"
#include <stdio.h>

int main()
{
    TasksCollection :: getCategory("Aap", 10);
    TasksCollection :: getCategory("Mies", 30);
    TaskCategory *c = TasksCollection :: getCategory("Noot", 20);
    TasksCollection :: sort();

    IndexedList<TaskCategory *> *cats = TasksCollection :: getCategories();
    for (int i=0; i < cats->get_elements(); i++) {
        printf("%s\n", (*cats)[i]->getName());
    }
    return 0;
}
