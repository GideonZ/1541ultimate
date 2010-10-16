#include "filetype_g64.h"
#include "directory.h"
#include "c1541.h"
#include "filemanager.h"

/* Drives to mount on */
extern C1541 *c1541_A;
extern C1541 *c1541_B;

// tester instance
FileTypeG64 tester_g64(file_type_factory);

/*********************************************************************/
/* G64 File Browser Handling                                         */
/*********************************************************************/
#define G64FILE_MOUNT      0x2111
#define G64FILE_MOUNT_RO   0x2112
#define G64FILE_MOUNT_UL   0x2113
#define G64FILE_MOUNT_B    0x2121
#define G64FILE_MOUNT_RO_B 0x2122
#define G64FILE_MOUNT_UL_B 0x2123

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
    int count = 0;
    if(CAPABILITIES & CAPAB_DRIVE_1541_1) {
        list.append(new MenuItem(this, "Mount Disk", G64FILE_MOUNT));
        list.append(new MenuItem(this, "Mount Disk Read Only", G64FILE_MOUNT_RO));
        list.append(new MenuItem(this, "Mount Disk Unlinked", G64FILE_MOUNT_UL));
        count += 3;
    }
    
    if(CAPABILITIES & CAPAB_DRIVE_1541_2) {
        list.append(new MenuItem(this, "Mount Disk on Drive B", G64FILE_MOUNT_B));
        list.append(new MenuItem(this, "Mount Disk R/O on Dr.B", G64FILE_MOUNT_RO_B));
        list.append(new MenuItem(this, "Mount Disk Unlnkd Dr.B", G64FILE_MOUNT_UL_B));
        count += 3;
    }

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
    t_drive_command *drive_command;

	switch(selection) {
	case G64FILE_MOUNT_UL:
	case G64FILE_MOUNT_UL_B:
		protect = false;
		flags = FA_READ;
		break;
	case G64FILE_MOUNT_RO:
	case G64FILE_MOUNT_RO_B:
		protect = true;
		flags = FA_READ;
		break;
	case G64FILE_MOUNT:
	case G64FILE_MOUNT_B:
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
		if(file) {
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT_GCR;
            push_event(e_unfreeze);
			push_event(e_object_private_cmd, c1541_A, (int)drive_command);
			if(selection != G64FILE_MOUNT) {
                drive_command = new t_drive_command;
                drive_command->command = MENU_1541_UNLINK;
    			push_event(e_object_private_cmd, c1541_A, (int)drive_command);
			}
		} else {
			printf("Error opening file.\n");
		}
		break;
	case G64FILE_MOUNT_B:
	case G64FILE_MOUNT_RO_B:
	case G64FILE_MOUNT_UL_B:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT_GCR;
            push_event(e_unfreeze);
			push_event(e_object_private_cmd, c1541_A, (int)drive_command);
			if(selection != G64FILE_MOUNT) {
                drive_command = new t_drive_command;
                drive_command->command = MENU_1541_UNLINK;
    			push_event(e_object_private_cmd, c1541_A, (int)drive_command);
			}
		} else {
			printf("Error opening file.\n");
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}
