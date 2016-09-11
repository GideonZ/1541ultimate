#ifndef FILETYPE_U2P_H
#define FILETYPE_U2P_H

#include "filetypes.h"
#include "filemanager.h"
#include "subsys.h"

class FileTypeUpdate : public FileType
{
	BrowsableDirEntry *browsable;
public:
	FileTypeUpdate(BrowsableDirEntry *par);
    ~FileTypeUpdate();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *inf);

    static int execute(SubsysCommand *);
};

#endif
