#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "file_direntry.h"
#include "file_system.h"


class FileTypeTap : public FileDirEntry
{
public:
    FileTypeTap(CachedTreeNode *par, FileInfo *fi);
    ~FileTypeTap();

    int   fetch_children(void) { return -1; }
    int   fetch_context_items(IndexedList<CachedTreeNode *> &list);
    FileDirEntry *test_type(CachedTreeNode *obj);
    void execute(int);
};


#endif
