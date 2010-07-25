#ifndef PATH_H
#define PATH_H

#include "indexed_list.h"
#include "mystring.h"
#include "small_printf.h"
#include "pattern.h"

class MenuItem;
class FileInfo;
class PathObject;

int path_object_compare(IndexedList<PathObject *> *, int, int);

class PathObject
{
	int ref_count;
	string name;
public:
	IndexedList<PathObject*> children;
	PathObject *parent;

    PathObject(PathObject *par) : parent(par), children(0, NULL) {
    	ref_count = 0;
    }
	PathObject(PathObject *par, char *n) : parent(par), name(n), children(0, NULL) {
    	ref_count = 0;
    }

	virtual ~PathObject() {
		//printf("destructor %p %s ", this, get_name());
		cleanup_children();
		//printf("Ref=%d\n", ref_count);
		if(ref_count) {
			printf("Internal error! Deleting a path object that has %d references!\n", ref_count);
		}
	}

    // allow derivates to handle the sub items differently.
	virtual char *get_name() {
		if(name.c_str())
			return name.c_str();
		return "base - no name";
	}

	virtual char *get_display_string() {
		return get_name();
	}

	virtual int get_header_lines(void) {
		return 0;
	}
	
	virtual void attach(bool single=false) {
		if(single) {
			printf("attach single: %s\n", get_name());
			ref_count++;
		} else {
//			printf("Attach recursive: ");
			PathObject *obj = this;
			while(obj) {
//				printf("%s, ", obj->get_name());
				obj->ref_count++;
				obj = obj->parent;
			}
//			printf("\n");
		}
	}

	virtual void detach(void) {
		PathObject *obj = this;
		//printf("Detach recursive: ");
		while(obj) {
			//printf("%s (%d), ", obj->get_name(), obj->ref_count);
			if(obj->ref_count > 0)
				obj->ref_count--;
			else
				printf("Internal error. Detach of object that is not attached. (%s)\n", obj->get_name());
			obj = obj->parent;
		}
		//printf("\n");
	}

	virtual void cleanup_children(void) {
		//printf("Cleaning up %d children of %s.\n", children.get_elements(), get_name());
		for(int i=0;i<children.get_elements();i++) {
			children[i]->cleanup();
    		if(children[i]->ref_count == 0)
    			delete children[i];
		}
		children.clear_list();
	}

	virtual void cleanup(void) {
		cleanup_children();
	}

	// this will automatically add children, if any, and store them in the children list
	virtual int  fetch_children()  {
		return -1; // default: we don't know how to fetch children...
//		return children.get_elements(); // by default, we just return the number of children that were
		// statically added.
	}

	// the following function will return a new PathObject, based on the name.
	virtual PathObject *find_child(char *find_me) {
		// the quick and memory hungry way:
		printf("Trying to find child %s. Name of this: %s\n", find_me, get_name());
		if(fetch_children() < 1)
			return NULL;
		for(int i=0;i<children.get_elements();i++) {
			if(pattern_match(find_me, children[i]->get_name(), false))
				return children[i];
		}
		return NULL;
	}

	// this will get the applicable context items, storing them into a list
    virtual int  fetch_context_items(IndexedList<PathObject*> &item_list)  {
        return 0;
    }

    virtual int  fetch_task_items(IndexedList<PathObject*> &item_list)  {
        return 0;
    }

    virtual FileInfo *get_file_info(void) { return NULL; }

    virtual void execute(int select) {
        printf("Selected option #%d for base object.\n", select);
    }

    virtual void sort_children(void) {
    	children.sort(path_object_compare);
    }

	void dump(int level=0) {
		PathObject *obj;
		for(int i=0;i<children.get_elements();i++) {
			obj = children[i];
			for(int j=0;j<level;j++) {
				printf(" ");
			}
			printf("%2d %s\n", obj->ref_count, obj->get_name());
			obj->dump(level+1);
		}
	}
};

class Path
{
    string full_path;
    PathObject *root_obj;
    PathObject *current;

    int cd_single(char *p);
    void path_from_object(PathObject *obj);
public:
    Path();
    Path(PathObject *obj);
    ~Path();

    int cd(char *p);
    int cd(PathObject *obj);
    PathObject *get_path_object(void);
    char *get_path(void);
};


#endif
