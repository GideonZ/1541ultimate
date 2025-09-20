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
#include "small_printf.h"
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
    void allowSelectable(bool b) { selectable = b; }

    virtual void event(int) {}
	virtual void fetch_context_items(IndexedList<Action *>&items) { }
	virtual IndexedList<Browsable *> *getSubItems(int &error) { error = 0; return &children; }
	virtual Browsable *getParent() { return 0; }
	virtual const char *getName() { return "Browsable"; }
	virtual void getDisplayString(char *buffer, int width) { strncpy(buffer, getName(), width-1); }
	virtual void getDisplayString(char *buffer, int width, int squeeze_option) { getDisplayString(buffer, width); }
};

class BrowsableStatic : public Browsable
{
    const char *message;
public:
    BrowsableStatic(const char *msg) : message(msg) { selectable = false; }
    ~BrowsableStatic() { }
    const char *getName() { return message; }
    void getDisplayString(char *buffer, int width) {
        int len = strlen(message);
        if (len > width) {
            len = width;
        }
        int offs = (width - len) / 2;
        sprintf(buffer, "%#s%#s", offs, "", len, message);
    }
};

#endif /* USERINTERFACE_BROWSABLE_H_ */
