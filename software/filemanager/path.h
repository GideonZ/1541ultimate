#ifndef PATH_H
#define PATH_H

#include "managed_array.h"
#include "mystring.h"
#include "indexed_list.h"

class Path
{
private:
    friend class PathInfo;
    mstring full_path;
    ManagedArray<mstring *>elements;
    int depth;
    int cd_single(char *p);
    void cleanupElements();
    void update(const char *p);
    void update(int i, const char *p);
    void regenerateFullPath();
public:
    Path();
    Path(Path *); // make a copy
    Path(Path *, int start, int stop);
    ~Path();
    const char *owner;

    int up(mstring *stripped);
    int cd(const char *p);
    const char *get_path(void);

    int getDepth();
    const char *getTail(int index, mstring &work);
    const char *getSub(int start, int stop, mstring &work);
    const char *getHead(mstring &work);
    const char *getElement(int);
    const char *getLastElement();

    bool match(Path *search);
    bool equals(Path *other);
};

class SubPath
{
    Path *reference_path;
    int start;
    int stop;
    mstring *full_path;
public:
    SubPath(Path *p) {
        full_path = NULL;
        start = 0;
        stop = -1;
        reference_path = p;
    }

    ~SubPath() {
        if (full_path) {
            delete full_path;
        }
    }

    const char *get_path() {
        if (!full_path) {
            full_path = new mstring();
        }
        return reference_path->getSub(start, stop, *full_path);
    }

    const char *get_root_path(mstring &work) {
        return reference_path->getSub(0, stop, work);
    }

    Path *get_new_path() {
        return new Path(reference_path, start, stop);
    }

    bool match(Path *search);

    friend class PathInfo;
};

#endif
