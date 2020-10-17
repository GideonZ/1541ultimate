/*
 * fat_filesystem.h
 *
 *  Created on: Aug 23, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FAT_FILESYSTEM_H_
#define FILESYSTEM_FAT_FILESYSTEM_H_

#include "file_system.h"
#include "ff2.h"

class DirectoryFAT : public Directory
{
	DIR *fatdir;
	FileSystem *fs;

	void copy_info(FILINFO *fi, FileInfo *inf)
	{
		inf->fs = fs;
		inf->attrib = fi->fattrib;
		inf->size = fi->fsize;
		inf->date = fi->fdate;
		inf->time = fi->ftime;

	#if _USE_LFN
		get_extension(fi->fname, inf->extension);
		inf->cluster = fi->fclust;

		if(!(*inf->lfname)) {
			strncpy(inf->lfname, fi->fname, inf->lfsize);
		}
	#else
		strncpy(inf->lfname, fi->fname, inf->lfsize);
	#endif
	}

public:
	DirectoryFAT(FileSystem *fs) {
		this->fs = fs;
		fatdir = new DIR;
	}
	~DirectoryFAT() {
		f_closedir(fatdir);
		delete fatdir;
    }

    DIR *getDIR(void) { return fatdir; }

    FRESULT get_entry(FileInfo &out) {
    	FILINFO inf;
    #if _USE_LFN
    	inf.lfname = out.lfname;
    	inf.lfsize = out.lfsize;
    #endif
    	FRESULT res = f_readdir(fatdir, &inf);
    	if (fatdir->sect == 0)
    		return FR_NO_FILE;
    	copy_info(&inf, &out);
    	return res;
    }
};

class FileSystemFAT : public FileSystem
{
	FATFS fatfs;
	void copy_info(FILINFO *fi, FileInfo *inf);
public:
    FileSystemFAT(Partition *p);
    virtual ~FileSystemFAT();

    static FileSystem *test (Partition *prt);

    bool    init(void);              // Initialize file system
    FRESULT format(const char *name);// Format!
    FRESULT get_free (uint32_t *e);  // Get number of free sectors on the file system
    bool    is_writable();           // by default a file system is not writable, unless we implement it
    FRESULT sync(void); 			 // by default we can't write, and syncing is thus always successful

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *relativeDir = 0); // Opens directory (creates dir object)
    void    dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    FRESULT dir_create(const char *path);  // Creates a directory

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **, FileInfo *relativeDir = 0);  // Opens file (creates file object)
    FRESULT file_rename(const char *old_name, const char *new_name);  // Renames a file
    FRESULT file_delete(const char *path); // deletes a file

	void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);
    FRESULT file_sync(File *f);             // Clean-up cached data

    uint32_t get_file_size(File *f);
    uint32_t get_inode(File *f);
    bool     needs_sorting() { return true; }
};



#endif /* FILESYSTEM_FAT_FILESYSTEM_H_ */
