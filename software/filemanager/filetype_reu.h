#ifndef FILETYPE_REU_H
#define FILETYPE_REU_H

#include "file_direntry.h"

class FileTypeREU : public FileDirEntry
{
    int type;
public:
    FileTypeREU(FileTypeFactory &fac);
    FileTypeREU(CachedTreeNode *par, FileInfo *fi, int);
    ~FileTypeREU();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<CachedTreeNode *> &list);
    FileDirEntry *test_type(CachedTreeNode *obj);

    void  execute(int selection);
};


#endif
