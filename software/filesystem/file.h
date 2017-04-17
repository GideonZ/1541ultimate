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
	FileSystem *filesystem;
	mstring pathString;

	// the following function shall only be called by the file manager
	friend class FileManager;
	virtual void    close(void);
public:
    void *handle;   // could be a pointer to an object used in the derived class

    File(FileSystem *fs, void *h) {
    	filesystem = fs;
    	handle = h;
    }

    virtual ~File() {
    }

    // functions for reading and writing files
    void invalidate(void) {
    	filesystem = NULL;
    }

    bool isValid(void) { return (filesystem != NULL); }

    const char *get_path() {
    	return pathString.c_str();
    }
    mstring &get_path_reference() {
    	return pathString;
    }

    virtual FRESULT sync(void);
    virtual FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT seek(uint32_t pos);
//    virtual void print_info() { filesystem->file_print_info(this); }
    virtual uint32_t get_size(void)
    {
    	if(!filesystem)
            return 0;
    	return filesystem->get_file_size(this);
    }
    FileSystem *get_file_system() { return filesystem; }
    uint32_t 	get_inode() { return filesystem->get_inode(this); }
};

#endif
