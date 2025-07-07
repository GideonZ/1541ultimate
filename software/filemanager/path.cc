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

Path :: Path(Path *p) : full_path("/"), elements(10, NULL)
{
    owner = "Unknown";
    depth = p->getDepth();

    for(int i=0; i < p->getDepth(); i++) {
        update(i, p->getElement(i));
    }
    regenerateFullPath();
}

Path :: Path(Path *p, int start, int stop) : full_path("/"), elements(stop-start, NULL)
{
    owner = "Unknown";
    if (stop < 0) {
        stop = p->getDepth();
    }
    depth = stop-start;

    for(int i=0; i < depth; i++) {
        update(i, p->getElement(i+start));
    }
    regenerateFullPath();
}

Path :: Path(const char *p) : full_path("/"), elements(8, NULL)
{
    owner = "Unknown";
    depth = 0;
    cd(p);
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
    full_path = "/";
}

int Path :: up(mstring *stripped)
{
    int p_len = full_path.length();
    char *p = (char *)full_path.c_str();
    int ret = 0;
    for(int i=p_len-2;i>=0;i--) {
        if((p[i] == '/')||(p[i] == '\\')||(i==0)) {
            p[i+1] = '\0';
            depth--;
            if (stripped) {
                (*stripped) = *(elements[depth]);
            }
            delete elements[depth];
            elements.set(depth, 0);
            ret = 1;
            break;
        }
    }
    return ret;
}

int Path :: cd_single(char *cd)
{

	// printf("CD Single: %s\n", cd);
    if(strcmp(cd, "..") == 0) {
        return up(NULL);
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
    
    // printf("CD '%s' (starting from %s)\n", pa, full_path.c_str());
	
    // check for start from root
    if( (pa_len) &&
	    ((*pa == '/')||(*pa == '\\')) ) {
        --pa_len;
        pa++;
        full_path = "/";
        cleanupElements();
    }

    // nothing to be done anymore
    if(!pa_len) {
		delete[] pa_alloc;
        return 1;
	}

    // snoop last '/' (or '\')
    if ((pa[pa_len-1] == '/')||(pa[pa_len-1] == '\\')) {
        pa_len--;
        pa[pa_len] = 0;
    }

    // nothing to be done anymore
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

const char *Path :: getLastElement()
{
    return (elements[depth-1])->c_str();
}

/*
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
*/
const char * Path :: getTail(int index, mstring &work)
{
	return getSub(index, depth, work);
}

const char * Path :: getSub(int start, int stop, mstring &work)
{
	work = "";
	for(int i = start; i < stop; i++) {
		work += "/";
		work += (elements[i])->c_str();
	}
	if (work.length() == 0)
		work = "/";
	return work.c_str();
}

const char * Path :: getHead(mstring &work)
{
	work = "";
	for(int i = 0; i < depth - 1; i++) {
		work += "/";
		work += (elements[i])->c_str();
	}
	if (work.length() == 0)
		work = "/";
	return work.c_str();
}

void Path :: regenerateFullPath()
{
	full_path = "/";
	for(int i=0;i<depth;i++) {
		full_path += (elements[i])->c_str();
		full_path += "/";
	}
}

// Return true when the search path is at least as long as
// the 'this' path.
bool Path :: match(Path *search)
{
    if (search->getDepth() < depth) {
        return false;
    }
    for(int i=0; i<depth; i++) {
        if (!pattern_match(search->getElement(i), getElement(i), false)) {
            return false;
        }
    }
    return true;
}

// Return true when the paths are exactly equal, case insensitive though.
bool Path :: equals(Path *search)
{
    if (search->getDepth() != depth) {
        return false;
    }
    for(int i=0; i<depth; i++) {
        if (strcasecmp(search->getElement(i), getElement(i)) != 0) {
            return false;
        }
    }
    return true;
}

// Return true when the search path is at least as long as
// the 'this' path.
bool SubPath :: match(Path *search)
{
    int realStop = (stop < 0) ? reference_path->getDepth() : stop;
    int depth = realStop - start;

    if (search->getDepth() < depth) {
        return false;
    }
    for(int i=0; i<depth; i++) {
        if (!pattern_match(search->getElement(i), reference_path->getElement(start+i), false)) {
            return false;
        }
    }
    return true;
}
