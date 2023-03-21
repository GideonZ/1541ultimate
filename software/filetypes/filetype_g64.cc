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
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1541.h"
#include "subsys.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_g64(FileType :: getFileTypeFactory(), FileTypeG64 :: test_type);

/*************************************************************/
/* G64 File Browser Handling                                 */
/*************************************************************/

FileTypeG64 :: FileTypeG64(BrowsableDirEntry *node, int type)
{
	this->node = node; // link
	this->ftype = type;
}

FileTypeG64 :: ~FileTypeG64()
{

}


int FileTypeG64 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_DRIVE_1541_1) {
        C64 *machine = C64 :: getMachine();
        list.append(new Action("Mount Disk", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_G64, ftype));
    	if (machine->exists()) {
            list.append(new Action("Run Disk", runDisk_st, 0, ftype));
            count ++;
    	}
        list.append(new Action("Mount Disk Read Only", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_G64_RO, ftype));
        list.append(new Action("Mount Disk Unlinked", SUBSYSID_DRIVE_A, MENU_1541_MOUNT_G64_UL, ftype));
        count += 3;
    }

    if(capabilities & CAPAB_DRIVE_1541_2) {
        list.append(new Action("Mount Disk on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_G64, ftype));
        list.append(new Action("Mount Disk R/O on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_G64_RO, ftype));
        list.append(new Action("Mount Disk Unl. on B", SUBSYSID_DRIVE_B, MENU_1541_MOUNT_G64_UL, ftype));
        count += 3;
    }

    return count;
}

int FileTypeG64 :: runDisk_st(SubsysCommand *cmd)
{
    // First command is to mount the disk
    SubsysCommand *drvcmd = new SubsysCommand(cmd->user_interface, SUBSYSID_DRIVE_A, MENU_1541_MOUNT_G64, cmd->mode, cmd->path.c_str(), cmd->filename.c_str());
    drvcmd->execute();

    // Second command is to perform a load"*",8,1
    char *drvId = "H";
    drvId[0] = 0x40 + c1541_A->get_current_iec_address();
    SubsysCommand *c64cmd = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_MOUNT_LOAD_RUN, drvId, "*");
    c64cmd->execute();
    return 0;
}

FileType *FileTypeG64 :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "G64")==0)
        return new FileTypeG64(obj, 1541);
    if(strcmp(inf->extension, "G71")==0)
        return new FileTypeG64(obj, 1571);
    return NULL;
}
