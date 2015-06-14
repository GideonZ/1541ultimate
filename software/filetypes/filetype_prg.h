#ifndef FILETYPE_PRG_H
#define FILETYPE_PRG_H

#include "filemanager.h"
#include "filetypes.h"

class FileTypePRG : public FileType
{
	CachedTreeNode *node;
	bool    has_header;
    bool    check_header(File *f);

    static void execute_st(void *obj, void *param);
    void  execute(int selection);
public:
    FileTypePRG(CachedTreeNode *n, bool header);
    ~FileTypePRG();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);
};


#endif
