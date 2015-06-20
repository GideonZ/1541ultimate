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
#include <stdio.h>

#include "indexed_list.h"
#include "action.h"
//#include "path.h"

class Browsable
{
	bool selected;
protected:
	bool selectable;
public:
	Browsable() {
		selected = false;
		selectable = true;
		(*getCount())++;
	}

	Browsable(bool s) {
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
	}

	void setSelection(bool s) { selected = s; }
	bool getSelection() { return selected; }
	bool isSelectable() { return selectable; }

	virtual void fetch_context_items(IndexedList<Action *>&items) { }
	virtual int fetch_task_items(IndexedList<Action *> &list) { return 0; }
	virtual bool isValid() { return false; }
	virtual int getSubItems(IndexedList<Browsable *>&list) { return 0; }
	virtual const char *getName() { return "Browsable"; }
	virtual void getDisplayString(char *buffer, int width) { strncpy(buffer, getName(), width-1); }
};



#endif /* USERINTERFACE_BROWSABLE_H_ */
