#ifndef FILETYPE_BIN_H
#define FILETYPE_BIN_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeBin : public FileType
{
	BrowsableDirEntry *node;
    static SubsysResultCode_e execute_st(SubsysCommand *cmd);
    SubsysResultCode_e execute(SubsysCommand *cmd);
    SubsysResultCode_e load_kernal(SubsysCommand *cmd);
    SubsysResultCode_e load_dos(SubsysCommand *cmd);
public:
    FileTypeBin(BrowsableDirEntry *node);
    virtual ~FileTypeBin();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
