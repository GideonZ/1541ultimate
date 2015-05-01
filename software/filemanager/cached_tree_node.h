/*
 * cached_tree_node.h
 *
 *  Created on: Apr 27, 2015
 *      Author: Gideon
 */

#ifndef FILEMANAGER_CACHED_TREE_NODE_H_
#define FILEMANAGER_CACHED_TREE_NODE_H_

#include <string.h>
#include <stdio.h>
#include "indexed_list.h"
#include "mystring.h"
#include "pattern.h"
#include "file_info.h"

class CachedTreeNode;

int path_object_compare(IndexedList<CachedTreeNode *> *, int, int);

class CachedTreeNode
{
protected:
	FileInfo info;
public:
	IndexedList<CachedTreeNode*> children;
	CachedTreeNode *parent;

    CachedTreeNode(CachedTreeNode *par) : parent(par), children(0, NULL) { }
    CachedTreeNode(CachedTreeNode *par, FileInfo &inf) : parent(par), children(0, NULL), info(inf) { }
    CachedTreeNode(CachedTreeNode *par, char *name) : parent(par), children(0, NULL), info(name) { }

    bool isRoot() { return parent == NULL; }

	virtual ~CachedTreeNode() {
		//printf("destructor %p %s (%d)\n", this, get_name(), ref_count);
		int el = children.get_elements();
		for(int i=0;i<el;i++) {
			delete children[i];
		}
	}

	virtual BYTE get_attributes() {
		return info.attrib;
	}

	// allow derivates to handle the sub items differently.
	virtual char *get_name() {
		if(info.lfname)
			return info.lfname;
		return "base - no name";
	}

	virtual char *get_display_string() {
		return get_name();
	}

	virtual int get_header_lines(void) {
		return 0;
	}

	virtual void cleanup_children(void) {
		int el = children.get_elements();
		for(int i=0;i<el;i++) {
			children[i]->cleanup_children();
			children.mark_for_removal(i);
			delete children[i];
		}
		children.purge_list();
	}

	// this will automatically add children, if any, and store them in the children list
	virtual int  fetch_children()  {
		return -1; // default: we don't know how to fetch children...
	}

	// the following function will return a new FileInfo, based on the name.
	virtual CachedTreeNode *find_child(char *find_me) {
		// printf("Trying to find child %s. Name of this: %s\n", find_me, get_name());
		fetch_children();
		for(int i=0;i<children.get_elements();i++) {
			if(pattern_match(find_me, children[i]->get_name(), false))
				return children[i];
		}
		return NULL;
	}

	// this will get the applicable context items, storing them into a list
	// TODO: This shall change to a list of "Actions", rather than cached tree nodes.
	virtual int  fetch_context_items(IndexedList<CachedTreeNode*> &item_list)  {
        return 0;
    }

    virtual void sort_children(void) {
    	children.sort(path_object_compare);
    }

	// default compare function, just by name!
	virtual int compare(CachedTreeNode *obj) {
		return stricmp(get_name(), obj->get_name());
	}

    char *get_full_path(string& out) {
        CachedTreeNode *po = this;
        string sep("/");
        out = sep + po->get_name();
        while(!po->parent->isRoot()) {
            po = po->parent;
            out = sep + po->get_name() + out;
        }
        printf("Get full path returns: %s\n", out.c_str());
        return out.c_str();
    }

	void dump(int level=0) {
		CachedTreeNode *obj;
        if(level == 0)
            printf("--- Dumping Tree from %s ---\n", get_name());
		for(int i=0;i<children.get_elements();i++) {
			obj = children[i];
			for(int j=0;j<level;j++) {
				printf(" ");
			}
			printf("[%p]", obj);
			if(!obj) {
				printf("(null)\n");
			} else {
				char *str = obj->get_name();
				if(!str)
					str = "(null name)";
				printf("%s\n", str);
				obj->dump(level+1);
			}
		}
	    if(level == 0)
			printf("--- End of Tree ---\n");
	}

	/* Not too sure if the following functions shall continue to exist */
	virtual FileInfo *get_file_info(void) { return &info; }
    virtual void execute(int select) { }
};




#endif /* FILEMANAGER_CACHED_TREE_NODE_H_ */
