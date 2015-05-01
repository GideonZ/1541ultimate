#ifndef FILETYPE_U2U_H
#define FILETYPE_U2U_H

#include "file_direntry.h"
#include "file_system.h"


class FileTypeUpdate : public FileDirEntry
{
public:
	FileTypeUpdate(FileTypeFactory &fac);
	FileTypeUpdate(CachedTreeNode *par, FileInfo *fi);
    ~FileTypeUpdate();

    int   fetch_children(void) { return -1; }
    int   fetch_context_items(IndexedList<CachedTreeNode *> &list);
    FileDirEntry *test_type(CachedTreeNode *obj);
    void execute(int);
};


#endif
