/*
 * filetype_d64.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 2007-2019 Gideon Zweijtzer <info@1541ultimate.net>
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

#include "filetype_t64.h"
/*
#include "directory.h"
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1541.h"
#include "subsys.h"
*/


// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_t64(FileType :: getFileTypeFactory(), FileTypeT64 :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


FileTypeT64 :: FileTypeT64(BrowsableDirEntry *node)
{
	this->node = node; // link
}

FileTypeT64 :: ~FileTypeT64()
{

}


int FileTypeT64 :: fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Enter", UserFileInteraction::S_enter, 0));
    return 1;
}

FileType *FileTypeT64 :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "T64")==0)
        return new FileTypeT64(obj);
    return NULL;
}
