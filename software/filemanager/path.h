#ifndef PATH_H
#define PATH_H

#include "cached_tree_node.h"

class FileManager;

class Path
{
private:
    friend class FileManager;
	string full_path;
    CachedTreeNode *current_dir_node;

    int cd_single(char *p);

    Path();
    ~Path();

public:
    int cd(char *p);
    char *get_path(void);
    int get_directory(IndexedList<FileInfo *> &target);

};


#endif
