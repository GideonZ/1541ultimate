#include <stdio.h>
#include "filemanager.h"
#include "file_system.h"
#include "directory.h"
#include "file_direntry.h"
#include "size_str.h"
#include "globals.h"
#include "embedded_fs.h"

FileDirEntry :: FileDirEntry(CachedTreeNode *par, char *name) : CachedTreeNode(par, name)
{
}
   

FileDirEntry :: FileDirEntry(CachedTreeNode *par, FileInfo *i) : CachedTreeNode(par, *i)
{
}

FileDirEntry :: ~FileDirEntry()
{
}

bool FileDirEntry :: is_writable(void)
{
    if (!(info.fs)) {
        printf("No file system on %s. => not writable\n", get_name());
        return false;
    }
    return (info.is_directory() && info.fs->is_writable());
}

int FileDirEntry :: fetch_children(void)
{
    cleanup_children();
    FileSystem *fs;
    mstring work;

	FileSystemInFile *embedded_fs;

    if(info.is_directory()) {
    	// printf("Opening dir %s.\n", info.lfname);
    	return fetch_directory(info);
    } else {
		FileManager *fm = FileManager :: getFileManager();
		MountPoint *mp = fm->find_mount_point(get_full_path(work));
    	if (mp) {
    		embedded_fs = mp->get_embedded();
		    FileInfo fi(16);
		    fi.attrib = AM_DIR;
		    fi.fs = embedded_fs->getFileSystem();
		    if (!fi.fs) {
		    	printf("Internal error. FileSystem = 0.\n");
		    	return -1; // internal error!
		    }
		    return fetch_directory(fi);
    	} else {
    		embedded_fs = Globals :: getEmbeddedFileSystemFactory() -> create(this);
    	}
    	if (embedded_fs) {
			// printf("Opening filesystem in %s.\n", info.lfname);

    		File *file = fm -> fopen_node(this, FA_READ | FA_WRITE);
    		embedded_fs->init(file);

    		if((fs = embedded_fs->getFileSystem()) != NULL) {
    			fm -> add_mount_point(file, embedded_fs);
    			info.attrib |= AM_HASCHILDREN;
    		    FileInfo fi(16);
    		    fi.attrib = AM_DIR;
    		    fi.fs = fs;
    		    return fetch_directory(fi);
    		} else {
    			fm->fclose(file);
    			delete embedded_fs;
    			return -1;
    		}
    	}
    }
	return -1;
}


int FileDirEntry :: fetch_directory(FileInfo &info)
{
	// printf("Fetch directory...\n");

	FileInfo fi(64);
	FRESULT fres;

    Directory *r = info.fs->dir_open(&info);
	if(!r) {
		printf("Error opening directory! Error: %s\n", info.fs->get_error_string(info.fs->get_last_error()));
		return -1;
	}
    int i=0;
    while((fres = r->get_entry(fi)) == FR_OK) {
        // printf(".");
		if((fi.lfname[0] != '.') && !(fi.attrib & AM_HID)) {
            children.append(new FileDirEntry(this, &fi));
            ++i;
		}
    }
    //printf("close");
    info.fs->dir_close(r);
    //printf("sort");
    sort_children();
    return i;
}

bool FileDirEntry :: is_ready(void)
{
    return true;
}

const char *FileDirEntry :: get_name()
{
	if(info.lfname)
		return info.lfname;
    return CachedTreeNode :: get_name();
}

int FileDirEntry :: compare(CachedTreeNode *obj)
{
	FileDirEntry *b = (FileDirEntry *)obj;

	int groupA = (info.attrib & AM_VOL) ? 2 : (info.attrib & AM_DIR) ? 1 : 0;
	int groupB = (b->info.attrib & AM_VOL) ? 2 : (b->info.attrib & AM_DIR) ? 1 : 0;

	if (groupA != groupB)
		return groupB - groupA;

    int by_name = strcmp(get_name(), b->get_name()); // FIXME: Was stricmp
    if(by_name)
	    return by_name;
	return 0;
}
