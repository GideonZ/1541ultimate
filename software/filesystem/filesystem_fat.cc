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
FRESULT FileSystemFAT :: dir_open(const TCHAR *path, Directory **dirout, FileInfo *relativeDir)
{
	*dirout = 0;

	DirectoryFAT *dir = new DirectoryFAT(this);
	FRESULT res;
	if (!path) {
	    path = "";
	}

	if ((relativeDir) && (relativeDir->cluster)) {
        fatfs.cdir = relativeDir->cluster;
    } else {
        fatfs.cdir = 0;
    }
    res = fs_opendir(&fatfs, dir->getDIR(), path);

	if (res == FR_OK) {
		*dirout = dir;
	} else {
		delete dir;
	}
	return res;
}

FRESULT FileSystemFAT :: dir_create(const TCHAR *path)
{
    fatfs.cdir = 0;
    return fs_mkdir(&fatfs, path);
}


FRESULT FileSystemFAT :: format(const char *name)
{
    // call f_mkfs and f_setlabel
    return FR_NOT_ENABLED;
}

FRESULT FileSystemFAT :: file_open(const char *filename, uint8_t flags, File **file, FileInfo *relativeDir)
{
	*file = 0;
	if (relativeDir) {
	    fatfs.cdir = relativeDir->cluster;
	} else {
	    fatfs.cdir = 0;
	}

	FileFAT *fatfile = new FileFAT(this);
	FIL *fil = fatfile->getFIL();

	FRESULT res = fs_open(&fatfs, filename, flags, fil);
	if (res == FR_OK) {
	    *file = fatfile;
	    return res;
	}
	delete fatfile;
	return res;
}

FRESULT FileSystemFAT :: file_rename(const TCHAR *path, const char *new_name)
{
    fatfs.cdir = 0;
	return fs_rename(&fatfs, path, new_name);
}

FRESULT FileSystemFAT :: file_delete(const TCHAR *path)
{
    fatfs.cdir = 0;
    return fs_unlink(&fatfs, path);
}

FRESULT FileFAT :: read(void *buffer, uint32_t len, uint32_t *transferred)
{
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_read(getFIL(), buffer, len, transferred);
}

FRESULT FileFAT :: write(const void *buffer, uint32_t len, uint32_t *transferred)
{
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_write(getFIL(), buffer, len, transferred);
}

FRESULT FileFAT :: seek(uint32_t pos)
{
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_lseek(getFIL(), pos);
}

FRESULT FileFAT :: sync(void)
{
    if (!get_file_system()) {
        return FR_NOT_READY;
    }
    return f_sync(getFIL());
}

FRESULT FileFAT :: close(void)
{
    FRESULT res = f_close(getFIL());
    delete this;
    return res;
}

uint32_t FileFAT :: get_size(void)
{
    return getFIL()->fsize;
}

uint32_t FileFAT :: get_inode(void)
{
    return getFIL()->sclust;
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

    buf = new uint8_t[secsize];

    DRESULT dr = prt->read(buf, 0, 1);
    if (dr != RES_OK) {	/* Load boot record */
    	printf("FileSystemFAT::test failed, because reading sector failed: %d\n", dr);
		delete buf;
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

FactoryRegistrator<Partition *, FileSystem *> fat_tester(FileSystem :: getFileSystemFactory(), FileSystemFAT :: test);
