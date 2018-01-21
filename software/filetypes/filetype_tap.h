#ifndef FILETYPE_TAP_H
#define FILETYPE_TAP_H

#include "filetypes.h"
#include "browsable_root.h"
#include "indexed_list.h"

typedef struct
{
    uint32_t offset;
    char name[28];
} TapIndexEntry;

class FileTypeTap : public FileType
{
	BrowsableDirEntry *node;
	void closeFile();
	bool indexValid;
	void readIndexFile();
	void parseIndexFile(File *f);
	IndexedList<TapIndexEntry *> tapIndices;
public:
    FileTypeTap(BrowsableDirEntry *par);
    ~FileTypeTap();

    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
    static int execute_st(SubsysCommand *cmd);
    int execute(SubsysCommand *cmd);
};


#endif
