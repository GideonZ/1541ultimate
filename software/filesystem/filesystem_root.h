/*
 * filesystem_root.h
 *
 *  Created on: Aug 30, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FILESYSTEM_ROOT_H_
#define FILESYSTEM_FILESYSTEM_ROOT_H_

#include "file_system.h"
#include "cached_tree_node.h"

class FileSystem_Root: public FileSystem {
	CachedTreeNode *rootnode;
public:
	FileSystem_Root(CachedTreeNode *node);
	virtual ~FileSystem_Root();

	PathStatus_t walk_path(Path *path, const char *fn, PathInfo& pathInfo);

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **); // Opens directory (creates dir object, NULL = root)
    void    dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    FRESULT dir_create(const char *path);  // Creates a directory
};

#endif /* FILESYSTEM_FILESYSTEM_ROOT_H_ */
