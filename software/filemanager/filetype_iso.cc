#include "filetype_iso.h"
#include "iso9660.h"
#include "directory.h"
#include "filemanager.h"

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FactoryRegistrator<CachedTreeNode *, FileType *> tester_iso(file_type_factory, FileTypeISO :: test_type);

/*********************************************************************/
/* ISO File Browser Handling                                         */
/*********************************************************************/

FileTypeISO :: FileTypeISO(CachedTreeNode *filenode)
{
	fs = NULL;
	FileInfo *fi = filenode->get_file_info();

	printf("Creating ISO type from info: %s\n", fi->lfname);
    // we'll create a file-mapped filesystem here and attach the ISO file system
    blk = new BlockDevice_File(filenode, 2048);
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
	if(fs) {
		delete fs;
		delete prt;
		delete blk;
		fs = NULL; // invalidate object
	}
}

FileType *FileTypeISO :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "ISO")==0)
        return new FileTypeISO(obj);
    return NULL;
}
