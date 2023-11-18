#ifndef FILETYPE_VPL_H
#define FILETYPE_VPL_H

#include "filetypes.h"
#include "filemanager.h"
#include "subsys.h"
#include "u64_config.h"

class FileTypePalette : public FileType
{
	BrowsableDirEntry *browsable;
public:
	FileTypePalette(BrowsableDirEntry *par);
    ~FileTypePalette();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *inf);

    static SubsysResultCode_e execute(SubsysCommand *);
    static SubsysResultCode_e executeFlash(SubsysCommand *cmd);

    static bool parseVplFile(File *f, uint8_t rgb[16][3]);
};

#endif
