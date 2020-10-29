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
};

#endif
