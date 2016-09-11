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

#define CMD_SET_KERNAL   0x4201
#define CMD_SET_DRIVEROM 0x4202

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
    } else if ((node->getInfo()->size == 16384) || (node->getInfo()->size == 32768)) {
    	list.append(new Action("Use as drive ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM, (int)this));
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

    uint32_t size = (cmd->functionID == CMD_SET_DRIVEROM) ? 32768 : 8192;

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    uint8_t id = (cmd->functionID == CMD_SET_DRIVEROM) ? FLASH_ID_CUSTOM_DRV : FLASH_ID_KERNAL_ROM;

    if(file) {
    	uint8_t *buffer = new uint8_t[size];
    	uint32_t transferred = 0;
    	file->read(buffer, size, &transferred);
    	// If we read a 16K rom, we duplicate it to 32K, to simulate mirroring in the drive memory map
    	// This way, we don't need to record whether our image is 16K or 32K later on. It will always be 32K in the flash.
    	if ((size == 32768) && (transferred == 16384)) {
    		memcpy(buffer + 16384, buffer, 16384);
    		transferred *= 2;
    	}
        fm->fclose(file);

    	if (transferred == size) { // this should now match
    		int retval = get_flash()->write_image(id, buffer, size);
    		if (retval) {
    			printf("Flashing Kernal or drive ROM Failed: %d\n", retval);
    			cmd->user_interface->popup("Flashing Failed", BUTTON_OK);
    		} else {
    			if (id == FLASH_ID_KERNAL_ROM) {
					// Success!
					C64 :: enable_kernal(buffer);
    			} else {
        			cmd->user_interface->popup("Now set custom ROM in drive config.", BUTTON_OK);
    			}
    		}
    	}
        delete buffer;
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
        return -2;
    }
    return 0;
}
