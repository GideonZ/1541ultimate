#ifndef FILETYPE_DDL_H
#define FILETYPE_DDL_H

#include "filetypes.h"
#include "browsable_root.h"

class SubsysCommand;

class FileTypeDDL: public FileType
{
	BrowsableDirEntry *node;
	bool    has_header;
    static bool check_header(File *f, bool has_header);
    static int  execute_st(SubsysCommand *);
public:
    FileTypeDDL(BrowsableDirEntry *n, bool header);
    ~FileTypeDDL();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
};


#endif
