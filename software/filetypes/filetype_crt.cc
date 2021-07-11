/*
 * filetype_crt.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 2008-2011 Gideon Zweijtzer <info@1541ultimate.net>
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

#include "filetype_crt.h"
#include "filemanager.h"
#include "c64_crt.h"
#include "userinterface.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_crt(FileType :: getFileTypeFactory(), FileTypeCRT :: test_type);

FileTypeCRT::FileTypeCRT(BrowsableDirEntry *n)
{
    node = n;
    printf("Creating CRT type from info: %s\n", n->getName());
}

FileTypeCRT::~FileTypeCRT()
{
}

int FileTypeCRT::fetch_context_items(IndexedList<Action *> &list)
{
    C64 *machine = C64 :: getMachine();
    if (machine->exists()) {
        list.append(new Action("Run Cart", FileTypeCRT::execute_st, 0, (int) this));
        list.append(new Action("Copy to Flash", FileTypeCRT::executeFlash_st, 0, (int) this));
    }
    return 1;
}

// static member
FileType *FileTypeCRT::test_type(BrowsableDirEntry *obj)
{
    FileInfo *inf = obj->getInfo();
    if (strcmp(inf->extension, "CRT") == 0)
        return new FileTypeCRT(obj);
    return NULL;
}

// static member
int FileTypeCRT::execute_st(SubsysCommand *cmd)
{
    return ((FileTypeCRT *) cmd->mode)->execute(cmd);
}

int FileTypeCRT::executeFlash_st(SubsysCommand *cmd)
{
    return ((FileTypeCRT *) cmd->mode)->executeFlash(cmd);
}

// non-static member
int FileTypeCRT::execute(SubsysCommand *cmd)
{
    printf("Cartridge Load.. %s\n", cmd->filename.c_str());

    cart_def def;
    int retval = C64_CRT :: load_crt(cmd->path.c_str(), cmd->filename.c_str(), &def, C64 :: get_cartridge_rom_addr());

    if (retval == 0) {
        SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&def, "", "");
        c64_command->execute();
    } else {
        cmd->user_interface->popup(C64_CRT :: get_error_string(retval), BUTTON_OK);
    }
    return 0;
}

int FileTypeCRT::executeFlash(SubsysCommand *cmd)
{
    FileManager *fm = FileManager::getFileManager();

    fm->create_dir(CARTS_DIRECTORY); // just in case it doesn't exist
    char fnbuf[32];
    truncate_filename(cmd->filename.c_str(), fnbuf, 30);
    FRESULT fres = fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), CARTS_DIRECTORY, fnbuf, true);
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem::get_error_string(fres), BUTTON_OK);
        return 0;
    }
    C64 :: getMachine() -> set_rom_config(3, fnbuf);

    return 0;
}
