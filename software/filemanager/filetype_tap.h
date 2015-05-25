#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "filetypes.h"
#include "filemanager.h"

class FileTypeTap : public FileType
{
	CachedTreeNode *node;
	File *file;
	void closeFile();
public:
    FileTypeTap(CachedTreeNode *par);
    ~FileTypeTap();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);
    static void execute_st(void *, void *);
    void execute(int);
};


#endif
