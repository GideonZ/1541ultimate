#ifndef FILETYPE_SID_H
#define FILETYPE_SID_H

#include "file_direntry.h"
#include "file_system.h"


class FileTypeSID : public FileDirEntry
{
	File *file;
	BYTE sid_header[0x80];
	WORD song;
	WORD start;
	WORD end;
	WORD player;
	int  prepare(bool);
	void load(void);
public:
    FileTypeSID(FileTypeFactory &fac);
    FileTypeSID(PathObject *par, FileInfo *fi);
    ~FileTypeSID();

    int   fetch_children(void);
	int   get_header_lines(void) { return 3; }
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);
    void execute(int);
};

class SidTune : public PathObject
{
	int index;
public:
	SidTune(PathObject *par, int idx) : PathObject(par), index(idx) { }
	~SidTune() { }

    int   fetch_context_items(IndexedList<PathObject *> &list);
    char *get_name() { return get_display_string(); }
    char *get_display_string();
};

#endif
