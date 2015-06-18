#ifndef FILE_DIRENTRY_H
#define FILE_DIRENTRY_H

#include "path.h"
#include "file_system.h"

class FileDirEntry : public CachedTreeNode
{
	int fetch_directory(FileInfo &fi);
public:
    FileDirEntry(CachedTreeNode *par, char *name);
    FileDirEntry(CachedTreeNode *par, FileInfo *fi);
    virtual ~FileDirEntry();

    virtual bool is_ready(void);
    virtual bool is_writable(void);
    virtual int fetch_children(void);
    virtual const char *get_name(void);
	virtual int compare(CachedTreeNode *obj);
};

#endif
