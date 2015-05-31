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
#include "globals.h"
#include "user_file_interaction.h"

class BrowsableFileTree : public Browsable
{
public:
	virtual BrowsableFileTree *getParent() {
		return NULL;
	}
	virtual CachedTreeNode *getNode() {
		return NULL;
	}
};

class BrowsableDirEntry : public BrowsableFileTree
{
	BrowsableFileTree *parent;
	int cacheEntryIndex;
	FileType *type;
public:
	BrowsableDirEntry(BrowsableFileTree *par, int idx, bool sel) {
		this->parent = par;
		this->type = NULL;
		this->selectable = sel;
		this->cacheEntryIndex = idx;
	}

	virtual ~BrowsableDirEntry() {
		if (type)
			delete type;
	}

	BrowsableFileTree *getParent() {
		return parent;
	}

	CachedTreeNode *getNode() {
		CachedTreeNode *parentNode = getParent()->getNode();
		if (parentNode)
			return parentNode->children[cacheEntryIndex];
		return 0;
	}

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		int retval = 0;
		CachedTreeNode *node = getNode();
		if (node->children.get_elements() == 0)
			retval = node->fetch_children();
		else
			retval = node->children.get_elements();

		for(int i=0;i<node->children.get_elements();i++) {
			CachedTreeNode *o = node->children[i];
			if (o) {
				list.append(new BrowsableDirEntry(this, i, !(o->get_file_info()->attrib & AM_VOL)));
			}
		}
		return retval;
	}

	virtual char *getName() {
		CachedTreeNode *node = getNode();
		if (node)
			return node->get_name();
		return "Invalid node";
	}

	virtual char *getDisplayString() {
		CachedTreeNode *node = getNode();
		if (node)
			return node->get_display_string();
		return "Invalid node";
	}
	virtual void fetch_context_items(IndexedList<Action *>&items) {
		CachedTreeNode *node = getNode();
		if (!node)
			return;
		if (!type)
			type = Globals :: getFileTypeFactory()->create(node);
		if (type)
			type->fetch_context_items(items);
		UserFileInteraction :: fetch_context_items(node, items);
	}

	virtual int fetch_task_items(IndexedList<Action *> &list) {
		CachedTreeNode *node = getNode();
		if(!node)
			return 0;
		return UserFileInteraction :: fetch_task_items(node, list);
	}

	virtual bool invalidateMatch(void *obj) {
		CachedTreeNode *n = (CachedTreeNode *)obj;
		CachedTreeNode *node = getNode();
		return (!node) || (n == node) ;
	}
};

class BrowsableRoot : public BrowsableFileTree
{
public:
	BrowsableRoot() { }
	virtual ~BrowsableRoot() { }

	virtual int getSubItems(IndexedList<Browsable *>&list) {
		CachedTreeNode *root = FileManager :: getFileManager() -> get_root();
		for(int i=0;i<root->children.get_elements();i++) {
			CachedTreeNode *o = root->children[i];
			if (o) {
				list.append(new BrowsableDirEntry(this, i, true));
			}
		}
		return root->children.get_elements();
	}

	virtual CachedTreeNode *getNode() {
		return FileManager :: getFileManager() -> get_root();
	}

	virtual char *getName() { return "Root"; }

	virtual bool invalidateMatch(void *obj) {
		CachedTreeNode *n = (CachedTreeNode *)obj;
		CachedTreeNode *root = FileManager :: getFileManager() -> get_root();
		return (n == root);
	}
};

#endif /* USERINTERFACE_BROWSABLE_ROOT_H_ */
