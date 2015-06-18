#ifndef FILETYPE_REU_H
#define FILETYPE_REU_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeREU : public FileType
{
	BrowsableDirEntry *node;
	int type;
public:
    FileTypeREU(BrowsableDirEntry *par, int);
    ~FileTypeREU();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);

    static int execute_st(SubsysCommand *cmd);
};


#endif
