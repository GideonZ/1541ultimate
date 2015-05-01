#ifndef FILE_DIRENTRY_H
#define FILE_DIRENTRY_H

#include "path.h"
#include "file_system.h"

class FileType;

class FileDirEntry : public CachedTreeNode
{
	FileType *discovered_type;
public:
    FileDirEntry(CachedTreeNode *par, char *name);
    FileDirEntry(CachedTreeNode *par, FileInfo *fi);
    virtual ~FileDirEntry();

    virtual bool is_writable(void);
    virtual int fetch_children(void);
    virtual char *get_name(void);
    virtual char *get_display_string(void);
	virtual int compare(CachedTreeNode *obj);

};


#endif
