/*
 * menu.h
 *
 *  Created on: May 12, 2010
 *      Author: Gideon
 */

#ifndef MENU_H_
#define MENU_H_

#include "action.h"
#include "indexed_list.h"
#include "path.h"
#include "tasks_collection.h"

class ObjectWithMenu
{
public:
	static IndexedList<ObjectWithMenu*>* getObjectsWithMenu() {
    	static IndexedList<ObjectWithMenu*> objects_with_menu(8, NULL);
    	return &objects_with_menu;
    }

	ObjectWithMenu() {
		ObjectWithMenu :: getObjectsWithMenu() -> append(this);
	}
	virtual ~ObjectWithMenu() {
		ObjectWithMenu :: getObjectsWithMenu() -> remove(this);
	}

	virtual void create_task_items(void) { }
	virtual void update_task_items(bool writablePath) {  }
};

#endif /* MENU_H_ */
