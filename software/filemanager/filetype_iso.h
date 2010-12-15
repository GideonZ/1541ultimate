#ifndef FILETYPE_ISO_H
#define FILETYPE_ISO_H

#include "file_direntry.h"
#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"

class FileTypeISO : public FileDirEntry
{
    BlockDevice_File *blk;
    Partition *prt;
    FileSystem *fs;
public:
    FileTypeISO(FileTypeFactory &fac);
    FileTypeISO(PathObject *par, FileInfo *fi);
    ~FileTypeISO();

    bool  is_writable(void) { return false; }
    int   fetch_children(void);
	int   get_header_lines(void) { return 0; }
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);
};

#endif
