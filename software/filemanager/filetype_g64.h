#ifndef FILETYPE_G64_H
#define FILETYPE_G64_H

#include "filetypes.h"

class FileTypeG64 : public FileType
{
	CachedTreeNode *node;
public:
    FileTypeG64(CachedTreeNode *n);
    ~FileTypeG64();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);
};

#endif
