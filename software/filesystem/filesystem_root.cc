/*
 * filesystem_root.cc
 *
 *  Created on: Aug 30, 2015
 *      Author: Gideon
 */

#include "filesystem_root.h"
#include "filemanager.h"

FileSystem_Root::FileSystem_Root(CachedTreeNode *node) : FileSystem(0) {
	rootnode = node;
	node->get_file_info()->fs = this;
}

FileSystem_Root::~FileSystem_Root() {

}

PathStatus_t FileSystem_Root::walk_path(PathInfo& pathInfo)
{
	// calling walk_path on the root of the file system will traverse through tree nodes.
	// as soon as a file system is encountered, the function returns
	//printf("WalkPathRoot: '%s'\n", path);
	FRESULT fres;
	CachedTreeNode *node = rootnode;

	pathInfo.enterFileSystem(this);

	// example1: /sd/somedir/somefile.d64/hello.prg
	while(pathInfo.hasMore()) {
		node = node->find_child(pathInfo.workPath.getElement(pathInfo.index));
		if (!node)
			return e_DirNotFound;

		pathInfo.replace(node->get_name());
		if (node->probe() < 0) {
			return e_DirNotFound;
		}

		// TODO: is this copy required?
		FileInfo *info = node->get_file_info();
		if (!info)
			return e_DirNotFound;

		FileInfo *ninf = pathInfo.getNewInfoPointer();
		ninf->copyfrom(info);
		pathInfo.index ++;

		if (info->fs) {
			return e_TerminatedOnMount;
		} else {
			ninf->fs = this;
		}
	}
	return e_EntryFound;
}

// functions for reading directories
FRESULT FileSystem_Root :: dir_open(const char *path, Directory **dir, FileInfo *dummy)
{
	FileManager *fm = FileManager :: getFileManager();
	Path *p = fm->get_new_path("root_temp");
	p->cd(path);
	CachedTreeNode *n = rootnode;
	int elements = n->children.get_elements();
	for(int i=0;i<p->getDepth();i++) {
		n = n->find_child(p->getElement(i));
		if (!n) {
			fm->release_path(p);
			return FR_NO_PATH;
		}
		elements = n->probe();
	}

	fm->release_path(p);
	if (!elements) {
		return FR_DENIED;
	}
	*dir = new DirectoryInRoot(this, n);
	return FR_OK;
}

void    FileSystem_Root :: dir_close(Directory *d)
{
	delete d;
}

FRESULT FileSystem_Root :: dir_read(Directory *d, FileInfo *f)
{
	return d->get_entry(*f);
}
