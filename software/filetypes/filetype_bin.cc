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
#include "ext_i2c.h"

#if U64
#include "u64.h"
#endif

extern "C" {
    #include "dump_hex.h"
}

#define CMD_SET_KERNAL   0x4201
#define CMD_SET_DRIVEROM 0x4202
#define CMD_SET_CARTROM  0x4203
#define CMD_WRITE_EEPROM 0x4204
#define CMD_SET_DRIVEROM2 0x4205
#define CMD_SET_DRIVEROM3 0x4206
#define CMD_TEST_CHARS   0x4222

#define CMD_SET_KERNAL_ORIG  0x4211
#define CMD_SET_KERNAL_ALT   0x4212
#define CMD_SET_BASIC_ORIG   0x4213
#define CMD_SET_BASIC_ALT    0x4214
#define CMD_SET_CHAR_ORIG    0x4215
#define CMD_SET_CHAR_ALT     0x4216
#define CMD_SET_KERNAL_ALT2  0x4217
#define CMD_SET_KERNAL_ALT3  0x4218
#define CMD_LOAD_KERNAL      0x4219
#define CMD_LOAD_DOS         0x421A

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
    if (size <= 4098) {
        list.append(new Action("Test Chars", FileTypeBin :: execute_st, CMD_TEST_CHARS, (int)this));
    }
#endif

#if U64
    if (size == 4096) {
        list.append(new Action("Flash as Orig. Char ROM", FileTypeBin :: execute_st, CMD_SET_CHAR_ORIG, (int)this));
        count++;
        list.append(new Action("Flash as Alt. Char ROM", FileTypeBin :: execute_st, CMD_SET_CHAR_ALT, (int)this));
        count++;
    }
    if (size == 8192) {
        list.append(new Action("Flash as Orig. Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL_ORIG, (int)this));
        count++;
        list.append(new Action("Flash as Alt. Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL_ALT, (int)this));
        count++;
        list.append(new Action("Flash as Alt. Kernal 2", FileTypeBin :: execute_st, CMD_SET_KERNAL_ALT2, (int)this));
        count++;
        list.append(new Action("Flash as Alt. Kernal 3", FileTypeBin :: execute_st, CMD_SET_KERNAL_ALT3, (int)this));
        count++;
        list.append(new Action("Flash as Orig. Basic ROM", FileTypeBin :: execute_st, CMD_SET_BASIC_ORIG, (int)this));
        count++;
        list.append(new Action("Flash as Alt. Basic ROM", FileTypeBin :: execute_st, CMD_SET_BASIC_ALT, (int)this));
        count++;
        list.append(new Action("Load Kernal", FileTypeBin :: load_kernal_st, CMD_LOAD_KERNAL, (int)this));
        count++;
    }
#elif CLOCK_FREQ == 62500000
    if (size == 8192) {
        list.append(new Action("Use as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
        count++;
        list.append(new Action("Use as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL_ALT2, (int)this));
        count++;
        list.append(new Action("Load Kernal", FileTypeBin :: load_kernal_st, CMD_LOAD_KERNAL, (int)this));
        count++;
    }
#else
    if (size == 8192) {
        list.append(new Action("Use as Kernal ROM", FileTypeBin :: execute_st, CMD_SET_KERNAL, (int)this));
        count++;
        list.append(new Action("Load Kernal", FileTypeBin :: load_kernal_st, CMD_LOAD_KERNAL, (int)this));
        count++;
    }
#endif

    if (size == 16384 || size == 32768) {
        list.append(new Action("Use as Drive ROM", FileTypeBin :: execute_st, CMD_SET_DRIVEROM, (int)this));
        count++;
        list.append(new Action("Load Drive ROM", FileTypeBin :: load_dos_st, CMD_LOAD_DOS, (int)this));
        count++;
#if U64
        list.append(new Action("Use as Drive ROM 2", FileTypeBin :: execute_st, CMD_SET_DRIVEROM2, (int)this));
        count++;
        list.append(new Action("Use as Drive ROM 3", FileTypeBin :: execute_st, CMD_SET_DRIVEROM3, (int)this));
        count++;
#elif CLOCK_FREQ == 62500000
        list.append(new Action("Use as Drive ROM 2", FileTypeBin :: execute_st, CMD_SET_DRIVEROM2, (int)this));
        count++;
#endif
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

    case CMD_SET_DRIVEROM2:
        size = 32768;
        id = FLASH_ID_CUSTOM2_DRV;
        break;

    case CMD_SET_DRIVEROM3:
        size = 32768;
        id = FLASH_ID_CUSTOM3_DRV;
        break;

    case CMD_SET_KERNAL:
    case CMD_SET_KERNAL_ALT:
        size = 8192;
        id = FLASH_ID_KERNAL_ROM;
        break;

    case CMD_SET_KERNAL_ORIG:
        size = 8192;
        id = FLASH_ID_ORIG_KERNAL;
        break;

    case CMD_SET_KERNAL_ALT2:
        size = 8192;
        id = FLASH_ID_KERNAL_ROM2;
        break;

    case CMD_SET_KERNAL_ALT3:
        size = 8192;
        id = FLASH_ID_KERNAL_ROM3;
        break;

    case CMD_SET_BASIC_ORIG:
        size = 8192;
        id = FLASH_ID_ORIG_BASIC;
        break;
      
    case CMD_SET_BASIC_ALT:
        size = 8192;
        id = FLASH_ID_BASIC_ROM;
        break;

    case CMD_SET_CHAR_ORIG:
        size = 4096;
        id = FLASH_ID_ORIG_CHARGEN;
        break;

    case CMD_SET_CHAR_ALT:
        size = 4096;
        id = FLASH_ID_CHARGEN_ROM;
        break;

    case CMD_SET_CARTROM:
        size = node->getInfo()->size;
        id = FLASH_ID_CUSTOM_ROM;
        break;

    case CMD_TEST_CHARS:
        size = node->getInfo()->size;

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
        if (cmd->functionID == CMD_TEST_CHARS) {
            uint8_t *src = buffer;
            src += (size & 0x7);
            size &= 0xFFF8;
            int remain = 4096;
            uint8_t *dest = (uint8_t *)U64_CHARROM_BASE;
            while (remain > 0) {
                int current = (remain < size) ? remain : size;
                memcpy(dest, src, current); // as simple as that
                dest += current;
                remain -= current;
            }
            delete buffer;
            return 0;
        }
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
            uint32_t *rom = (uint32_t *)buffer;
            bool ok = true;

            switch (cmd->functionID) {
            case CMD_SET_BASIC_ORIG:
            case CMD_SET_BASIC_ALT:
                if ((rom[1] != 0x424D4243) || (rom[2] != 0x43495341)) {
                    if (cmd->user_interface->popup("Are you sure this is a BASIC ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                        ok = false;
                    }
                }
                break;

            case CMD_SET_KERNAL_ORIG:
            case CMD_SET_KERNAL_ALT:
                if (rom[0] != 0x0F205685) {
                    if (cmd->user_interface->popup("Are you sure this is a KERNAL ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                        ok = false;
                    }
                }
                break;

            case CMD_SET_CHAR_ORIG:
            case CMD_SET_CHAR_ALT:
                if (rom[0] != 0x6E6E663C) {
                    if (cmd->user_interface->popup("Are you sure this is a CHAR ROM?", BUTTON_YES | BUTTON_NO) == BUTTON_NO) {
                        ok = false;
                    }
                }
                break;
            }

            if (ok) {
                int retval = get_flash()->write_image(id, buffer, size);
                if (retval) {
                    printf("Flashing Kernal or drive ROM Failed: %d\n", retval);
                    cmd->user_interface->popup("Flashing Failed", BUTTON_OK);
                } else {
                    switch (cmd->functionID) {
                    // For all U64 flash functions, also set the config accordingly
                    case CMD_SET_BASIC_ORIG:
                    case CMD_SET_BASIC_ALT:
                    case CMD_SET_KERNAL_ORIG:
                    case CMD_SET_KERNAL_ALT:
                    case CMD_SET_KERNAL_ALT2:
                    case CMD_SET_KERNAL_ALT3:
                    case CMD_SET_CHAR_ORIG:
                    case CMD_SET_CHAR_ALT:
                        C64 :: getMachine()->new_system_rom(id);
                        cmd->user_interface->popup("System ROM Flashed.", BUTTON_OK);
                        break;

                    case CMD_SET_CARTROM:
                        cmd->user_interface->popup("Now select appropriate Custom Cart", BUTTON_OK);
                        break;

                    case CMD_SET_DRIVEROM:
                        cmd->user_interface->popup("Please select Custom 1541 ROM.", BUTTON_OK);
                        break;
                        
                    case CMD_SET_DRIVEROM2:
                        cmd->user_interface->popup("Please select Custom 1541 ROM 2.", BUTTON_OK);
                        break;

                    case CMD_SET_DRIVEROM3:
                        cmd->user_interface->popup("Please select Custom 1541 ROM 3.", BUTTON_OK);
                        break;

                    // For U2/U2+ flash function, tell user that alternate kernal is being used.
                    case CMD_SET_KERNAL:
                        C64 :: getMachine()->enable_kernal(buffer);
                        cmd->user_interface->popup("Now using Alternate Kernal.", BUTTON_OK);
                        break;
                    }
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

int FileTypeBin :: load_kernal_st(SubsysCommand *cmd)
{
	return ((FileTypeBin *)cmd->mode)->load_kernal(cmd);
}

int FileTypeBin :: load_kernal(SubsysCommand *cmd)
{
    File *file = 0;

    FileManager *fm = FileManager :: getFileManager();

    uint32_t size = 8192;

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    if(file) {
    	uint8_t *buffer = new uint8_t[size];
    	uint32_t transferred = 0;
    	file->read(buffer, size, &transferred);
        fm->fclose(file);

        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_C64, C64_SET_KERNAL, 0, buffer, size);
        c64_command->execute();
        c64_command = new SubsysCommand(NULL, SUBSYSID_C64, MENU_C64_RESET, 0, 0, 0);
        c64_command->execute();
        
        delete[] buffer;
   }
}

int FileTypeBin :: load_dos_st(SubsysCommand *cmd)
{
	return ((FileTypeBin *)cmd->mode)->load_dos(cmd);
}

int FileTypeBin :: load_dos(SubsysCommand *cmd)
{
    File *file = 0;

    FileManager *fm = FileManager :: getFileManager();

    uint32_t size = 32768;

    printf("Binary Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    if(file) {
    	uint8_t *buffer = new uint8_t[size];
    	uint32_t transferred = 0;
    	file->read(buffer, size, &transferred);
        fm->fclose(file);

    	if ((size == 32768) && (transferred == 16384)) {
    		memcpy(buffer + 16384, buffer, 16384);
    		transferred *= 2;
    	}

        SubsysCommand *c64_command = new SubsysCommand(NULL, SUBSYSID_DRIVE_A, FLOPPY_LOAD_DOS, 0, buffer, size);
        c64_command->execute();

        c64_command = new SubsysCommand(NULL, SUBSYSID_DRIVE_A, MENU_1541_RESET, 0, 0, 0);
        c64_command->execute();
         
        
        delete[] buffer;
   }
}