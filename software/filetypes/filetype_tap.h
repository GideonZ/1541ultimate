#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeTap : public FileType
{
	BrowsableDirEntry *node;
	void closeFile();
public:
    FileTypeTap(BrowsableDirEntry *par);
    ~FileTypeTap();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
    static int execute_st(SubsysCommand *cmd);
    int execute(SubsysCommand *cmd);
};


#endif
