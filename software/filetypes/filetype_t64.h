#ifndef FILETYPE_T64_H
#define FILETYPE_T64_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeT64 : public FileType
{
	BrowsableDirEntry *node;
public:
    FileTypeT64(BrowsableDirEntry *node);
    virtual ~FileTypeT64();

    int   fetch_context_items(IndexedList<Action *> &list);

    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
