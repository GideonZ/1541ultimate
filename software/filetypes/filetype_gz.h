#ifndef FILETYPE_GZ_H
#define FILETYPE_GZ_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeGZ : public FileType
{
    BrowsableDirEntry *node;

    static SubsysResultCode_e do_decompress(SubsysCommand *cmd);

public:
    FileTypeGZ(BrowsableDirEntry *node);
    ~FileTypeGZ() {}

    int fetch_context_items(IndexedList<Action *> &list) override;
    static FileType *test_type(BrowsableDirEntry *obj);
};

#endif
