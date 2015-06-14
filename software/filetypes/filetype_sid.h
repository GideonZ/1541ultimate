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
	uint32_t offset;
	bool header_valid;
	int numberOfSongs;

	int  prepare(bool);
	void load(void);
public:
    FileTypeSID(CachedTreeNode *n);
    ~FileTypeSID();

    int   readHeader(void);
    void  showInfo(void);
    int   fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(CachedTreeNode *obj);
    static void execute_st(void *obj, void *param);
    static void loadAndRun(void *obj);
    void execute(int);
};

#endif
