#ifndef FILETYPE_G64_H
#define FILETYPE_G64_H

#include "file_direntry.h"

class FileTypeG64 : public FileDirEntry
{
public:
    FileTypeG64(FileTypeFactory &fac);
    FileTypeG64(PathObject *par, FileInfo *fi);
    ~FileTypeG64();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};

#endif
