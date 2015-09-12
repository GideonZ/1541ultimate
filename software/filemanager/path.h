#ifndef PATH_H
#define PATH_H

//#include "cached_tree_node.h"
#include "file_info.h"
#include "managed_array.h"
#include "mystring.h"
#include "fs_errors_flags.h"
#include "indexed_list.h"

class FileManager;

class Path
{
private:
    friend class FileManager;
    friend class PathInfo;
    mstring full_path;
    ManagedArray<mstring *>elements;
    int depth;
    int cd_single(char *p);
    void cleanupElements();
    void update(const char *p);
    void update(int i, const char *p);
    void regenerateFullPath();
    Path();
    ~Path();
public:
    const char *owner;

    int cd(const char *p);
    const char *get_path(void);

    int getDepth();
    const char *getTail(int index, mstring &work);
    const char *getSub(int start, int stop, mstring &work);
    const char *getHead(mstring &work);

    const char *getElement(int);

    void get_display_string(const char *filename, char *buffer, int width);
    FRESULT get_directory(IndexedList<FileInfo *> &target);
    bool isValid();
    void dump() {
    	printf("** PATH OBJECT ** Owner = %s ** FullPathString = %s\n", owner, full_path.c_str());
    	for(int i=0;i<depth;i++) {
    		printf(" %2d: %s\n", i, getElement(i));
    	}
    }
};

#endif
