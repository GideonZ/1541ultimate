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

class FileInfo;

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

    virtual void setSelection(bool s) { selected = s; }
    virtual bool getSelection() { return selected; }
    virtual bool isSelectable() { return selectable; }
    virtual void allowSelectable(bool b) { selectable = b; }
    virtual FileInfo *getFileInfo() { return NULL; }
    virtual bool pickAsCurrentPath() { return false; }
    virtual bool isPathPickerWrapper() { return false; }
    virtual int  getSortOrder(void) { return 0; }

    static int compare_alphabetically(IndexedList<Browsable *>*list, int a, int b)
    {
        return strcmp((*list)[a]->getName(), (*list)[b]->getName());
    }

    static int compare_sortorder(IndexedList<Browsable *>*list, int a, int b)
    {
        return (*list)[a]->getSortOrder() - (*list)[b]->getSortOrder();
    }

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

class BrowsableHeader : public Browsable
{
    const char *message;
    int sort_order;
public:
    BrowsableHeader(const char *msg, int sortorder) : message(msg)
    {
        selectable = false;
        sort_order = sortorder;
    }

    ~BrowsableHeader() { }
    const char *getName() { return message; }
    int getSortOrder() { return sort_order; }
    void getDisplayString(char *buffer, int width) {
        int len = strlen(message);
        if (len > width) {
            len = width;
        }
        int offs = 0;
        sprintf(buffer, "%#s%#s", offs, "", len, message);
    }
};
#endif /* USERINTERFACE_BROWSABLE_H_ */
