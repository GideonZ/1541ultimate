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
#include "subsys.h"
#include "userinterface.h"

extern "C" {
    #include "dump_hex.h"
}

#define CMD_SET_KERNAL 0x4201

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_bin(FileType :: getFileTypeFactory(), FileTypeBin :: test_type);

/*********************************************************************/
/* D64/D71/D81 File Browser Handling                                 */
/*********************************************************************/


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
    if (node->getInfo()->size == 8192) {
    	list.append(new Action("Use as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
    	count++;
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
	File *file = 0;
	FileInfo *inf;

    FileManager *fm = FileManager :: getFileManager();

    uint32_t mem_addr = ((uint32_t)C64_CARTRIDGE_RAM_BASE) << 16;

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    if(file) {
    	uint8_t *buffer = new uint8_t[8192];
    	uint32_t transferred = 0;
    	file->read(buffer, 8192, &transferred);
    	if (transferred == 8192) {
    		int retval = get_flash()->write_image(FLASH_ID_KERNAL_ROM, buffer, 8192);
    		if (retval) {
    			printf("Flashing Kernal Failed: %d\n", retval);
    			cmd->user_interface->popup("Flashing Kernal Failed", BUTTON_OK);
    		} else {
    			// Success!
    			C64 :: enable_kernal(buffer);
    		}
    	}
        fm->fclose(file);
        delete buffer;
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
        return -2;
    }
    return 0;
}
