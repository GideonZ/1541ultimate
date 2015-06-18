/*
 * globals.h
 *
 *  Created on: May 23, 2015
 *      Author: Gideon
 */

#ifndef APPLICATION_ULTIMATE_GLOBALS_H_
#define APPLICATION_ULTIMATE_GLOBALS_H_

#include "indexed_list.h"
#include "managed_array.h"
#include "factory.h"

class ObjectWithMenu;
class CachedTreeNode;
class FileInfo;
class FileType;
class Partition;
class FileSystem;
class FileSystemInFile;
class BrowsableDirEntry;
class SubSystem;

class Globals
{
public:
	// named generics, these cannot be placed in the classes themselves

	static IndexedList<ObjectWithMenu*>* getObjectsWithMenu() {
    	static IndexedList<ObjectWithMenu*> objects_with_menu(8, NULL);
    	return &objects_with_menu;
    }

	static ManagedArray<SubSystem *>* getSubSystems() {
		static ManagedArray<SubSystem *> subsystem_array(16, NULL);
		return &subsystem_array;
	}

	static Factory<CachedTreeNode *, FileSystemInFile *>* getEmbeddedFileSystemFactory() {
		static Factory<CachedTreeNode *, FileSystemInFile *> embedded_fs_factory;
		return &embedded_fs_factory;
	}

	static Factory<BrowsableDirEntry *, FileType *>* getFileTypeFactory() {
		static Factory<BrowsableDirEntry *, FileType *> file_type_factory;
		return &file_type_factory;
	}

	static Factory<Partition *, FileSystem *>* getFileSystemFactory() {
		static Factory<Partition *, FileSystem *> file_system_factory;
		return &file_system_factory;
	}
};


#endif /* APPLICATION_ULTIMATE_GLOBALS_H_ */
