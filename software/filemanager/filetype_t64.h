#ifndef FILETYPE_T64_H
#define FILETYPE_T64_H

#include "filetypes.h"
#include "filemanager.h"

class FileTypeT64 : public FileType
{
	CachedTreeNode *node;
	File *file;
    FileSystem *fs;
public:
    FileTypeT64(CachedTreeNode *obj);
    ~FileTypeT64();

    FileSystem *getFileSystem() { return fs; }
    int   fetch_context_items(IndexedList<Action *> &list);

    static FileType *test_type(CachedTreeNode *obj);
};


#endif
