/*
 * filesystem_dummy.h
 *
 *  Created on: Sep 12, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FILESYSTEM_DUMMY_H_
#define FILESYSTEM_FILESYSTEM_DUMMY_H_

#include "file_system.h"

class FileSystemDummy : public FileSystem
{
public:
	FileSystemDummy() : FileSystem(0) { }
    ~FileSystemDummy() { }

    // Initialize file system
    bool    init(void) { return true; }

    // Get number of free sectors on the file system
    FRESULT get_free (uint32_t *a) {
    	*a = 0;
    	return FR_OK;
    }

    // Clean-up cached data
    FRESULT sync(void) { return FR_OK; }

	PathStatus_t walk_path(PathInfo& pathInfo) {
		pathInfo.enterFileSystem(this);
		mstring work;
		printf("Walk path on dummy FS: %s\n", pathInfo.workPath.getTail(pathInfo.index, work));
		if (pathInfo.hasMore()) {
			printf("Connect to: %s\n", pathInfo.workPath.getElement(pathInfo.index));
			pathInfo.index ++;
		}
		if (pathInfo.hasMore()) {
			printf("STAT: %s\n", pathInfo.workPath.getTail(pathInfo.index, work));
		}
		return e_EntryFound;
	}

	// functions for reading directories
    FRESULT dir_open(const char *path, Directory **dir)
    {   // Opens directory (creates dir object)
    	printf("Dummy open dir: %s\n", path);
    	*dir = new Directory();
    	return FR_OK;
    }

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **)
    {   // Opens file (creates file object)
    	printf("Dummy open file: %s\n", filename);
    	return FR_DENIED;
    }
};


#endif /* FILESYSTEM_FILESYSTEM_DUMMY_H_ */
