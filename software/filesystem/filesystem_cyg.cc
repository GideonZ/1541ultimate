/*
 * cygwin_fs.cc
 *
 *  Created on: Jun 13, 2015
 *      Author: Gideon
 */

#include <stdio.h>
#include <dirent.h>
#include "directory.h"
#include <sys/stat.h>
#include "file_direntry.h"
#include "cygwin_fs.h"
#include "filemanager.h"

struct CygwinDirHandle
{
	DIR *d;
	char *dir_name;
};

FileSystemCygwin :: FileSystemCygwin() : FileSystem(0)
{

}

FileSystemCygwin :: ~FileSystemCygwin()
{

}

bool FileSystemCygwin :: init(void)
{
	return true;
}

// functions for reading directories
Directory *FileSystemCygwin :: dir_open(FileInfo *info)
{
	// Opens directory (creates dir object, NULL = root)
    DIR *d;
    mstring work("/");

    if ((info) && (info->object)) {
    	printf("%016x %s\n", info->object, (char *)info->object);
    	work = (char *)info->object; // SUPER DIRTY, but it's just a temporary wrapper to test other stuff...
    	work += info->lfname;
    	work += "/";
    	printf("Cygwin dir open %s\n", work.c_str());
    }

    d = opendir(work.c_str());

    if (d) {
    	CygwinDirHandle *handle = new CygwinDirHandle;
    	handle->d = d;
    	handle->dir_name = new char[strlen(work.c_str()) + 1];
    	strcpy(handle->dir_name, work.c_str());

    	printf("Handle = %p %s\n", handle->d, handle->dir_name);
    	Directory *directory = new Directory(this, handle);
		return directory;
	}
	return 0;
}

void FileSystemCygwin :: dir_close(Directory *d)
{
	// Closes (and destructs dir object)

	CygwinDirHandle *handle = (CygwinDirHandle *)d->handle;
	closedir(handle->d);
	//delete handle->dir_name;
	delete handle;
	delete d;
}

FRESULT FileSystemCygwin :: dir_read(Directory *d, FileInfo *f)
{
	// reads next entry from dir
	CygwinDirHandle *handle = (CygwinDirHandle *)d->handle;
	DIR *dir = handle->d;
	struct stat st;

	mstring work;

	struct dirent *ent;
	if (dir) {
		ent = readdir(dir);
		if (ent) {
			f->attrib = 0;
			f->fs = this;
			if (ent->d_type == DT_DIR)
				f->attrib = AM_DIR;

			f->object = handle->dir_name; // SUPER DIRTY
			strncpy(f->lfname, ent->d_name, f->lfsize);
			work = handle->dir_name;
			work += ent->d_name;
			stat(work.c_str(), &st);
			// printf("%s %b %d %8x\n", work.c_str(), ent->d_type, st.st_size, st.st_mode);
			f->size = st.st_size;
			get_extension(ent->d_name, f->extension);
			if ((f->size == 0) && (ent->d_type != 0x08))
				f->attrib |= AM_DIR; // temp hack
			return FR_OK;
		}
	}
	return FR_NO_FILE;
}

// functions for reading and writing files
File   *FileSystemCygwin :: file_open(FileInfo *info, uint8_t flags)
{
	// Opens file (creates file object)
	mstring work;
	work = (char *)(info->object); // SUPER DIRTY, but it's just a temporary wrapper to test other stuff...
	work += info->lfname;

	printf("Cygwin open file %s\n", work.c_str());
	FILE *cygf = fopen(work.c_str(), "rb+");
	if (cygf) {
		File *ultf = new File(info, (uint32_t)cygf);
		return ultf;
	}
	return 0;
}

void    FileSystemCygwin :: file_close(File *f)
{
	FILE *cygf = (FILE *)f->handle;
	fclose(cygf);
	delete f;
}

FRESULT FileSystemCygwin :: file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
	FILE *cygf = (FILE *)f->handle;
	*transferred = fread(buffer, 1, len, cygf);
	return FR_OK;
}

FRESULT FileSystemCygwin :: file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred)
{
	FILE *cygf = (FILE *)f->handle;
	*transferred = fwrite(buffer, 1, len, cygf);
	return FR_OK;
}

FRESULT FileSystemCygwin :: file_seek(File *f, uint32_t pos)
{
	FILE *cygf = (FILE *)f->handle;
	fseek(cygf, pos, SEEK_SET);
	return FR_OK;
}

class PCFileSystemRegistrar
{
public:
	PCFileSystemRegistrar() {
		FileInfo *inf = new FileInfo("SD");
		inf->attrib = AM_DIR;
		inf->fs = new FileSystemCygwin();
		CachedTreeNode *node = new FileDirEntry(NULL, inf);
		FileManager :: getFileManager() -> add_root_entry(node);
	}
};

PCFileSystemRegistrar pc;
