#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "file_direntry.h"
#include "file_system.h"


class FileTypeTap : public FileDirEntry
{
	File *file;
public:
    FileTypeTap(FileTypeFactory &fac);
    FileTypeTap(PathObject *par, FileInfo *fi);
    ~FileTypeTap();

    int   fetch_children(void) { return -1; }
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);
    void execute(int);
};


#endif
