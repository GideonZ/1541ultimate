#ifndef FILETYPE_BIN_H
#define FILETYPE_BIN_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeCfg : public FileType
{
	BrowsableDirEntry *node;
    static SubsysResultCode_e execute_st(SubsysCommand *cmd);
    SubsysResultCode_e execute(SubsysCommand *cmd);
public:
    FileTypeCfg(BrowsableDirEntry *node);
    virtual ~FileTypeCfg();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
