/*
 * fat_filesystem.cc
 *
 *  Created on: Aug 23, 2015
 *      Author: Gideon
 */

#include "filesystem_fat.h"
#include "filemanager.h"
#include "chanfat_manager.h"
#include <stdio.h>
#include <string.h>

FileSystemFAT :: FileSystemFAT(Partition *p) : FileSystem(p)
{
    uint8_t drv = ChanFATManager :: getManager() -> addDrive(p);
    memset(&fatfs, 0, sizeof(FATFS));

    if (drv != 0xFF) {
        driveIndex = drv;
        sprintf(prefix, "%d:", drv);
    }
}

FileSystemFAT :: ~FileSystemFAT()
{
    f_unmount(prefix);
    ChanFATManager :: getManager() -> removeDrive(driveIndex);
}

bool    FileSystemFAT :: init(void)
{
    FRESULT fres = f_mount(&fatfs, prefix, 1);
    return (fres == FR_OK);
}

FRESULT FileSystemFAT :: get_free (uint32_t *e, uint32_t *cs)
{
#if	FF_FS_MINIMIZE >= 1
	return FR_NOT_ENABLED;
#else
    FATFS *fs = 0;
    FRESULT fres = f_getfree(prefix, e, &fs);
    if (fs) {
        *cs = fs->ssize * fs->csize;
    }
    return fres;
#endif
}

bool    FileSystemFAT :: is_writable()
{
#if FF_FS_READONLY
	return false;
#else
	DSTATUS sta = prt->status();
	return (sta == 0); // not writable when there is no disk, not initialized or protected. All flags should be 0
#endif
}

FRESULT FileSystemFAT :: sync(void)
{
	return FR_OK;
    // f_sync();
}

// functions for reading directories
FRESULT FileSystemFAT :: dir_open(const TCHAR *path, Directory **dirout)
{
#if	FF_FS_MINIMIZE >= 2
	return FR_NOT_ENABLED;
#else
	*dirout = 0;

	DirectoryFAT *dir = new DirectoryFAT(this);
	FRESULT res;
	if (!path) {
	    path = "";
	}

    mstring prefixedPath(prefix);
    prefixedPath += path;

    res = f_opendir(dir->getDIR(), prefixedPath.c_str());

	if (res == FR_OK) {
		*dirout = dir;
	} else {
		delete dir;
	}
	return res;
#endif
}

FRESULT FileSystemFAT :: dir_create(const TCHAR *path)
{
#if	FF_FS_MINIMIZE >= 1
	return FR_NOT_ENABLED;
#else
    mstring prefixedPath(prefix);
    prefixedPath += path;
    return f_mkdir(prefixedPath.c_str());
#endif
}


FRESULT FileSystemFAT :: format(const char *name)
{
#if FF_USE_MKFS
    MKFS_PARM param;
    param.fmt = FM_SFD | FM_ANY;
    param.n_fat = 1;
    param.n_root = 0;
    param.au_size = 0; //0x1000;
    param.align = 0; // for 4K use 8

    uint8_t *buffer = new uint8_t[4096];
    FRESULT fres = f_mkfs(prefix, &param, buffer, 4096);
    return fres;
#else
    return FR_NOT_ENABLED;
#endif
}

FRESULT FileSystemFAT :: file_open(const char *filename, uint8_t flags, File **file)
{
	*file = 0;

    mstring prefixedFilename(prefix);
    prefixedFilename += filename;

    FileFAT *fatfile = new FileFAT(this);
	FIL *fil = fatfile->getFIL();

	FRESULT res = f_open(fil, prefixedFilename.c_str(), flags);
	if (res == FR_OK) {
	    *file = fatfile;
	    return res;
	}
	delete fatfile;
	return res;
}

FRESULT FileSystemFAT :: file_rename(const TCHAR *path, const char *new_name)
{
#if	FF_FS_MINIMIZE >= 1
    return FR_NOT_ENABLED;
#else
	mstring prefixedPath(prefix);
    prefixedPath += path;
    return f_rename(prefixedPath.c_str(), new_name);
#endif
}

FRESULT FileSystemFAT :: file_delete(const TCHAR *path)
{
#if	FF_FS_MINIMIZE >= 1
	return FR_NOT_ENABLED;
#else
    mstring prefixedPath(prefix);
    prefixedPath += path;
    return f_unlink(prefixedPath.c_str());
#endif
}

FRESULT FileFAT :: read(void *buffer, uint32_t len, uint32_t *transferred)
{
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_read(getFIL(), buffer, len, (UINT*)transferred);
}

FRESULT FileFAT :: write(const void *buffer, uint32_t len, uint32_t *transferred)
{
#if FF_FS_READONLY
	return FR_NOT_ENABLED;
#else
	if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_write(getFIL(), buffer, len, (UINT*)transferred);
#endif
}

FRESULT FileFAT :: seek(uint32_t pos)
{
#if	FF_FS_MINIMIZE >= 3
	return FR_NOT_ENABLED;
#else
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_lseek(getFIL(), pos);
#endif
}

FRESULT FileFAT :: sync(void)
{
#if FF_FS_READONLY
	return FR_NOT_ENABLED;
#else
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_sync(getFIL());
#endif
}

FRESULT FileFAT :: close(void)
{
    FRESULT res = f_close(getFIL());
    delete this;
    return res;
}

uint32_t FileFAT :: get_size(void)
{
    return getFIL()->obj.objsize;
}

uint32_t FileFAT :: get_inode(void)
{
    return getFIL()->obj.sclust;
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

    if(prt->get_type() == 1) { // already preset to FAT12
        return new FileSystemFAT(prt);
    }

    // Just try; use the f_mount function to see if it succeeds
    FileSystemFAT *fs = new FileSystemFAT(prt);
    if (fs->init()) {
        return fs;
    }
    delete fs;
    return NULL;
}

FactoryRegistrator<Partition *, FileSystem *> fat_tester(FileSystem :: getFileSystemFactory(), FileSystemFAT :: test);

uint32_t get_fattime (void) __attribute__((weak));
uint32_t get_fattime (void)     /* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
                                                    /* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
/*
    35 << 25 = 0x46000000
     9 << 21 = 0x01200000
     1 << 16 = 0x00010000
     9 << 11 = 0x00004800
    36 <<  5 = 0x00000480
    23 <<  0 = 0x00000017
*/
    return 0x47244C97; // 0100011 1001 00100 01001 100100 10111 => 2015-09-04 09:36:46
}
