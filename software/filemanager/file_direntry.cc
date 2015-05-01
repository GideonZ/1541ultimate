#include <stdio.h>
#include "filemanager.h"
#include "file_system.h"
#include "directory.h"
#include "file_direntry.h"
#include "size_str.h"


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

    FileInfo fi(32);    
	FRESULT fres;
	
    if(info.is_directory()) {
        printf("Opening dir %s.\n", info.lfname);
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
    } else {
    	return -1;
/*
        // Filetype factory->transform 

    	FileDirEntry *promoted = attempt_promotion();
        if(promoted) {
            return promoted->fetch_children();
        } else {
            return -1;
        }
*/
    }
}

char *FileDirEntry :: get_name()
{
	if(info.lfname)
		return info.lfname;
    return CachedTreeNode :: get_name();
}

char *FileDirEntry :: get_display_string()
{
    static char buffer[44];
    static char sizebuf[8];
    
    if(info.is_directory()) {
        sprintf(buffer, "%29s\032 DIR", info.lfname);
    } else {
        size_to_string_bytes(info.size, sizebuf);
        sprintf(buffer, "%29s\027 %3s %s", info.lfname, info.extension, sizebuf);
    }
    
    return buffer;
}


int FileDirEntry :: compare(CachedTreeNode *obj)
{
	FileDirEntry *b = (FileDirEntry *)obj;

	if ((info.attrib & AM_DIR) && !(b->info.attrib & AM_DIR))
		return -1;
	if (!(info.attrib & AM_DIR) && (b->info.attrib & AM_DIR))
		return 1;

    int by_name = stricmp(get_name(), b->get_name());
    if(by_name)
	    return by_name;
	return 0;
}
