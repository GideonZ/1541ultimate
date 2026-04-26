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
    static SubsysResultCode_e execute_st(SubsysCommand *cmd);
    static SubsysResultCode_e enter_st(SubsysCommand *cmd);
    SubsysResultCode_e execute(SubsysCommand *cmd);

    int getCustomBrowsables(Browsable *, IndexedList<Browsable *> &list);
};

class BrowsableTapEntry : public Browsable
{
    Browsable *parent;
    TapIndexEntry *tiEntry;
public:
    BrowsableTapEntry(Browsable *p, TapIndexEntry *ti) : parent(p), tiEntry(ti) {

    }

    virtual ~BrowsableTapEntry() { }

    Browsable *getParent() { return parent; }
    const char *getName() { return "BrowsableTapEntry"; }
    void fetch_context_items(IndexedList<Action *> &list);
    IndexedList<Browsable *> *getSubItems(int &error) { error = -1; return &children; }
    void getDisplayString(char *buffer, int width) {
        sprintf(buffer, "%#s \eE%6x", width-8, tiEntry->name, tiEntry->offset);
    }
};

#endif
