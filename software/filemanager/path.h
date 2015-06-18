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
    void update(const char *p);
    Path();
    ~Path();

public:
    const char *owner;
    int cd(const char *p);
    const char *get_path(void);
    FRESULT get_directory(IndexedList<FileInfo *> &target);
    void get_display_string(const char *filename, char *buffer, int width);
    bool isValid();
};


#endif
