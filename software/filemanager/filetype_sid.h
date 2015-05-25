#ifndef FILETYPE_SID_H
#define FILETYPE_SID_H

#include "filetypes.h"
#include "filemanager.h"

class FileTypeSID : public FileType
{
	FileManager *fm;
	CachedTreeNode *node;
	File *file;
	uint8_t sid_header[0x80];
	uint16_t song;
	uint16_t start;
	uint16_t end;
	uint16_t player;
	int  prepare(bool);
	void load(void);
public:
    FileTypeSID(CachedTreeNode *n);
    ~FileTypeSID();

    int   fetch_children(void);
    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);
    void execute(int);
};

class SidTune : public CachedTreeNode
{
	int index;
public:
	SidTune(CachedTreeNode *par, int idx) : CachedTreeNode(par), index(idx) { }
	~SidTune() { }

    int   fetch_context_items(IndexedList<CachedTreeNode *> &list);
    char *get_name() { return get_display_string(); }
    char *get_display_string();
};

#endif
