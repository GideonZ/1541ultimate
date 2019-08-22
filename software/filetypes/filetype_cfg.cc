/*
 * filetype_bin.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2016 Gideon Zweijtzer <info@1541ultimate.net>
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

#include "filetype_cfg.h"
#include "filemanager.h"
#include "configio.h"
#include "menu.h"
#include "subsys.h"
#include "userinterface.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_cfg(FileType :: getFileTypeFactory(), FileTypeCfg :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


FileTypeCfg :: FileTypeCfg(BrowsableDirEntry *node)
{
	this->node = node; // link
}

FileTypeCfg :: ~FileTypeCfg()
{

}

int FileTypeCfg :: fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Load Settings", FileTypeCfg :: execute_st, 0, (int)this));
    return 1;
}

FileType *FileTypeCfg :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "CFG")==0)
        return new FileTypeCfg(obj);
    return NULL;
}

// static member
int FileTypeCfg :: execute_st(SubsysCommand *cmd)
{
	return ((FileTypeCfg *)cmd->mode)->execute(cmd);
}

// non-static member
int FileTypeCfg :: execute(SubsysCommand *cmd)
{
	File *file = 0;

    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    if(file) {
        bool ok = ConfigIO :: S_read_from_file(file);
        fm->fclose(file);
        if (ok) {
            cmd->user_interface->popup("Loading configuration successful!", BUTTON_OK);
        } else {
            cmd->user_interface->popup("There were errors.", BUTTON_OK);
        }
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
        return -2;
    }
    return 0;
}
