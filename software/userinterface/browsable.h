/*
 * browsable.h
 *
 *  Created on: May 3, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_BROWSABLE_H_
#define USERINTERFACE_BROWSABLE_H_

#include "contextable.h"
#include "mystring.h"
#include "action.h"
#include <stdio.h>

class Browsable : public Contextable
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

	virtual int fetch_task_items(IndexedList<Action *> &list) { return 0; }
	virtual bool invalidateMatch(void *a) { return false; }
	virtual int getSubItems(IndexedList<Browsable *>&list) { return 0; }
	virtual char *getName() { return "Browsable"; }
	virtual char *getDisplayString() { return getName(); }
};

class BrowsableNamed : public Browsable
{
	mstring name;
public:
	BrowsableNamed(bool s, char *n) : Browsable(s), name(n) { }
	virtual ~BrowsableNamed() {}

	virtual char *getName() { return name.c_str(); }
};

class BrowsableTest : public BrowsableNamed
{
	int numberOfSubItems;
public:
	BrowsableTest(int subs, bool s, char *n) : BrowsableNamed(s, n), numberOfSubItems(subs) { }
	virtual ~BrowsableTest() {}

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		char temp[32];
		list.append(new BrowsableTest(1, true, "Top Row"));
		for(int i=0;i<numberOfSubItems/2;i++) {
			sprintf(temp, "First SubItem %d", i+1);
			list.append(new BrowsableTest(5, (((i*5)%7)!=2), temp));
			//list.append(new BrowsableNamed(true, temp));
		}
		list.append(new BrowsableTest(3, true, "Somewhere in the middle"));
		for(int i=0;i<numberOfSubItems/2;i++) {
			sprintf(temp, "Second SubItem %d", i+1);
			list.append(new BrowsableTest(5, ((i%17)!=2), temp));
			//list.append(new BrowsableNamed(true, temp));
		}
		list.append(new BrowsableTest(6, true, "Oh lala! Last!"));
		return numberOfSubItems+1;
	}

	virtual void fetch_context_items(IndexedList<Action *>&items) {
		char temp[16];
		for(int i=0;i<25;i++) {
			sprintf(temp, "A%d", i+1);
			items.append(new Action(temp, BrowsableTest :: action, this, (void *)i));
		}
	}

	static void action(void *obj, void *param) {
		printf("You selected action number %d from %s\n", (int)param, ((BrowsableTest *)obj)->getName());
	}
};


#endif /* USERINTERFACE_BROWSABLE_H_ */
