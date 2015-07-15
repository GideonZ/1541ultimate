#ifndef PATH_H
#define PATH_H

#include "cached_tree_node.h"
#include "file_system.h"
#include "managed_array.h"

class FileManager;

class Path
{
private:
    friend class FileManager;
    mstring full_path;
    ManagedArray<mstring *>elements;
    int depth;
    int cd_single(char *p);
    void cleanupElements();
    void update(const char *p);
    void update(int i, const char *p);
    Path();
    ~Path();
public:
    const char *owner;
    int cd(const char *p);
    const char *get_path(void);
    int getDepth();
    const char *getElement(int);

    void get_display_string(const char *filename, char *buffer, int width);
    FRESULT get_directory(IndexedList<FileInfo *> &target);
    bool isValid();
};


#endif
