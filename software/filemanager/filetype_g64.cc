#include "filetype_g64.h"
#include "directory.h"
#include "c1541.h"
#include "filemanager.h"

/* Drives to mount on */
extern C1541 *c1541;

// tester instance
FileTypeG64 tester_g64(file_type_factory);

/*********************************************************************/
/* G64 File Browser Handling                                         */
/*********************************************************************/
#define G64FILE_MOUNT    0x2111
#define G64FILE_MOUNT_RO 0x2112
#define G64FILE_MOUNT_UL 0x2113

FileTypeG64 :: FileTypeG64(FileTypeFactory &fac) : FileDirEntry(NULL, NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypeG64 :: FileTypeG64(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating g64 type from info: %s\n", fi->lfname);
}

FileTypeG64 :: ~FileTypeG64()
{
}

int FileTypeG64 :: fetch_children()
{
	return -1;
}

int FileTypeG64 :: fetch_context_items(IndexedList<PathObject *> &list)
{
	printf("G64 context...\n");
	list.append(new MenuItem(this, "Mount Disk", G64FILE_MOUNT));
    list.append(new MenuItem(this, "Mount Disk Read Only", G64FILE_MOUNT_RO));
    list.append(new MenuItem(this, "Mount Disk Unlinked", G64FILE_MOUNT_UL));

    return 3 + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeG64 :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "G64")==0)
        return new FileTypeG64(obj->parent, inf);
    return NULL;
}

void FileTypeG64 :: execute(int selection)
{
    bool protect;
    File *file;
    BYTE flags;

	switch(selection) {
	case G64FILE_MOUNT_UL:
		protect = false;
		flags = FA_READ;
		break;
	case G64FILE_MOUNT_RO:
		protect = true;
		flags = FA_READ;
		break;
	case G64FILE_MOUNT:
        protect = (info->attrib & AM_RDO);
        flags = (protect)?FA_READ:(FA_READ | FA_WRITE);
	default:
		break;
	}

	switch(selection) {
	case G64FILE_MOUNT:
	case G64FILE_MOUNT_RO:
	case G64FILE_MOUNT_UL:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
//		file = info->fs->file_open(info, flags);
		if(file) {
            push_event(e_unfreeze);
			push_event(e_mount_drv1_gcr, file, protect);
			if(selection != G64FILE_MOUNT) {
				push_event(e_unlink_drv1);
			}
		} else {
			printf("Error opening file.\n");
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}
