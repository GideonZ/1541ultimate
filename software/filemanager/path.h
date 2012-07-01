#ifndef PATH_H
#define PATH_H

#include <string.h>
extern "C" {
    #include "small_printf.h"
}
#include "indexed_list.h"
#include "mystring.h"
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

    int get_ref_count() { return ref_count; }

	virtual ~PathObject() {
		//printf("destructor %p %s (%d)\n", this, get_name(), ref_count);
		int el = children.get_elements();
		for(int i=0;i<el;i++) {
			if(children[i]->ref_count == 0)
				delete children[i];
		}
		if(ref_count) {
			printf("Internal error! Deleting a path object %s that has %d references!\n", ref_count, get_name());
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
			//printf("attach single: %s\n", get_name());
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

	virtual void detach(bool single=false) {
		PathObject *obj = this;
		//printf("Detach recursive: ");
		if(single) {
			if(ref_count > 0)
			    ref_count--;
		} else {
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
	}

	virtual void cleanup_children(void) {
		int el = children.get_elements();
//		if(el)
//			printf("Cleaning up %d children of %s.\n", el, get_name());
		for(int i=0;i<el;i++) {
			children[i]->cleanup_children();
			if(children[i]->ref_count == 0) {
				children.mark_for_removal(i);
				//printf(" deleting %s\n", children[i]->get_name());
				delete children[i];
			}
		}
		children.purge_list();
	}

	// this will automatically add children, if any, and store them in the children list
	virtual int  fetch_children()  {
		return -1; // default: we don't know how to fetch children...
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

    virtual void execute(int select) { }

    virtual void sort_children(void) {
    	children.sort(path_object_compare);
    }

	// default compare function, just by name!
	virtual int compare(PathObject *obj) {
		return stricmp(get_name(), obj->get_name());
	}
	
	void dump(int level=0) {
		PathObject *obj;
        if(level == 0)
            printf("--- Dumping Path from %s ---\n", get_name());
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
				printf("%2d %s\n", obj->ref_count, str);
				obj->dump(level+1);
			}
		}
	    if(level == 0)
			printf("--- End of Tree ---\n");
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
