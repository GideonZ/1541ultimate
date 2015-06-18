#ifndef FILETYPE_D64_H
#define FILETYPE_D64_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeD64 : public FileType
{
	BrowsableDirEntry *node;
public:
    FileTypeD64(BrowsableDirEntry *node);
    virtual ~FileTypeD64();

    int   fetch_context_items(IndexedList<Action *> &list);

    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
