/*
 * browsable_root.h
 *
 *  Created on: May 5, 2015
 *      Author: Gideon
 */

#ifndef USERINTERFACE_BROWSABLE_ROOT_H_
#define USERINTERFACE_BROWSABLE_ROOT_H_

#include "browsable.h"
#include "filemanager.h"
#include "filetypes.h"

class BrowsableDirEntry : public Browsable
{
	CachedTreeNode *node;
	FileType *type;
public:
	BrowsableDirEntry(CachedTreeNode *node)
		: Browsable(), node(node) {
		type = NULL;
	}

	virtual ~BrowsableDirEntry() {
		if (type)
			delete type;
	}

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		int retval = 0;
		if (node->children.get_elements() == 0)
			retval = node->fetch_children();
		else
			retval = node->children.get_elements();

		for(int i=0;i<node->children.get_elements();i++) {
			list.append(new BrowsableDirEntry(node->children[i]));
		}
		return retval;
	}

	virtual char *getName() {
		return node->get_name();
	}
	virtual char *getDisplayString() {
		return node->get_display_string();
	}
	virtual void fetch_context_items(IndexedList<Action *>&items) {
		if (!type)
			type = file_type_factory.create(node);
		if (type)
			type->fetch_context_items(items);
	}
};

class BrowsableRoot : public Browsable
{
public:
	BrowsableRoot() : Browsable() { }
	virtual ~BrowsableRoot() { }

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		CachedTreeNode *root = file_manager.get_root();
		for(int i=0;i<root->children.get_elements();i++) {
			list.append(new BrowsableDirEntry(root->children[i]));
		}
		return root->children.get_elements();
	}
	virtual char *getName() { return "Root"; }
};


#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
