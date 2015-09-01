/*
 * fat_filesystem.cc
 *
 *  Created on: Aug 23, 2015
 *      Author: Gideon
 */

#include "fat_filesystem.h"
#include "ff2.h"
#include <stdio.h>
#include <string.h>

FileSystemFAT :: FileSystemFAT(Partition *p) : FileSystem(p)
{
	memset(&fatfs, 0, sizeof(FATFS));
}

FileSystemFAT :: ~FileSystemFAT()
{

}

bool    FileSystemFAT :: init(void)
{
	fs_init_volume(&fatfs, 0);
	if (fatfs.fs_type)
		return true;
	return false;
}

FRESULT FileSystemFAT :: get_free (uint32_t *e)
{
	return fs_getfree(&fatfs, e);
}

bool    FileSystemFAT :: is_writable()
{
	DSTATUS sta = prt->status();
	return (sta == 0); // not writable when there is no disk, not initialized or protected. All flags should be 0
}

FRESULT FileSystemFAT :: sync(void)
{
	return FR_OK; // there is no external sync function apparently in ChanFAT
}

// functions for reading directories
FRESULT FileSystemFAT :: dir_open(const TCHAR *path, Directory **dirout)
{
	*dirout = 0;

	DIR *dir = new DIR;
	FRESULT res = fs_opendir(&fatfs, dir, path);
	if (res == FR_OK) {
		*dirout = new Directory(&fatfs, dir);
	}
	return res;
}

void FileSystemFAT :: dir_close(Directory *d)
{
	DIR *dir = (DIR *)d->handle;
	f_closedir(dp);
}

FRESULT FileSystemFAT :: dir_read(Directory *d, FileInfo *f)
{
	DIR *dir = (DIR *)d->handle;
	FILINFO inf;
	inf.lfname = f->lfname;
	inf.lfsize = f->lfsize;

	FRESULT res = f_readdir(dir, &inf);
	// TODO: copy file attributes in destination
	return res;
}

FRESULT FileSystemFAT :: dir_create(const TCHAR *path)
{
	return fs_mkdir(&fatfs, path);
}


// functions for reading and writing files
FRESULT FileSystemFAT :: file_open(const TCHAR *path, uint8_t flags, File **file)
{
	FATFIL *fil = new FATFIL;
	FRESULT res = fs_open(&fatfs, path, flags, fil);
	if (res == FR_OK) {
		*file = new File(&fatfs, fil);
		return res;
	}
	delete fil;
	*file = 0;
	return res;
}

FRESULT FileSystemFAT :: file_rename(const TCHAR *path, const char *new_name)
{
	return fs_rename(&fatfs, path, new_name);
}

FRESULT FileSystemFAT :: file_delete(const TCHAR *path)
{
	return fs_unlink(&fatfs, path);
}

void    FileSystemFAT :: file_close(File *f)
{
}

FRESULT FileSystemFAT :: file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
	FATFIL *fil = (FATFIL *)f->handle;
	return f_read(fil, buffer, len, transferred);
}

FRESULT FileSystemFAT :: file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
	FATFIL *fil = (FATFIL *)f->handle;
	return f_read(fil, buffer, len, transferred);
}

FRESULT FileSystemFAT :: file_seek(File *f, uint32_t pos)
{
	FATFIL *fil = (FATFIL *)f->handle;
	return f_lseek(fil, pos);
}

FRESULT FileSystemFAT :: file_sync(File *f)
{
	FATFIL *fil = (FATFIL *)f->handle;
	return f_sync(fil);
}
