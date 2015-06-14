#ifndef FILETYPE_ISO_H
#define FILETYPE_ISO_H

#include "file_direntry.h"
#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"
#include "filetypes.h"

class FileTypeISO : public FileType
{
    BlockDevice_File *blk;
    Partition *prt;
    FileSystem *fs;
public:
    FileTypeISO(CachedTreeNode *filenode);
    ~FileTypeISO();

    FileSystem *getFileSystem() { return fs; }

    static FileType *test_type(CachedTreeNode *obj);
};

#endif
