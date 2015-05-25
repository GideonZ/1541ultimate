#ifndef FILETYPE_REU_H
#define FILETYPE_REU_H

#include "filetypes.h"
#include "filemanager.h"

class FileTypeREU : public FileType
{
	CachedTreeNode *node;
	int type;
public:
    FileTypeREU(CachedTreeNode *par, int);
    ~FileTypeREU();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);

    static void execute_st(void *obj, void *param);
    void  execute(int selection);
};


#endif
