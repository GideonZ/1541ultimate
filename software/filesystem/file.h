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

class File
{
public:
	FileInfo *info; // refers to a structure with file information, when set to null, this file is invalidated
    uint32_t handle;   // could be a pointer to an object used in the derived class

    File(FileInfo *n, uint32_t h) {
        info = n;
        handle = h;
    }

    virtual ~File() { }

    // functions for reading and writing files
    void invalidate(void) { info = NULL; }
    bool isValid(void) { return info != NULL; }
    virtual void    close(void);
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
