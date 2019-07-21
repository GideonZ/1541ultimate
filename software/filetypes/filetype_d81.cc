/*
 * filetype_d81.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *    Scott Hutter <scott.hutter@gmail.com>
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

#include "filetype_d81.h"
#include "directory.h"
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1581.h"
#include "subsys.h"

extern "C" {
    #include "dump_hex.h"
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_d81(FileType :: getFileTypeFactory(), FileTypeD81 :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


FileTypeD81 :: FileTypeD81(BrowsableDirEntry *node)
{
	this->node = node; // link
}

FileTypeD81 :: ~FileTypeD81()
{

}


int FileTypeD81 :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    
    //if(capabilities & CAPAB_DRIVE_1541_2) {
        list.append(new Action("Mount Disk on Soft IEC", SUBSYSID_DRIVE_C, D81FILE_MOUNT));
        count += 1;
    //}
    
    return count;
}

FileType *FileTypeD81 :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "D81")==0)
        return new FileTypeD81(obj);
    return NULL;
}
