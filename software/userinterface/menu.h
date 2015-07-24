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
#include "globals.h"
#include "path.h"

class ObjectWithMenu
{
public:
	ObjectWithMenu() {
		Globals :: getObjectsWithMenu() -> append(this);
	}
	virtual ~ObjectWithMenu() {
		Globals :: getObjectsWithMenu() -> remove(this);
	}

    virtual int fetch_task_items(Path *p, IndexedList<Action*> &item_list) { return 0; }
};

#endif /* MENU_H_ */
