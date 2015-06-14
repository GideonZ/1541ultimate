#ifndef FILE_H
#define FILE_H

#include "integer.h"
#include "path.h"
#include "file_system.h"

/*
 * The File class is a wrapper to hide the actual implementation
 * of a stream. Right now, the diversification is handled by
 * derivates of the FileSystem class, but future specials
 * could handle direct file implementations as well. However,
 * one thing needs to be noted: the 'node' field should always
 * refer to a valid object. If this variable is set to NULL,
 * the file is invalid. This can be caused by a tree cleanup,
 * at the removal of a media.
 */
#define COPY_INFO 1

class File
{
	FileInfo *info; // refers to a structure with file information, when set to null, this file is invalidated
	mstring pathString;

	// the following function shall only be called by the file manager
	friend class FileManager;
	virtual void    close(void);
public:
    uint32_t handle;   // could be a pointer to an object used in the derived class

    File(FileInfo *n, uint32_t h) {
#if COPY_INFO
    	info = new FileInfo(*n); // copy data!
#else
    	info = n; // no copy
#endif
        handle = h;
    }

    virtual ~File() {
#if COPY_INFO
    	if(info) {
    		delete info;
    	}
#endif
    }

    // functions for reading and writing files
    void invalidate(void) {
#if COPY_INFO
    	if(info) {
    		delete info;
    	}
#endif
		info = NULL;
    }
    bool isValid(void) { return (info != NULL); }
    FileInfo *getFileInfo() { return info; }
    void set_path(char *n) {
    	pathString = n;
    }
    char *get_path() {
    	return pathString.c_str();
    }

    virtual FRESULT sync(void);
    virtual FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT write(void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT seek(uint32_t pos);
    virtual void print_info() { info->fs->file_print_info(this); }
    virtual uint32_t get_size(void)
    {
    	if(!info)
            return -1;
    	return info->size;
    }
    
};

#endif
