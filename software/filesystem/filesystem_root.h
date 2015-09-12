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

	PathStatus_t walk_path(PathInfo& pathInfo);

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *); // Opens directory (creates dir object, NULL = root)
    void    dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
};

class DirectoryInRoot : public Directory
{
	CachedTreeNode *node;
	int index;
public:
	DirectoryInRoot(FileSystem *fs, CachedTreeNode *n) : Directory(fs, 0), node(n) {
		index = 0;
	}
	virtual ~DirectoryInRoot() { }

	FRESULT get_entry(FileInfo &info) {
		if (index >= node->children.get_elements()) {
			return FR_NO_FILE;
		}
		info.copyfrom(node->children[index++]->get_file_info());
		return FR_OK;
	}
};

#endif /* FILESYSTEM_FILESYSTEM_ROOT_H_ */
