#ifndef FILETYPE_BIT_H
#define FILETYPE_BIT_H

#include "file_direntry.h"
#include "flash.h"

class FileTypeBIT : public FileType
{
    Flash *flash;
    void boot(void);
    bool program(int id, void *buffer, int length, char *version, char *descr);

public:
    FileTypeBIT(CachedTreeNode *par, FileInfo *fi);
    ~FileTypeBIT();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<CachedTreeNode *> &list);
    static FileType *test_type(CachedTreeNode *obj);

    void  execute(int selection);
};


#endif
