#ifndef FILE_H
#define FILE_H

#include "integer.h"
#include "path.h"
#include "file_system.h"

/*
 * The File class is a wrapper to hide the actual implementation
 * of a stream. Right now, the diversification is handled by
 * derivates of the FileSystem class, but future specials
 * could handle direct file implementations as well. 
 */
#define COPY_INFO 1

class File
{
    FileSystem *filesystem;
    mstring pathString;

	// the following function shall only be called by the file manager
	friend class FileManager;
public:
    File(FileSystem *fs) { filesystem = fs; }
    virtual ~File() { }

    FileSystem *get_file_system() { return filesystem; }
    bool isValid(void) { return (filesystem != NULL); }
    void invalidate(void) { filesystem = NULL; }
    virtual uint32_t get_size(void) { return 0; }

    const char *get_path() {
    	return pathString.c_str();
    }
    mstring &get_path_reference() {
    	return pathString;
    }

protected:
    // functions for reading and writing files
    virtual FRESULT close(void) { delete this; return FR_NO_FILESYSTEM; }
    virtual FRESULT sync(void) { return FR_NO_FILESYSTEM; }
    virtual FRESULT read(void *buffer, uint32_t len, uint32_t *transferred) { return FR_NO_FILESYSTEM; }
    virtual FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred) { return FR_NO_FILESYSTEM; }
    virtual FRESULT seek(uint32_t pos) { return FR_NO_FILESYSTEM; }
    virtual uint32_t get_inode() { return 0; }
};

#endif
