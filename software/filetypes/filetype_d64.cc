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
#include "subsys.h"

extern "C" {
    #include "dump_hex.h"
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_d64(Globals :: getFileTypeFactory(), FileTypeD64 :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


FileTypeD64 :: FileTypeD64(BrowsableDirEntry *node)
{
	this->node = node; // link
}

FileTypeD64 :: ~FileTypeD64()
{

}


int FileTypeD64 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_DRIVE_1541_1) {
        list.append(new Action("Run Disk", SUBSYSID_DRIVE_A, D64FILE_RUN));
        list.append(new Action("Mount Disk", SUBSYSID_DRIVE_A, D64FILE_MOUNT));
        list.append(new Action("Mount Disk Read Only", SUBSYSID_DRIVE_A, D64FILE_MOUNT_RO));
        list.append(new Action("Mount Disk Unlinked", SUBSYSID_DRIVE_A, D64FILE_MOUNT_UL));
        count += 4;
    }
    
    if(capabilities & CAPAB_DRIVE_1541_2) {
        list.append(new Action("Mount Disk on B", SUBSYSID_DRIVE_B, D64FILE_MOUNT));
        list.append(new Action("Mount Disk R/O on B", SUBSYSID_DRIVE_B, D64FILE_MOUNT_RO));
        list.append(new Action("Mount Disk Unl. on B", SUBSYSID_DRIVE_B, D64FILE_MOUNT_UL));
        count += 3;
    }
    
    return count;
}

FileType *FileTypeD64 :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "D64")==0)
        return new FileTypeD64(obj);
    return NULL;
}
