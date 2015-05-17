/*
 * filetype_d64.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *  Copyright (C) 2011 Daniel Kahlin <daniel@kahlin.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "filetype_d64.h"
#include "directory.h"
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1541.h"

extern "C" {
    #include "dump_hex.h"
}

// tester instance
FactoryRegistrator<CachedTreeNode *, FileType *> tester_d64(file_type_factory, FileTypeD64 :: test_type);

extern C1541 *c1541_A;
extern C1541 *c1541_B;

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/
#define D64FILE_RUN        0x2101
#define D64FILE_MOUNT      0x2102
#define D64FILE_MOUNT_RO   0x2103
#define D64FILE_MOUNT_UL   0x2104
#define D64FILE_MOUNT_B    0x2111
#define D64FILE_MOUNT_RO_B 0x2112
#define D64FILE_MOUNT_UL_B 0x2113


FileTypeD64 :: FileTypeD64(CachedTreeNode *node)
{
	this->node = node; // link
	FileInfo *fi = node->get_file_info();
	printf("Creating d64 type from info: %s. Node = %p. This.FileInfo = %p. NodeName = %s\n",
			fi->lfname, node, node->get_file_info(), node->get_name());
    // we'll create a file-mapped block device and a default
    // partition to attach our file system to
    blk = new BlockDevice_File(node, 256);
    prt = new Partition(blk, 0, (fi->size)>>8, 0);
    fs  = new FileSystemD64(prt);
}

FileTypeD64 :: ~FileTypeD64()
{
	if(fs) {
		delete fs;
		delete prt;
		delete blk;
		fs = NULL; // invalidate object
	}
}


int FileTypeD64 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    DWORD capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_DRIVE_1541_1) {
/*
        list.append(new MenuItem(this, "Run Disk", D64FILE_RUN));
        list.append(new MenuItem(this, "Mount Disk", D64FILE_MOUNT));
        list.append(new MenuItem(this, "Mount Disk Read Only", D64FILE_MOUNT_RO));
        list.append(new MenuItem(this, "Mount Disk Unlinked", D64FILE_MOUNT_UL));
        count += 4;
*/
    }
    
    if(capabilities & CAPAB_DRIVE_1541_2) {
/*
        list.append(new MenuItem(this, "Mount Disk on Drive B", D64FILE_MOUNT_B));
        list.append(new MenuItem(this, "Mount Disk R/O on Dr.B", D64FILE_MOUNT_RO_B));
        list.append(new MenuItem(this, "Mount Disk Unlnkd Dr.B", D64FILE_MOUNT_UL_B));
        count += 3;
*/
    }

	list.append(new Action("Test Action", FileTypeD64 :: test_action, this, node));
    count++;
    
    return count;
}

FileType *FileTypeD64 :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "D64")==0)
        return new FileTypeD64(obj);
//    if(strcmp(inf->extension, "D71")==0)
//        return new FileTypeD64(NULL, inf);
//    if(strcmp(inf->extension, "D81")==0)
//        return new FileTypeD64(NULL, inf);
    return NULL;
}

void FileTypeD64 :: test_action(void *obj, void *prm)
{
	CachedTreeNode *node = (CachedTreeNode *)prm;
	printf("Test action for node %s\n", node->get_name());
}

/*
void FileTypeD64 :: execute(int selection)
{
    bool protect;
    File *file;
    BYTE flags;
    t_drive_command *drive_command;
    
	switch(selection) {
	case D64FILE_MOUNT_UL:
	case D64FILE_MOUNT_UL_B:
		protect = false;
		flags = FA_READ;
		break;
	case D64FILE_MOUNT_RO:
	case D64FILE_MOUNT_RO_B:
		protect = true;
		flags = FA_READ;
		break;
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case D64FILE_MOUNT_B:
        protect = (info->attrib & AM_RDO);
        flags = (protect)?FA_READ:(FA_READ | FA_WRITE);
	default:
		break;
	}

	switch(selection) {
	case D64FILE_RUN:
	case D64FILE_MOUNT:
	case D64FILE_MOUNT_RO:
	case D64FILE_MOUNT_UL:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            // unfortunately, we need to insert this here, because we have to make sure we still have
            // a user interface available, to ask if a save is needed.
            c1541_A->check_if_save_needed();
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT;
//			push_event(e_mount_drv1, file, protect);
			if(selection == D64FILE_RUN) {
                C64Event::prepare_dma_load(NULL, "*", 1, RUNCODE_MOUNT_LOAD_RUN);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                C64Event::perform_dma_load(NULL, RUNCODE_MOUNT_LOAD_RUN);

            } else {
                push_event(e_unfreeze);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                if(selection != D64FILE_MOUNT) {
                    drive_command = new t_drive_command;
                    drive_command->command = MENU_1541_UNLINK;
                    //				push_event(e_unlink_drv1);
                    push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                }
            }
		} else {
			printf("Error opening file.\n");
		}
		break;
	case D64FILE_MOUNT_B:
	case D64FILE_MOUNT_RO_B:
	case D64FILE_MOUNT_UL_B:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            // unfortunately, we need to insert this here, because we have to make sure we still have
            // a user interface available, to ask if a save is needed.
            c1541_B->check_if_save_needed();
            push_event(e_unfreeze);
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT;
//			push_event(e_mount_drv1, file, protect);
			push_event(e_object_private_cmd, c1541_B, (int)drive_command);

			if(selection != D64FILE_MOUNT_B) {
                drive_command = new t_drive_command;
                drive_command->command = MENU_1541_UNLINK;
//				push_event(e_unlink_drv1);
    			push_event(e_object_private_cmd, c1541_B, (int)drive_command);
			}
		} else {
			printf("Error opening file.\n");
		}
		break;
	default:
		FileDirEntry :: execute(selection);
    }
}

*/
