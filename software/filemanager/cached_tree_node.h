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
    CachedTreeNode(CachedTreeNode *par, const char *name) : parent(par), children(0, NULL), info(name) { }

    bool isRoot() { return parent == NULL; }

	virtual ~CachedTreeNode() {
		// printf("destructor %p %s\n", this, get_name());
		int el = children.get_elements();
		for(int i=0;i<el;i++) {
			delete children[i];
		}
	}

	virtual uint8_t get_attributes() {
		return info.attrib;
	}

	// allow derivates to handle the sub items differently.
	virtual const char *get_name() {
		if(info.lfname)
			return info.lfname;
		return "base - no name";
	}

	virtual void get_display_string(char *buffer, int width) {
		strncpy(buffer, get_name(), width-1);
	}

	virtual int get_header_lines(void) {
		return 0;
	}

	virtual bool is_ready(void) {
		return false; // by default we're not ready
	}

	virtual void cleanup_children(void) {
		int el = children.get_elements();
/*
		if (el > 0)
			printf("Cleaning up children of %s\n", this->get_name());
*/
		for(int i=0;i<el;i++) {
			children[i]->cleanup_children();
			children.mark_for_removal(i);
			delete children[i];
		}
		children.purge_list();
	}

	// this will automatically add children, if any, and store them in the children list
	virtual int  fetch_children()  {
		return children.get_elements(); // default: we just have childen or we don't. We cannot fetch ourselves.
	}

	// the following function will return a new FileInfo, based on the name.
	virtual CachedTreeNode *find_child(const char *find_me) {
		// printf("Trying to find child %s. Name of this: %s\n", find_me, get_name());
		if (children.is_empty())
			fetch_children();
		for(int i=0;i<children.get_elements();i++) {
			if(children[i]->get_file_info()->attrib & AM_VOL)
				continue;
			if(pattern_match(find_me, children[i]->get_name(), false))
				return children[i];
		}
		return NULL;
	}

	// TODO: Sorting is part of the user interface, not of the cache, unless the sorting is used for faster find
	virtual void sort_children(void) {
    	children.sort(path_object_compare);
    }

	// default compare function, just by name!
	virtual int compare(CachedTreeNode *obj) {
		return stricmp(get_name(), obj->get_name());
	}

    const char *get_full_path(mstring& out) {
        CachedTreeNode *po = this;
        mstring sep("/");

        out = "/";

        while(!po->isRoot()) {
            //printf("po->get_name() = %s\n", po->get_name());
            out = sep + po->get_name() + out;
            //printf("out = %s\n", out.c_str());
            po = po->parent;
        }
        //printf("Get full path returns: %s\n", out.c_str());
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
				const char *str = obj->get_name();
				if(!str)
					str = "(null name)";
				printf("%s\n", str);
				obj->dump(level+1);
			}
		}
	    if(level == 0)
			printf("--- End of Tree ---\n");
	}

	FileInfo *get_file_info(void) { return &info; }

};

#endif /* FILEMANAGER_CACHED_TREE_NODE_H_ */
