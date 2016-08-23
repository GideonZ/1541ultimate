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

    virtual int fetch_task_items(Path *p, IndexedList<Action*> &item_list) { return 0; }
};

#endif /* MENU_H_ */
