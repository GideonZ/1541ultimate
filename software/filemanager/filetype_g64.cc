/*
 * filetype_g64.cc
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

#include "filetype_g64.h"
#include "directory.h"
#include "c1541.h"
#include "c64.h"
#include "filemanager.h"

/* Drives to mount on */
extern C1541 *c1541_A;
extern C1541 *c1541_B;

// tester instance
FileTypeG64 tester_g64(file_type_factory);

/*********************************************************************/
/* G64 File Browser Handling                                         */
/*********************************************************************/
#define G64FILE_RUN        0x2121
#define G64FILE_MOUNT      0x2122
#define G64FILE_MOUNT_RO   0x2123
#define G64FILE_MOUNT_UL   0x2124
#define G64FILE_MOUNT_B    0x2131
#define G64FILE_MOUNT_RO_B 0x2132
#define G64FILE_MOUNT_UL_B 0x2133

FileTypeG64 :: FileTypeG64(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
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
        list.append(new MenuItem(this, "Run Disk", G64FILE_RUN));
        list.append(new MenuItem(this, "Mount Disk", G64FILE_MOUNT));
        list.append(new MenuItem(this, "Mount Disk Read Only", G64FILE_MOUNT_RO));
        list.append(new MenuItem(this, "Mount Disk Unlinked", G64FILE_MOUNT_UL));
        count += 4;
    }
    
    if(CAPABILITIES & CAPAB_DRIVE_1541_2) {
        list.append(new MenuItem(this, "Mount Disk on Drive B", G64FILE_MOUNT_B));
        list.append(new MenuItem(this, "Mount Disk R/O on Dr.B", G64FILE_MOUNT_RO_B));
        list.append(new MenuItem(this, "Mount Disk Unlnkd Dr.B", G64FILE_MOUNT_UL_B));
        count += 3;
    }

    return count + FileDirEntry :: fetch_context_items_actual(list);
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
	case G64FILE_RUN:
	case G64FILE_MOUNT:
	case G64FILE_MOUNT_B:
        protect = (info->attrib & AM_RDO);
        flags = (protect)?FA_READ:(FA_READ | FA_WRITE);
	default:
		break;
	}

	switch(selection) {
	case G64FILE_RUN:
	case G64FILE_MOUNT:
	case G64FILE_MOUNT_RO:
	case G64FILE_MOUNT_UL:
		printf("Mounting disk.. %s\n", get_name());
		file = root.fopen(this, flags);
		if(file) {
            c1541_A->check_if_save_needed();
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT_GCR;
			if(selection == G64FILE_RUN) {
                C64Event::prepare_dma_load(NULL, "*", 1, RUNCODE_MOUNT_LOAD_RUN);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                C64Event::perform_dma_load(NULL, RUNCODE_MOUNT_LOAD_RUN);

            } else {
                push_event(e_unfreeze);
                push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                if(selection != G64FILE_MOUNT) {
                    drive_command = new t_drive_command;
                    drive_command->command = MENU_1541_UNLINK;
                    push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                }
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
            c1541_B->check_if_save_needed();
            drive_command = new t_drive_command;
            drive_command->file = file;
            drive_command->protect = protect;
            drive_command->command = MENU_1541_MOUNT_GCR;
            push_event(e_unfreeze);
			push_event(e_object_private_cmd, c1541_B, (int)drive_command);
			if(selection != G64FILE_MOUNT_B) {
                drive_command = new t_drive_command;
                drive_command->command = MENU_1541_UNLINK;
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
