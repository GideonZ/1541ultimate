/*
 * path.cc
 *
 *  Created on: Apr 11, 2010
 *  Revised on: Apr 27, 2015
 *      Author: gideon
 */
#include "path.h"
#include "filemanager.h"
#include "file_info.h"
#include <string.h>

Path :: Path() : full_path("/")
{
    current_dir_node = file_manager.get_root();
//	cd("SD");
}

Path :: ~Path()
{
}

int Path :: cd_single(char *cd)
{
    int p_len = full_path.length();
    char *p = full_path.c_str();
    CachedTreeNode *t;

	printf("CD Single: %s\n", cd);
    if(strcmp(cd, "..") == 0) {
    	if ((current_dir_node->parent == NULL) ||
    		(current_dir_node->parent == file_manager.get_root())) { // we'll end up in root.
    		full_path = "/";
    		current_dir_node = file_manager.get_root();
    	} else {
            for(int i=p_len-2;i>=0;i--) {
                if((p[i] == '/')||(p[i] == '\\')||(i==0)) {
                    p[i] = '\0';
                    break;
                }
            }
            current_dir_node = current_dir_node->parent;
    	}
        return 1;
    }
    if(strcmp(cd, ".") == 0) {
        return 1;
    }

    t = current_dir_node->find_child(cd);
    printf("Find child returned: %p\n", t);
    if(!t)
        return 0;

    if (t->get_attributes() & (AM_DIR | AM_HASCHILDREN)) { // success!
    	full_path += t->get_name();
		full_path += "/";
		current_dir_node = t;
		return 1;
    }
    return 0;
}

int Path :: cd(char *pa_in)
{
    int pa_len = strlen(pa_in);

    int fp_len = full_path.length();
    char *fp = full_path.c_str();
    char *last_part;
    CachedTreeNode *t;

	char *pa_alloc = new char[pa_len+1];
	char *pa = pa_alloc;
	
	strcpy(pa, pa_in);
    
    // snoop last '/' (or '\')
    if ((pa[pa_len-1] == '/')||(pa[pa_len-1] == '\\')) {
        pa_len--;
        pa[pa_len] = 0;
    }

    // printf("CD '%s' (starting from %s)\n", pa, full_path.c_str());
	
    // check for start from root
    if( (pa_len) &&
	    ((*pa == '/')||(*pa == '\\')) ) {
        --pa_len;
        pa++;
        current_dir_node = file_manager.get_root();
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

char *Path :: get_path(void)
{
	return full_path.c_str();
}

int Path :: get_directory(IndexedList<FileInfo *> &target)
{
	int result = current_dir_node->fetch_children();
	printf("Path :: get_directory Result = %d\n", result);
	for (int i=0;i<current_dir_node->children.get_elements();i++) {
		target.append(new FileInfo(*(current_dir_node->children[i]->get_file_info())));
	}
	return result;
}

// =======================
//   COMPARE PATH OBJECTS 
// =======================

int path_object_compare(IndexedList<CachedTreeNode *> *list, int a, int b)
{
//	printf("Compare %d and %d: ", a, b);
	CachedTreeNode *obj_a = (*list)[a];
	CachedTreeNode *obj_b = (*list)[b];
	
	if(!obj_b)
		return 1;
	if(!obj_a)
		return -1;

//	printf("%p %p ", obj_a, obj_b);
//	printf("%s %s\n", obj_a->get_name(), obj_b->get_name());
	return obj_a->compare(obj_b);
//	return stricmp(obj_a->get_name(), obj_b->get_name());
}
