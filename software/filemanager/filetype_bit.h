#ifndef FILETYPE_BIT_H
#define FILETYPE_BIT_H

#include "file_direntry.h"
#include "flash.h"

class FileTypeBIT : public FileDirEntry
{
    Flash *flash;
    void boot(void);
    bool program(int id, void *buffer, int length, char *version, char *descr);

public:
    FileTypeBIT(FileTypeFactory &fac);
    FileTypeBIT(PathObject *par, FileInfo *fi);
    ~FileTypeBIT();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};


#endif
