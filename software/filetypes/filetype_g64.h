#ifndef FILETYPE_G64_H
#define FILETYPE_G64_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeG64 : public FileType
{
	BrowsableDirEntry *node;
public:
    FileTypeG64(BrowsableDirEntry *n);
    ~FileTypeG64();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
