#ifndef FILETYPE_PRG_H
#define FILETYPE_PRG_H

#include "filetypes.h"
#include "browsable_root.h"

class SubsysCommand;

class FileTypePRG : public FileType
{
	BrowsableDirEntry *node;
	bool    has_header;
    static bool check_header(File *f, bool has_header);
    static int  execute_st(SubsysCommand *);
public:
    FileTypePRG(BrowsableDirEntry *n, bool header);
    ~FileTypePRG();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};


#endif
