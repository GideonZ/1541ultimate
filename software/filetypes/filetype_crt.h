#ifndef FILETYPE_CRT_H
#define FILETYPE_CRT_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeCRT : public FileType
{
	BrowsableDirEntry *node;

    static int executeFlash_st(SubsysCommand *cmd);
public:
    FileTypeCRT(BrowsableDirEntry *node);
    ~FileTypeCRT();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);

    static int execute_st(SubsysCommand *cmd);
};


#endif
