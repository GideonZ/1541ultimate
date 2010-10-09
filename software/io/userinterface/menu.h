/*
 * menu.h
 *
 *  Created on: May 12, 2010
 *      Author: Gideon
 */

#ifndef MENU_H_
#define MENU_H_

#include "path.h"
#include "event.h"

class MenuItem : public PathObject
{
    PathObject *object;
public:
    int function;

    MenuItem(PathObject *o, char *n, int f) : PathObject(NULL, n),
         function(f), object(o) {  }
    ~MenuItem() { }

    virtual void execute(int dummy) {
        if(object)
            object->execute(function);
    }
};

class MenuItemGlobal : public MenuItem
{
	void *obj;
public:
	MenuItemGlobal(void *o, char *n, int f) : MenuItem(NULL, n, f), obj(o) {
		attach();
	}
	~MenuItemGlobal() { }

	void execute(int dummy) {
		push_event(e_object_private_cmd, obj, function);
	}
};

extern IndexedList<PathObject*> main_menu_static_items;

#endif /* MENU_H_ */
