/*
 * path.cc
 *
 *  Created on: Apr 11, 2010
 *      Author: gideon
 */
#include "path.h"
#include "filemanager.h"
#include <string.h>

Path :: Path() : full_path("/")
{
	root_obj = &root;
    current  = &root;
    root.attach();
	cd("SdCard");
}

Path :: Path(PathObject *obj) : full_path("/")
{
	root_obj = &root;
    current  = &root;
    path_from_object(obj);
}

Path :: ~Path()
{
//	printf("Destroying Path...\n");
//	root_obj->dump();
	current->detach();
//	root_obj->dump();
	root_obj->cleanup_children();
//	root_obj->dump();
//	printf("Exit path destructor..\n");
/*
	PathObject *p;
	do {
		p = current->parent;
		current->detach();
		current = p;
	} while(p);
*/

/*
	while(current != root_obj) {
    	printf("Cleaning up children of: %p ", current);
    	printf("%s\n", current->get_name());
    	current->cleanup_children();
    	current = current->parent;
	}
*/
}

int Path :: cd_single(char *cd)
{
    int p_len = full_path.length();
    char *p = full_path.c_str();
    PathObject *t;

	printf("CD Single: %s\n", cd);
    if(strcmp(cd, "..") == 0) {
    	if(p_len > 1) {
            for(int i=p_len-2;i>=0;i--) {
                if((p[i] == '/')||(p[i] == '\\')) {
                    p[i] = '\0';
                    t = current->parent;
                    t->cleanup_children();
                    current->detach(true);
                    current = t;
                    break;
                }
            }
        }
        return 1;
    }
    if(strcmp(cd, ".") == 0) {
        return 1;
    }
    t = current->find_child(cd);
    if(!t)
        return 0;

    if(current != root_obj)
		full_path += "/";
	full_path += t->get_name();
	current->detach();
	current = t;
	current->attach();
	return 1;
}

int Path :: cd(char *pa_in)
{
    int pa_len = strlen(pa_in);
    int fp_len = full_path.length();
    char *fp = full_path.c_str();
    char *last_part;
    PathObject *t;

	char *pa_alloc = new char[pa_len+1];
	char *pa = pa_alloc;
	
	strcpy(pa, pa_in);
//    printf("CD '%s' (starting from %s)\n", pa, full_path.c_str());
	
    // check for start from root
    if( (pa_len) &&
	    ((*pa == '/')||(*pa == '\\')) ) {
        --pa_len;
        pa++;
        while(current != root_obj) {
        	current->cleanup_children();
		    current->detach(true);
        	current = current->parent;
		}
        full_path = "/";
    }
    if(!pa_len) {
		delete[] pa_alloc;
        return 1;
	}
    // split path string into separate parts
    last_part = pa;
    for(int i=0;i<pa_len;i++) {
        if((pa[i] == '/')||(pa[i] == '\\')) {
            pa[i] = 0;
            if(!cd_single(last_part)) {
                printf("A) Can't CD to %s\n", pa);
                delete[] pa_alloc;
				return 0;
            }
            last_part = &pa[i+1];
        }
    }

    if(!cd_single(last_part)) {
        printf("B) Can't CD to %s\n", last_part);
        delete[] pa_alloc;
		return 0;
    }
	delete[] pa_alloc;
    return 1;
}

int Path :: cd(PathObject *obj)
{
	// forward
	if(current == obj->parent) {
		current->detach();
		current = obj;
		current->attach();
	    if(current != root_obj)
			full_path += "/";
		full_path += obj->get_name();
		return 1;
	}

	// backward case
	if(obj == current->parent) {
		return cd_single("..");
	}
	printf("CD to object %s cannot be done on current path '%s'\n", obj->get_name(), get_path());
	return 0; // error
}

PathObject *Path :: get_path_object(void)
{
    return current;
}

char *Path :: get_path(void)
{
	return full_path.c_str();
}

void Path :: path_from_object(PathObject *obj)
{
	// recursive.. first: find root object
	if(obj->parent) {
		path_from_object(obj->parent);
	}
//	else {
//		root_obj = obj;
//	}

	// now, we are at the root, or processing the path in the correct order
	obj->attach(true);
	if(obj->parent)
		full_path += "/";

	full_path += obj->get_name();
	current = obj;
}

// =======================
//   COMPARE PATH OBJECTS 
// =======================

int path_object_compare(IndexedList<PathObject *> *list, int a, int b)
{
//	printf("Compare %d and %d: ", a, b);
	PathObject *obj_a = (*list)[a];
	PathObject *obj_b = (*list)[b];
	
	if(!obj_b)
		return 1;
	if(!obj_a)
		return -1;

//	printf("%p %p ", obj_a, obj_b);
//	printf("%s %s\n", obj_a->get_name(), obj_b->get_name());
	return obj_a->compare(obj_b);
//	return stricmp(obj_a->get_name(), obj_b->get_name());
}
