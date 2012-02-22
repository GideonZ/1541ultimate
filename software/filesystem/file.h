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
	Path   *path;     // refers to the path, if any
	PathObject *node; // refers to the path object, if any
    FileSystem *fs;   // temporary public... TODO
    DWORD handle;     // could be a pointer to an object used in the derived class


    File(FileSystem *f, DWORD h) {
        path = NULL;
        node = NULL;
        handle = h;
        fs = f;
    }

    ~File() { }

    // functions for reading and writing files
    void invalidate(void) { node = NULL; }
    virtual void    close(void);
    virtual FRESULT sync(void);
    virtual FRESULT read(void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT write(void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT seek(DWORD pos);
    virtual void print_info() { fs->file_print_info(this); }
    virtual int get_size(void)
    {
        if(!node)
            return -1;
        FileInfo *fi = node->get_file_info();
        if(!fi)
            return -2;
        return (int)fi->size; 
    }
    
};

#endif
