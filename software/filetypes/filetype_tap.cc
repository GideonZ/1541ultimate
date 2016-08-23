/*
 * filetype_tap.cc
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

#include "filetype_tap.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"
#include "dump_hex.h"
#include "tape_controller.h"

__inline uint32_t le_to_cpu_32(uint32_t a)
{
#ifdef NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_tap(FileType :: getFileTypeFactory(), FileTypeTap :: test_type);

#define TAPFILE_RUN 0x3101
#define TAPFILE_START 0x3110
#define TAPFILE_WRITE 0x3111
#define TAPFILE_WRITE2 0x3112

/*************************************************************/
/* Tap File Browser Handling                                 */
/*************************************************************/


FileTypeTap :: FileTypeTap(BrowsableDirEntry *node)
{
	this->node = node;
	printf("Creating Tap type from: %s\n", node->getName());
}

FileTypeTap :: ~FileTypeTap()
{
    printf("Destructor of FileTypeTap.\n");
}

int FileTypeTap :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_C2N_STREAMER) {
        list.append(new Action("Run Tape", FileTypeTap :: execute_st, TAPFILE_RUN, 0 ));
        count++;
        list.append(new Action("Start Tape", FileTypeTap :: execute_st, TAPFILE_START, 0 ));
        count++;
        list.append(new Action("Write to Tape", FileTypeTap :: execute_st, TAPFILE_WRITE, 0 ));
        count++;
//        list.append(new Action("Alt. Write", FileTypeTap :: execute_st, TAPFILE_WRITE2, 0 ));
//        count++;
    }
    return count;
}

FileType *FileTypeTap :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "TAP")==0)
        return new FileTypeTap(obj);
    return NULL;
}

int FileTypeTap :: execute_st(SubsysCommand *cmd)
{
	FRESULT fres;
	uint8_t read_buf[20];
	char *signature = "C64-TAPE-RAW";
	uint32_t *pul;
	uint32_t bytes_read;
	SubsysCommand *c64_command;
	
	tape_controller->stop();
	tape_controller->close();

	File *file = 0;
	fres = FileManager :: getFileManager() -> fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
	if(!file) {
		cmd->user_interface->popup("Can't open TAP file.", BUTTON_OK);
		return -1;
	}
	fres = file->read(read_buf, 20, &bytes_read);
	if(fres != FR_OK) {
		cmd->user_interface->popup("Error reading TAP file header.", BUTTON_OK);
		return -2;
	}
	if((bytes_read != 20) || (memcmp(read_buf, signature, 12))) {
		cmd->user_interface->popup("TAP file: invalid signature.", BUTTON_OK);
		return -3;
	}
	pul = (uint32_t *)&read_buf[16];
	tape_controller->set_file(file, le_to_cpu_32(*pul), int(read_buf[12]));
	file = NULL; // after set file, the tape controller is now owner of the File object :)

	switch(cmd->functionID) {
	case TAPFILE_START:
		if(cmd->user_interface->popup("Tape emulation starts now..", BUTTON_OK | BUTTON_CANCEL) == BUTTON_OK) {
			tape_controller->start(1);
		}
		break;
	case TAPFILE_RUN:
//		c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&sid_cart, "", "");
//      c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD, run_code, cmd->path.c_str(), cmd->filename.c_str());
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_LOAD_RUN, "", "");
        c64_command->execute();
		tape_controller->start(1);
		break;
    case TAPFILE_WRITE:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_RECORD, "", "");
        c64_command->execute();
		tape_controller->start(2);
		break;
    case TAPFILE_WRITE2:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_RECORD, "", "");
        c64_command->execute();
		tape_controller->start(3);
		break;
    default:
		break;
	}
	return 0;
}
