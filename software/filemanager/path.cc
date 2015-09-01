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

Path :: Path() : full_path("/"), elements(10, NULL)
{
    owner = "Unknown";
    depth = 0;
}

Path :: ~Path()
{
	cleanupElements();
}

void Path :: update(const char *p)
{
	if (full_path == p)
		return;

	full_path = p;
}

void Path :: update(int i, const char *p)
{
	if (!elements[i]) {
		elements.set(i, new mstring(p));
		return;
	}

	if (*(elements[i]) == p)
		return;

	*(elements[i]) = p;
}

void Path :: cleanupElements() {
	for(int i=0;i<depth;i++) {
		if (elements[i]) {
			delete elements[i];
			elements.set(i, 0);
		}
	}
	depth = 0;
}

int Path :: cd_single(char *cd)
{
    int p_len = full_path.length();
    char *p = (char *)full_path.c_str();

	// printf("CD Single: %s\n", cd);
    if(strcmp(cd, "..") == 0) {
		for(int i=p_len-2;i>=0;i--) {
			if((p[i] == '/')||(p[i] == '\\')||(i==0)) {
				p[i+1] = '\0';
				depth--;
				delete elements[depth];
				elements.set(depth, 0);
				break;
			}
		}
        return 1;
    }
    if(strcmp(cd, ".") == 0) {
        return 1;
    }

    update(depth, cd);
    depth++;
    full_path += cd;
	full_path += "/";
	return 1;
}

int Path :: cd(const char *pa_in)
{
	int pa_len = strlen(pa_in);

    //int fp_len = full_path.length();
    //const char *fp = full_path.c_str();
    char *last_part;

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
        full_path = "/";
        cleanupElements();
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

const char *Path :: get_path(void)
{
	return full_path.c_str();
}

int Path :: getDepth()
{
	return depth;
}

const char *Path :: getElement(int a)
{
	if ((a >= depth) || (a < 0)) {
		return "illegal!";
	}
	return (elements[a])->c_str();
}

void Path :: removeFirst()
{
	if(!depth)
		return;

	mstring *old = elements[0];
	delete old;

	for(int i=1;i<depth;i++) {
		elements.set(i-1, elements[i]);
	}
	depth--;

	regenerateFullPath();
}

void Path :: regenerateFullPath()
{
	full_path = "/";
	for(int i=0;i<depth;i++) {
		full_path += (elements[i])->c_str();
		full_path += "/";
	}
}


FRESULT Path :: get_directory(IndexedList<FileInfo *> &target)
{
	return FileManager :: getFileManager() -> get_directory(this, target);
}

bool Path :: isValid()
{
	return FileManager :: getFileManager() -> is_path_valid(this);
}

void Path :: get_display_string(const char *filename, char *buffer, int width)
{
	FileManager :: getFileManager() -> get_display_string(this, filename, buffer, width);
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
