#ifndef FILETYPE_U2U_H
#define FILETYPE_U2U_H

#include "filetypes.h"
#include "filemanager.h"


class FileTypeUpdate : public FileType
{
	CachedTreeNode *node;
public:
	FileTypeUpdate(CachedTreeNode *par);
    ~FileTypeUpdate();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(FileInfo *inf);

    static void execute(void *obj, void *param);
};


#endif
