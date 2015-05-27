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
#include "event.h"
#include "globals.h"

class ObjectWithMenu
{
public:
	ObjectWithMenu() {
		Globals :: getObjectsWithMenu() -> append(this);
	}
	virtual ~ObjectWithMenu() {
		Globals :: getObjectsWithMenu() -> remove(this);
	}

    virtual int fetch_task_items(IndexedList<Action*> &item_list) { return 0; }
};

extern IndexedList<ObjectWithMenu*> main_menu_objects;

// Backward compatibility class
class ObjectMenuItem : public Action
{
public:
	ObjectMenuItem(ObjectWithMenu *obj, const char *text, int code) : Action(text, ObjectMenuItem :: run, obj, (void *)code)
	{
	}

	static void run(void *obj, void *prm) {
		push_event(e_object_private_cmd, obj, (int)prm);
	}
};
#endif /* MENU_H_ */
