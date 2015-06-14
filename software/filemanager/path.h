#ifndef PATH_H
#define PATH_H

#include "cached_tree_node.h"
#include "file_system.h"

class FileManager;

class Path
{
private:
    friend class FileManager;
    mstring full_path;

    int cd_single(char *p);
    void update(char *p);
    Path();
    ~Path();

public:
    const char *owner;
    int cd(char *p);
    char *get_path(void);
    FRESULT get_directory(IndexedList<FileInfo *> &target);

};


#endif
