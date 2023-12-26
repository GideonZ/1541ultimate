#ifndef FILETYPE_ESP_H
#define FILETYPE_ESP_H

#include "filetypes.h"
#include "filemanager.h"
#include "subsys.h"

class FileTypeESP : public FileType
{
	BrowsableDirEntry *browsable;
public:
    FileTypeESP(BrowsableDirEntry *par);
    ~FileTypeESP();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *inf);

    static SubsysResultCode_e execute(SubsysCommand *);
};

#endif
