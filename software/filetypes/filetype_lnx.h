#ifndef FILETYPE_LNX_H
#define FILETYPE_LNX_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeLNX : public FileType
{
    BrowsableDirEntry *node;

    static SubsysResultCode_e do_convert_d64(SubsysCommand *cmd);

public:
    FileTypeLNX(BrowsableDirEntry *node);
    ~FileTypeLNX() {}

    int fetch_context_items(IndexedList<Action *> &list) override;
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
