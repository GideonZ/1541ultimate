#include "filetype_iso.h"
#include "iso9660.h"
#include "directory.h"
#include "filemanager.h"

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FileTypeISO tester_iso(file_type_factory);

/*********************************************************************/
/* ISO File Browser Handling                                         */
/*********************************************************************/

FileTypeISO :: FileTypeISO(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    fs = NULL;
}

FileTypeISO :: FileTypeISO(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating ISO type from info: %s\n", fi->lfname);
    // we'll create a file-mapped filesystem here and attach the ISO file system
    blk = new BlockDevice_File(this, 2048);
    if(blk->status()) {
        printf("Can't open file.\n");
    } else {
        DWORD sec_count;
        DRESULT dres = blk->ioctl(GET_SECTOR_COUNT, &sec_count);
        printf("Sector count: %d.\n", sec_count);
        prt = new Partition(blk, 0, sec_count, 0);
        fs = new FileSystem_ISO9660(prt);
    }
    if(fs) {
        if(!fs->init()) {
            delete fs;
			delete prt;
			delete blk;
            fs = NULL;
        }
    }
}

FileTypeISO :: ~FileTypeISO()
{
	if(info) {
		if(fs) {
			delete fs;
			delete prt;
			delete blk;
			fs = NULL; // invalidate object
		}
	}
}

int FileTypeISO :: fetch_children()
{
    printf("Fetch ISO children. %p\n", this);
    if(!fs)
        return -1;
        
    cleanup_children();

    // now getting the root directory
    Directory *r = fs->dir_open(NULL);
    FileInfo fi(32);    

    int i=0;        
    while(r->get_entry(fi) == FR_OK) {
        children.append(new FileDirEntry(this, &fi));
        ++i;
    }
    fs->dir_close(r);
    sort_children();
    remove_duplicates();
    return i;
}

int   FileTypeISO :: fetch_context_items(IndexedList<PathObject *> &list)
{
    return FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeISO :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "ISO")==0)
        return new FileTypeISO(obj->parent, inf);
    return NULL;
}
