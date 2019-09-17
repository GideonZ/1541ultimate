#ifndef FILETYPE_D81_H
#define FILETYPE_D81_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeD81 : public FileType
{
	BrowsableDirEntry *node;
public:
    FileTypeD81(BrowsableDirEntry *node);
    virtual ~FileTypeD81();

    int   fetch_context_items(IndexedList<Action *> &list);

    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif