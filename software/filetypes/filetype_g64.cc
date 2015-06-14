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

// tester instance
FactoryRegistrator<CachedTreeNode *, FileType *> tester_g64(Globals :: getFileTypeFactory(), FileTypeG64 :: test_type);

extern C1541 *c1541_A;
extern C1541 *c1541_B;

/*********************************************************************/
/* G64 File Browser Handling                                         */
/*********************************************************************/
FileTypeG64 :: FileTypeG64(CachedTreeNode *node)
{
	this->node = node; // link
}

FileTypeG64 :: ~FileTypeG64()
{
}

int FileTypeG64 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_DRIVE_1541_1) {
		list.append(new DriveMenuItem("Run Disk", c1541_A, G64FILE_RUN, node));
		list.append(new DriveMenuItem("Mount Disk", c1541_A, G64FILE_MOUNT, node));
		list.append(new DriveMenuItem("Mount Disk Read Only", c1541_A, G64FILE_MOUNT_RO, node));
		list.append(new DriveMenuItem("Mount Disk Unlinked", c1541_A, G64FILE_MOUNT_UL, node));
		count += 4;
	}

	if(capabilities & CAPAB_DRIVE_1541_2) {
		list.append(new DriveMenuItem("Mount Disk on B", c1541_B, G64FILE_MOUNT, node));
		list.append(new DriveMenuItem("Mount Disk R/O on B", c1541_B, G64FILE_MOUNT_RO, node));
		list.append(new DriveMenuItem("Mount Disk Unl. on B", c1541_B, G64FILE_MOUNT_UL, node));
		count += 3;
	}

	return count;
}

FileType *FileTypeG64 :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "G64")==0)
        return new FileTypeG64(obj);
    return NULL;
}
