#ifndef FILETYPE_PRG_H
#define FILETYPE_PRG_H

#include "file_direntry.h"

class FileTypePRG : public FileDirEntry
{
    bool    has_header;
    bool    check_header(File *f);
public:
    FileTypePRG(FileTypeFactory &fac);
    FileTypePRG(PathObject *par, FileInfo *fi, bool header);
    ~FileTypePRG();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};


#endif
