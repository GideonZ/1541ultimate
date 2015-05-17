#ifndef FILETYPE_D64_H
#define FILETYPE_D64_H

#include "file_direntry.h"
#include "partition.h"
#include "file_system.h"
#include "blockdev_file.h"
#include "filetypes.h"

class FileTypeD64 : public FileType
{
	CachedTreeNode *node;
	BlockDevice_File *blk;
    Partition *prt;
    FileSystem *fs;
public:
    FileTypeD64(CachedTreeNode *par);
    virtual ~FileTypeD64();

    FileSystem *getFileSystem() { return fs; }
//	int   get_header_lines(void) { return 1; }
    int   fetch_context_items(IndexedList<Action *> &list);

    static FileType *test_type(CachedTreeNode *obj);
    static void test_action(void *obj, void *prm);
};

#endif
