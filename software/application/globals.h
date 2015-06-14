/*
 * globals.h
 *
 *  Created on: May 23, 2015
 *      Author: Gideon
 */

#ifndef APPLICATION_ULTIMATE_GLOBALS_H_
#define APPLICATION_ULTIMATE_GLOBALS_H_

#include "indexed_list.h"
#include "factory.h"
//#include "cached_tree_node.h"
//#include "filemanager.h"
//#include "partition.h"
//#include "indexed_list.h"
//#include "event.h"
//#include "config.h"
//#include "menu.h"
//#include "filetypes.h"

class ObjectWithMenu;
class CachedTreeNode;
class FileInfo;
class FileType;
class Partition;
class FileSystem;
class FileSystemInFile;

class Globals
{
public:
	// named generics, these cannot be placed in the classes themselves

	static IndexedList<ObjectWithMenu*>* getObjectsWithMenu() {
    	static IndexedList<ObjectWithMenu*> objects_with_menu(8, NULL);
    	return &objects_with_menu;
    }

    static Factory<CachedTreeNode *, FileSystemInFile *>* getEmbeddedFileSystemFactory() {
		static Factory<CachedTreeNode *, FileSystemInFile *> embedded_fs_factory;
		return &embedded_fs_factory;
	}

	static Factory<FileInfo *, FileType *>* getFileTypeFactory() {
		static Factory<FileInfo *, FileType *> file_type_factory;
		return &file_type_factory;
	}

	static Factory<Partition *, FileSystem *>* getFileSystemFactory() {
		static Factory<Partition *, FileSystem *> file_system_factory;
		return &file_system_factory;
	}
};


#endif /* APPLICATION_ULTIMATE_GLOBALS_H_ */
