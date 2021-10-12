#ifndef FILETYPE_G64_H
#define FILETYPE_G64_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeG64 : public FileType
{
	BrowsableDirEntry *node;
	int ftype;
public:
    FileTypeG64(BrowsableDirEntry *n, int);
    ~FileTypeG64();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
    static int runDisk_st(SubsysCommand *cmd);
};

#endif
