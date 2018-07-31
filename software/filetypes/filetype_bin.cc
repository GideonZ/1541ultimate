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
#include "ext_i2c.h"

#if (CLOCK_FREQ != 62500000) && (CLOCK_FREQ != 50000000)
#include "u64.h"
#endif

extern "C" {
    #include "dump_hex.h"
}

#define CMD_SET_KERNAL   0x4201
#define CMD_SET_DRIVEROM 0x4202
#define CMD_SET_CARTROM  0x4203
#define CMD_WRITE_EEPROM 0x4204
#define CMD_SET_BASIC    0x4205
#define CMD_SET_CHARSET  0x4206

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
    int size = node->getInfo()->size;

#if DEVELOPER == 2
    if (size == 256) {
        list.append(new Action("Write EEPROM", FileTypeBin :: execute_st, CMD_WRITE_EEPROM, (int)this));
        count++;
    }
#endif

    if (size == 4096) {
#if (CLOCK_FREQ != 62500000) && (CLOCK_FREQ != 50000000)
      list.append(new Action("Use as Charset ROM", FileTypeBin :: execute_st, CMD_SET_CHARSET, (int)this));
      count++;
#endif
    }
    else if (size == 8192) {
      list.append(new Action("Use as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
      count++;
#if (CLOCK_FREQ != 62500000) && (CLOCK_FREQ != 50000000)
      list.append(new Action("Use as BASIC ROM", FileTypeBin :: execute_st, CMD_SET_BASIC, (int)this));
      count++;
#endif
    }
    else if (size == 16384 || size == 32768) {
        list.append(new Action("Use as Drive ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM, (int)this));
        count++;
      }

    if(size == 65536 || size == 32768 || size == 16384 || size == 8192) {
      list.append(new Action("Use as Cartridge ROM", FileTypeBin :: execute_st, CMD_SET_CARTROM, (int)this));
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

    uint32_t size;
    uint8_t id;
    
    switch(cmd->functionID) {

    case CMD_WRITE_EEPROM:
        size = 256;
        break;

    case CMD_SET_DRIVEROM:
      size = 32768;
      id = FLASH_ID_CUSTOM_DRV;
      break;

    case CMD_SET_KERNAL:
      size = 8192;
      id = FLASH_ID_KERNAL_ROM;
      break;

    case CMD_SET_BASIC:
      size = 8192;
      id = FLASH_ID_BASIC_ROM;
      break;
      
    case CMD_SET_CHARSET:
      size = 4096;
      id = FLASH_ID_CHARGEN_ROM;
      break;

    case CMD_SET_CARTROM:
      size = node->getInfo()->size;
      id = FLASH_ID_CUSTOM_ROM;
    }  
    
    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

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

#if DEVELOPER == 2
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

        if (transferred == size) { // this should now match
    		int retval = get_flash()->write_image(id, buffer, size);
    		if (retval) {
    			printf("Flashing Kernal or drive ROM Failed: %d\n", retval);
    			cmd->user_interface->popup("Flashing Failed", BUTTON_OK);
    		} else {
    			if (id == FLASH_ID_KERNAL_ROM) {
					// Success!
					C64 :: enable_kernal(buffer);
                    cmd->user_interface->popup("Now using Alternate Kernal.", BUTTON_OK);

    			} else if (id == FLASH_ID_CUSTOM_DRV) {
        			cmd->user_interface->popup("Now use Custom 1541 ROM.", BUTTON_OK);
    			}
                else if (id == FLASH_ID_CUSTOM_ROM) {
                    cmd->user_interface->popup("Now use appropriate Custom Cartridge.", BUTTON_OK);
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
