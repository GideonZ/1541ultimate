/*
 * filesystem_root.cc
 *
 *  Created on: Aug 30, 2015
 *      Author: Gideon
 */

#include "filesystem_root.h"

FileSystem_Root::FileSystem_Root(CachedTreeNode *node) : FileSystem(0) {
	rootnode = node;
}

FileSystem_Root::~FileSystem_Root() {

}

PathStatus_t FileSystem_Root::walk_path(Path *path, const char *fn, PathInfo& pathInfo)
{
	// calling walk_path on the root of the file system will traverse through tree nodes.
	// as soon as a file system is encountered, the function returns
	printf("WalkPathRoot: '%s':'%s'\n", path->get_path(), fn);
	FRESULT fres;
	CachedTreeNode *node = rootnode;
	pathInfo.pathFromRootOfFileSystem.cd("/"); // reset path from Root

	// example1: /sd/somedir/somefile.d64/hello.prg
	for(int i=0;i<path->getDepth();i++) {
		node->probe();
		CachedTreeNode *newnode = node->find_child(path->getElement(i));
		if (!newnode)
			return e_DirNotFound;

		// TODO: is this copy required?
		FileInfo *info = newnode->get_file_info();
		pathInfo.fileInfo.copyfrom(info);

		if (!info)
			return e_DirNotFound;

		pathInfo.remainingPath.removeFirst();

		if (info->fs) {
			pathInfo.fileSystem = info->fs;
			return e_TerminatedOnMount;
		}

		pathInfo.pathFromRootOfFileSystem.cd(path->getElement(i));
	}
	return e_EntryFound;
}

// functions for reading directories
FRESULT FileSystem_Root :: dir_open(const char *path, Directory **)
{

}

void    FileSystem_Root :: dir_close(Directory *d)
{
	// Closes (and destructs dir object)
}

FRESULT FileSystem_Root :: dir_read(Directory *d, FileInfo *f)
{
	// reads next entry from dir
}

FRESULT FileSystem_Root :: dir_create(const char *path)
{
	// Creates a directory
}

