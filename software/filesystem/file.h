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
	//FileInfo *info; // refers to a structure with file information, when set to null, this file is invalidated
	FileSystem *filesystem;
	mstring pathString;

	// the following function shall only be called by the file manager
	friend class FileManager;
	virtual void    close(void);
public:
    void *handle;   // could be a pointer to an object used in the derived class

    File(FileSystem *fs, void *h) {
/*
#if COPY_INFO
    	info = new FileInfo(*n); // copy data!
#else
    	info = n; // no copy
#endif
*/
    	filesystem = fs;
    	handle = h;
    }

    virtual ~File() {
/*
#if COPY_INFO
    	if(info) {
    		delete info;
    	}
#endif
*/
    }

    // functions for reading and writing files
    void invalidate(void) {
/*
#if COPY_INFO
    	if(info) {
    		delete info;
    	}
#endif
		info = NULL;
*/
    	filesystem = NULL;
    }
    bool isValid(void) { return (filesystem != NULL); }
//    FileInfo *getFileInfo() { return filesystem; }
    void set_path(const char *n) {
    	pathString = n;
    }
    const char *get_path() {
    	return pathString.c_str();
    }

    virtual FRESULT sync(void);
    virtual FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT write(void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT seek(uint32_t pos);
    virtual void print_info() { filesystem->file_print_info(this); }
    virtual uint32_t get_size(void)
    {
    	if(!filesystem)
            return 0;
    	return filesystem->get_file_size(this);
    }
    FileSystem *get_file_system() { return filesystem; }
    uint32_t 	get_inode() { return filesystem->get_inode(this); }
    const char *get_name() { return "get_name"; } // TODO: Not yet implemented
};

#endif
