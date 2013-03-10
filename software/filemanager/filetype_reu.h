#ifndef FILETYPE_REU_H
#define FILETYPE_REU_H

#include "file_direntry.h"

class FileTypeREU : public FileDirEntry
{
    int type;
public:
    FileTypeREU(FileTypeFactory &fac);
    FileTypeREU(PathObject *par, FileInfo *fi, int);
    ~FileTypeREU();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};


#endif
