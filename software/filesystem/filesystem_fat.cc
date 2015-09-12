/*
 * fat_filesystem.cc
 *
 *  Created on: Aug 23, 2015
 *      Author: Gideon
 */

#include "filesystem_fat.h"
#include "filemanager.h"
#include <stdio.h>
#include <string.h>

FileSystemFAT :: FileSystemFAT(Partition *p) : FileSystem(p)
{
	memset(&fatfs, 0, sizeof(FATFS));
	fatfs.drv = p;
}

FileSystemFAT :: ~FileSystemFAT()
{

}

//private
void FileSystemFAT :: copy_info(FILINFO *fi, FileInfo *inf)
{
	inf->fs = this;
	inf->attrib = fi->fattrib;
	inf->size = fi->fsize;
	inf->date = fi->fdate;
	inf->time = fi->ftime;
	inf->cluster = fi->fclust;

	get_extension(fi->fname, inf->extension);

	if(!(*inf->lfname)) {
		strncpy(inf->lfname, fi->fname, inf->lfsize);
	}
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
	fs_sync(&fatfs);
}

// functions for reading directories
FRESULT FileSystemFAT :: dir_open(const TCHAR *path, Directory **dirout, FileInfo *inf)
{
	*dirout = 0;

//	printf("FAT Open DIR: %s\n", path);

	DIR *dp = new DIR;
	FRESULT res;
	if (inf) {
		dp->fs = &fatfs;
		dp->id = fatfs.id;
		if (!inf) {
			dp->sclust = 0; // root directory
		} else {
			dp->sclust = inf->cluster;
		}
		res = dir_sdi(dp, 0);			/* Rewind directory */

/*
#if _FS_LOCK
		if (res == FR_OK) {
			if (dp->sclust) {
				dp->lockid = inc_lock(dp, 0);	// Lock the sub directory
				if (!dp->lockid)
					res = FR_TOO_MANY_OPEN_FILES;
			} else {
				dp->lockid = 0;	// Root directory need not to be locked
			}
		}
#endif
*/

	} else if(path) {
		res = fs_opendir(&fatfs, dp, path);
	} else {
		res = FR_INVALID_PARAMETER;
	}
	if (res == FR_OK) {
		*dirout = new Directory(this, dp);
	} else {
		delete dp;
	}
	return res;
}

void FileSystemFAT :: dir_close(Directory *d)
{
	DIR *dir = (DIR *)d->handle;
	f_closedir(dir);
	delete dir;
	delete d;
}

FRESULT FileSystemFAT :: dir_read(Directory *d, FileInfo *f)
{
	DIR *dir = (DIR *)d->handle;
	FILINFO inf;
	inf.lfname = f->lfname;
	inf.lfsize = f->lfsize;

	FRESULT res = f_readdir(dir, &inf);
	if (dir->sect == 0)
		return FR_NO_FILE;
	copy_info(&inf, f);
	return res;
}

FRESULT FileSystemFAT :: dir_create(const TCHAR *path)
{
	return fs_mkdir(&fatfs, path);
}


FRESULT FileSystemFAT :: file_open(const char *path, Directory *dir, const char *filename, uint8_t flags, File **file)
{
//	printf("FAT Open file: %s (%s)\n", path, filename);

	FIL *fil = new FIL;
	FRESULT res = fs_open(&fatfs, path, flags, fil);

	if (res == FR_OK) {
		*file = new File(this, fil);
		return res;
	}
	delete fil;
	*file = 0;
	return res;
}

uint32_t FileSystemFAT :: get_file_size(File *f)
{
	FIL *fil = (FIL *)f->handle;
	return fil->fsize;
}

uint32_t FileSystemFAT :: get_inode(File *f)
{
	FIL *fil = (FIL *)f->handle;
	return fil->sclust;
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
	FIL *fil = (FIL *)f->handle;
	f_close(fil);
}

FRESULT FileSystemFAT :: file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
	FIL *fil = (FIL *)f->handle;
	return f_read(fil, buffer, len, transferred);
}

FRESULT FileSystemFAT :: file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
	FIL *fil = (FIL *)f->handle;
	return f_write(fil, buffer, len, transferred);
}

FRESULT FileSystemFAT :: file_seek(File *f, uint32_t pos)
{
	FIL *fil = (FIL *)f->handle;
	return f_lseek(fil, pos);
}

FRESULT FileSystemFAT :: file_sync(File *f)
{
	FIL *fil = (FIL *)f->handle;
	return f_sync(fil);
}

/*-----------------------------------------------------------------------*/
/* Load boot record and check if it is an FAT boot record                */
/*-----------------------------------------------------------------------*/
#define BS_55AA             510
#define BS_FilSysType       54
#define BS_FilSysType32     82

FileSystem *FileSystemFAT::test (Partition *prt)
{
	uint32_t secsize;
    DRESULT status = prt->ioctl(GET_SECTOR_SIZE, &secsize);
    uint8_t *buf;
    if(status)
        return NULL;
    if(secsize > 4096)
        return NULL;
    if(secsize < 512)
        return NULL;

    buf = new uint8_t[secsize];

    DRESULT dr = prt->read(buf, 0, 1);
    if (dr != RES_OK) {	/* Load boot record */
    	printf("FileSystemFAT::test failed, because reading sector failed: %d\n", dr);
    	return NULL;
    }

	if (LD_WORD(&buf[BS_55AA]) != 0xAA55) {	/* Check record signature (always placed at offset 510 even if the sector size is >512) */
		delete buf;
		return NULL;
	}
	if ((LD_DWORD(&buf[BS_FilSysType]) & 0xFFFFFF) == 0x544146)	/* Check "FAT" string */
		return new FileSystemFAT(prt);
	if ((LD_DWORD(&buf[BS_FilSysType32]) & 0xFFFFFF) == 0x544146)
		return new FileSystemFAT(prt);

	delete buf;
	return NULL;
}

#include "globals.h"
FactoryRegistrator<Partition *, FileSystem *> fat_tester(Globals :: getFileSystemFactory(), FileSystemFAT :: test);
