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

#include "filetype_bin.h"
#include "filemanager.h"
#include "menu.h"
#include "c64.h"
#include "c1541.h"
#include "subsys.h"
#include "userinterface.h"

#if U64
#include "u64.h"
#endif

extern "C" {
    #include "dump_hex.h"
}

#define CMD_SET_BASIC    0x4200
#define CMD_SET_KERNAL   0x4201
#define CMD_SET_CHAR     0x4202
#define CMD_SET_DRIVEROM_41  0x4203
#define CMD_SET_DRIVEROM_71  0x4204
#define CMD_SET_DRIVEROM_81  0x4205

#define CMD_LOAD_KERNAL      0x4219
#define CMD_LOAD_DOS         0x421A
#define CMD_WRITE_EEPROM     0x4222

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_bin(FileType :: getFileTypeFactory(), FileTypeBin :: test_type);

/****************************************************************/
/* Binary File Browser Handling                                 */
/****************************************************************/


FileTypeBin :: FileTypeBin(BrowsableDirEntry *node)
{
	this->node = node; // link
}

FileTypeBin :: ~FileTypeBin()
{

}



int FileTypeBin :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    int size = node->getInfo()->size;

#if DEVELOPER == 2
    if (size == 256) {
        list.append(new Action("Write EEPROM", FileTypeBin :: execute_st, CMD_WRITE_EEPROM, (int)this));
        count++;
    }
#endif

#if U64
    if (size == 4096) {
        list.append(new Action("Set as Char ROM", FileTypeBin :: execute_st, CMD_SET_CHAR, (int)this));
        count++;
    }
    if (size == 8192) {
        list.append(new Action("Set as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
        count++;
        list.append(new Action("Set as Basic ROM", FileTypeBin :: execute_st, CMD_SET_BASIC, (int)this));
        count++;
        list.append(new Action("Load Kernal", FileTypeBin :: execute_st, CMD_LOAD_KERNAL, (int)this));
        count++;
    }
#else
    if (size == 8192) {
        list.append(new Action("Set as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
        count++;
        list.append(new Action("Load Kernal", FileTypeBin :: execute_st, CMD_LOAD_KERNAL, (int)this));
        count++;
    }
#endif

    if (c1541_A) {
        if ((size == 16384) || (size == 32768)) {
            list.append(new Action("Load Drive ROM", FileTypeBin :: execute_st, CMD_LOAD_DOS, (int)this));
            count++;
        }

        if (size == 16384) {
            list.append(new Action("Set as 1541 ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM_41, (int)this));
            count++;
        }
        if (size == 32768) {
            list.append(new Action("Set as 1541 ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM_41, (int)this));
            count++;
            list.append(new Action("Set as 1571 ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM_71, (int)this));
            count++;
            list.append(new Action("Set as 1581 ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM_81, (int)this));
            count++;
        }
    }
    return count;
}

FileType *FileTypeBin :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "BIN")==0)
        return new FileTypeBin(obj);
    if(strcmp(inf->extension, "ROM")==0)
        return new FileTypeBin(obj);
    if(strcmp(inf->extension, "64C")==0)
        return new FileTypeBin(obj);
    return NULL;
}

// static member
int FileTypeBin :: execute_st(SubsysCommand *cmd)
{
	return ((FileTypeBin *)cmd->mode)->execute(cmd);
}

// non-static member
int FileTypeBin :: execute(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    uint32_t rom[4];
    FRESULT fres = fm->load_file(cmd->path.c_str(), cmd->filename.c_str(), (uint8_t *)rom, 16, NULL);
    if (fres != FR_OK) {
        cmd->user_interface->popup("Unable to read file.", BUTTON_OK);
        return 0;
    }
    
    char fnbuf[36];
    truncate_filename(cmd->filename.c_str(), fnbuf, 32);

    bool ok = true;
    switch(cmd->functionID) {
    case CMD_SET_KERNAL:
        if (rom[0] != 0x0F205685) { // warning: will fail on big endian machine
            if (cmd->user_interface->popup("Are you sure this is a Kernal ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                ok = false;
            }
        }
        if (ok) {
            fm->create_dir(ROMS_DIRECTORY);
            fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), ROMS_DIRECTORY, fnbuf, true);
            C64 :: getMachine() ->set_rom_config(0, cmd->filename.c_str());
        }
        break;

    case CMD_SET_BASIC:
        if ((rom[1] != 0x424D4243) || (rom[2] != 0x43495341)) {
            if (cmd->user_interface->popup("Are you sure this is a BASIC ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                ok = false;
            }
        }
        if (ok) {
            fm->create_dir(ROMS_DIRECTORY);
            fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), ROMS_DIRECTORY, fnbuf, true);
            C64 :: getMachine() ->set_rom_config(1, cmd->filename.c_str());
        }
        break;

    case CMD_SET_CHAR:
        if (rom[0] != 0x6E6E663C) {
            if (cmd->user_interface->popup("Are you sure this is a CHAR ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                ok = false;
            }
        }
        if (ok) {
            fm->create_dir(ROMS_DIRECTORY);
            fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), ROMS_DIRECTORY, fnbuf, true);
            C64 :: getMachine() ->set_rom_config(2, cmd->filename.c_str());
        }
        break;

    case CMD_LOAD_KERNAL:
        return load_kernal(cmd);

    case CMD_LOAD_DOS:
        return load_dos(cmd);

    case CMD_SET_DRIVEROM_41:
    case CMD_SET_DRIVEROM_71:
    case CMD_SET_DRIVEROM_81:
        fm->create_dir(ROMS_DIRECTORY);
        fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), ROMS_DIRECTORY, fnbuf, true);
        if(c1541_A) {
            c1541_A->set_rom_config(cmd->functionID - CMD_SET_DRIVEROM_41, cmd->filename.c_str());
        }
        break;
    }  
    return 0;
}

int FileTypeBin :: load_kernal(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    uint32_t size = 8192;
    uint8_t *buffer = new uint8_t[size];

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->load_file(cmd->path.c_str(), cmd->filename.c_str(), buffer, size, NULL);

    if(fres == FR_OK) {
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_SET_KERNAL, 0, buffer, size);
        c64_command->execute();
        c64_command = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0, 0, 0);
        c64_command->execute();
    }
    delete[] buffer;
    return fres;
}

int FileTypeBin :: load_dos(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    uint32_t size = 32768;
    uint32_t transferred = 0;
    uint8_t *buffer = new uint8_t[size];

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->load_file(cmd->path.c_str(), cmd->filename.c_str(), buffer, size, &transferred);

    if(fres == FR_OK) {
    	if ((size == 32768) && (transferred == 16384)) {
    		memcpy(buffer + 16384, buffer, 16384);
    	}
        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_DRIVE_A, FLOPPY_LOAD_DOS, 0, buffer, size);
        c64_command->execute();
        c64_command = new SubsysCommand(NULL, SUBSYSID_DRIVE_A, MENU_1541_RESET, 0, 0, 0);
        c64_command->execute();
    }
    delete[] buffer;
    return fres;
}

#if 0
        if (cmd->functionID == CMD_WRITE_EEPROM) {
            for(int i=0;i<256;i++) {
                ext_i2c_write_byte(0xA0, i, buffer[i]);
                for(int j=0;j<10000;j++)
                    ;
            }
            cmd->user_interface->popup("EEPROM written", BUTTON_OK);
            delete buffer;
            return 0;
        }
#endif
