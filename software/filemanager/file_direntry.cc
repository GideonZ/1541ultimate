#include <stdio.h>
#include "filemanager.h"
#include "file_system.h"
#include "directory.h"
#include "file_direntry.h"
#include "filetypes.h"
#include "../components/size_str.h"


FileDirEntry :: FileDirEntry(CachedTreeNode *par, char *name) : CachedTreeNode(par, name)
{
    discovered_type = NULL;
}
   

FileDirEntry :: FileDirEntry(CachedTreeNode *par, FileInfo *i) : CachedTreeNode(par, *i)
{
    discovered_type = NULL;
}

FileDirEntry :: ~FileDirEntry()
{
	if(discovered_type)
		delete discovered_type;
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

    if(info.is_directory()) {
    	printf("Opening dir %s.\n", info.lfname);
    	return fetch_directory(info);
    } else {
    	if (!discovered_type) {
    		discovered_type = file_type_factory.create(this);
    	}
    	if (discovered_type) {
    		if((fs = discovered_type->getFileSystem()) != NULL) {
    			info.attrib |= AM_HASCHILDREN;
    			printf("Opening filesystem in %s.\n", info.lfname);
    		    FileInfo fi(16);
    		    fi.attrib = AM_DIR;
    		    fi.fs = fs;
    		    return fetch_directory(fi);
    		}
    	}
    }
	return -1;
}

int FileDirEntry :: fetch_directory(FileInfo &info)
{
    FileInfo fi(64);
	FRESULT fres;

    Directory *r = info.fs->dir_open(&info);
    printf("Directory = %p\n", r);
	if(!r) {
		printf("Error opening directory! Error: %s\n", info.fs->get_error_string(info.fs->get_last_error()));
		return -1;
	}
    int i=0;
    while((fres = r->get_entry(fi)) == FR_OK) {
        printf(".");
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

char *FileDirEntry :: get_name()
{
	if(info.lfname)
		return info.lfname;
    return CachedTreeNode :: get_name();
}

char *FileDirEntry :: get_display_string()
{
    static char buffer[48];
    static char sizebuf[8];
    
    if (info.attrib & AM_VOL) {
        sprintf(buffer, "\x1B[0;7m%29s\x1B[0;32m VOLUME", info.lfname);
    } else if(info.is_directory()) {
        sprintf(buffer, "\x1B[0m%29s\x1B[35m DIR", info.lfname);
    } else {
        size_to_string_bytes(info.size, sizebuf);
        sprintf(buffer, "\x1B[0m%29s\x1B[33m %3s %s", info.lfname, info.extension, sizebuf);
    }
    
    return buffer;
}


int FileDirEntry :: compare(CachedTreeNode *obj)
{
	FileDirEntry *b = (FileDirEntry *)obj;

	int groupA = (info.attrib & AM_VOL) ? 2 : (info.attrib & AM_DIR) ? 1 : 0;
	int groupB = (b->info.attrib & AM_VOL) ? 2 : (b->info.attrib & AM_DIR) ? 1 : 0;

	if (groupA != groupB)
		return groupB - groupA;

    int by_name = stricmp(get_name(), b->get_name());
    if(by_name)
	    return by_name;
	return 0;
}
