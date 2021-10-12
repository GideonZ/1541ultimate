#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include <string.h>
#include <stdio.h>
#include "partition.h"
#include "file_info.h"
#include "fs_errors_flags.h"
#include "factory.h"

class FileSystem;
class Path;

typedef enum {
	e_DirNotFound,
	e_EntryNotFound,
	e_EntryFound,
	e_TerminatedOnFile,
	e_TerminatedOnMount
} PathStatus_t;

class PathInfo;
class File;
class Directory;

class FileSystem
{
protected:
    Partition *prt;
public:
    FileSystem(Partition *p);
    virtual ~FileSystem();

	static Factory<Partition *, FileSystem *>* getFileSystemFactory() {
		static Factory<Partition *, FileSystem *> file_system_factory;
		return &file_system_factory;
	}

    static  const char *get_error_string(FRESULT res);

	virtual PathStatus_t walk_path(PathInfo& pathInfo);

	virtual bool    init();              // Initialize file system
    virtual bool    is_writable() { return false; } // by default a file system is not writable, unless we implement it
	virtual FRESULT format(const char *name);    // create initial structures of empty disk
	virtual bool    supports_direct_sector_access(void) { return false; }

    virtual FRESULT get_free (uint32_t *e, uint32_t *cs) { *e = 0; *cs = 0; return FR_OK; } // Get number of free sectors on the file system
    virtual FRESULT sync(void) { return FR_OK; } // by default we can't write, and syncing is thus always successful
    
    // functions for reading directories
    // In the following functions 'dirCluster' is a suggestive start cluster.
    // If other than 0 (which means root), it is assumed the path/filenames are relative to that cluster
    virtual FRESULT dir_open(const char *path, Directory **); // Opens directory (creates dir object)
    virtual FRESULT dir_create(const char *path);  // Creates a directory
    
    // functions for reading and writing files
    virtual FRESULT file_open(const char *filename, uint8_t flags, File **);  // Opens file (creates file object)
    virtual FRESULT file_rename(const char *old_name, const char *new_name);  // Renames a file
	virtual FRESULT file_delete(const char *path); // deletes a file


	virtual void    file_print_info(File *f) { } // debug

    virtual bool     needs_sorting() { return false; }
    virtual FRESULT  read_sector(uint8_t *buffer, int track, int sector) { return FR_DENIED; }
    virtual FRESULT  write_sector(uint8_t *buffer, int track, int sector) { return FR_DENIED; }
};

#include "factory.h"

typedef FileSystem *(*fileSystemTestFunction_t)(Partition *p);

extern Factory<Partition *, FileSystem *> file_system_factory;

#include "directory.h"
#include "path.h"
#include "file.h"

class PathInfo {
	FileInfo fileInfo1;
	FileInfo fileInfo2;
public:
	int index;
	Path workPath;
	SubPath subPath;
	FileInfo *last;
	FileInfo *previous;

	PathInfo(FileSystem *fs) : fileInfo1(128), fileInfo2(128), subPath(&workPath) {
		index = 0;
		subPath.start = 0;
		subPath.stop = -1;
		last = 0;
		previous = 0;
		fileInfo1.fs = fs; // just in case nothing happens, the last info should at least point to something valid
	}

	void init(const char *path) {
		if (!path)
			return;
		if (!strlen(path))
			return;
		workPath.cd(path);
	}

	void init(Path *path) {
		if (!path)
			return;
		workPath.cd(path->get_path());
	}

	void init(const char *path, const char *filename) {
		if (path)
			workPath.cd(path);
		if (filename)
			workPath.cd(filename);
	}

	void init(Path *path, const char *filename) {
		if (path)
			workPath.cd(path->get_path());
		if (!filename)
			return;
		workPath.cd(filename);
	}

	bool hasMore() {
		return (index < workPath.depth);
	}

	void enterFileSystem(FileSystem *fs) {
		subPath.start = index;
		FileInfo *info = this->getNewInfoPointer();
		info->fs = fs;
		info->cluster = 0;
		info->lfname[0] = 0;
		info->extension[0] = 0;
		info->attrib = AM_DIR;
	}

	void replace(const char *actualName) {
		workPath.update(index, actualName);
	}

	FileInfo *getNewInfoPointer() {
		if (last == 0) {
			last = &fileInfo1;
		} else if(previous == 0) {
			previous = last;
			last = &fileInfo2;
		} else {
			FileInfo *temp = previous;
			previous = last;
			last = temp;
		}
		return last;
	}

	FileInfo *getLastInfo() {
		if (last == 0) {
			return &fileInfo1;
		}
		return last;
	}

	FileInfo *getParentInfo() {
		if(previous == 0) {
			return getLastInfo();
		}
		return previous;
	}

	const char *getPathFromLastFS(void) {
	    subPath.stop = index;
	    return subPath.get_path();
	}

	const char *getDirectoryFromLastFS(void) {
        subPath.stop = index-1;
        return subPath.get_path();
	}

	SubPath *getSubPath(void) {
	    subPath.stop = index;
	    return &subPath;
	}

	const char *getFullPath(mstring &work, int part) {
		if (part < 0) {
			return workPath.getSub(0, index + part, work);
		} else if(part > 0) {
			return workPath.getSub(0, part, work);
		}
		return workPath.get_path();
	}

	const char *getFileName() {
		int depth = workPath.getDepth();
		if (depth == 0)
			return "";
		return workPath.getElement(depth-1);
	}

	void dumpState(const char *head) {
		printf("%s state of PathInfo structure:\n", head);
		printf("Working path: %s\n", workPath.get_path());
		printf("hasMore: %s\n", hasMore()?"yes":"no");
		printf("Path from FS: %s\n", getPathFromLastFS());
		mstring work;
		printf("Head: %s\n", workPath.getHead(work));
		printf("Filename: %s\n", getFileName());
		printf("LastInfo:\n");
		getLastInfo()->print_info();
		printf("ParentInfo:\n");
		getParentInfo()->print_info();
		printf("-- end of PathInfo state --\n\n");
	}
};

#endif
