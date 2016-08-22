/*
 * browsable.h
 *
 *  Created on: May 3, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_BROWSABLE_H_
#define USERINTERFACE_BROWSABLE_H_

#include "mystring.h"
#include "action.h"

#include "indexed_list.h"
#include "action.h"

class Browsable
{
	bool selected;
protected:
	bool selectable;
public:
	IndexedList <Browsable *>children;

	Browsable() : children(0, NULL) {
		selected = false;
		selectable = true;
		(*getCount())++;
	}

	Browsable(bool s) : children(0, NULL) {
		selected = false;
		selectable = s;
		(*getCount())++;
	}

	static int *getCount() {
		static int count = 0;
		return &count;
	}

	virtual ~Browsable() {
		(*getCount())--;
		killChildren();
	}

	void killChildren(void) {
		for (int i=0;i<children.get_elements();i++) {
			delete children[i];
		}
		children.clear_list();
	}

	Browsable *findChild(const char *n) {
		for (int i=0;i<children.get_elements();i++) {
			if (strcmp(children[i]->getName(), n) == 0)
				return children[i];
		}
		return 0;
	}

	void setSelection(bool s) { selected = s; }
	bool getSelection() { return selected; }
	bool isSelectable() { return selectable; }

	virtual void fetch_context_items(IndexedList<Action *>&items) { }
	virtual int fetch_task_items(IndexedList<Action *> &list) { return 0; }
	virtual IndexedList<Browsable *> *getSubItems(int &error) { error = 0; return &children; }
	virtual Browsable *getParent() { return 0; }
	virtual const char *getName() { return "Browsable"; }
	virtual void getDisplayString(char *buffer, int width) { strncpy(buffer, getName(), width-1); }
};


#endif /* USERINTERFACE_BROWSABLE_H_ */
