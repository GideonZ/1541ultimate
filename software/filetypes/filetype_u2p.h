#ifndef FILETYPE_U2P_H
#define FILETYPE_U2P_H

#include "filetypes.h"
#include "filemanager.h"
#include "subsys.h"

class FileTypeUpdate : public FileType
{
	BrowsableDirEntry *browsable;
    static uint32_t load_format_0(SubsysCommand *cmd, File *file, uint32_t remain);
    static uint32_t load_format_1(SubsysCommand *cmd, File *file, uint32_t remain);
    int format;
public:
	FileTypeUpdate(BrowsableDirEntry *par, int);
    ~FileTypeUpdate();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *inf);

    static SubsysResultCode_e execute(SubsysCommand *);
};

#endif
