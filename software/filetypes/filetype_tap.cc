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
#include "endianness.h"
#include "pattern.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_tap(FileType :: getFileTypeFactory(), FileTypeTap :: test_type);

#define TAPFILE_RUN 0x3101
#define TAPFILE_START 0x3110
#define TAPFILE_WRITE 0x3111
#define TAPFILE_WRITE2 0x3112
#define TAPFILE_INDEX  0x3113

#define TAPFILE_RUN_PARENT   (0x8000 | TAPFILE_RUN)
#define TAPFILE_START_PARENT (0x8000 | TAPFILE_START)
#define TAPFILE_WRITE_PARENT (0x8000 | TAPFILE_WRITE)

/*************************************************************/
/* Tap File Browser Handling                                 */
/*************************************************************/


FileTypeTap :: FileTypeTap(BrowsableDirEntry *node) : tapIndices(4, NULL)
{
	this->node = node;
	printf("Creating Tap type from: %s\n", node->getName());
	indexValid = false;
}

FileTypeTap :: ~FileTypeTap()
{
    printf("Destructor of FileTypeTap.\n");
    if (tape_controller) {
        tape_controller->stop();
        tape_controller->close();
    }
}

void FileTypeTap :: readIndexFile(void)
{
    FileManager *fm = FileManager :: getFileManager();
    File *idxFile;

    char filename[80];
    strncpy(filename, node->getName(), 79);
    filename[79] = 0;

    set_extension(filename, ".idx", 80);

    indexValid = false;
    if (fm->fopen(node->getPath(), filename, FA_READ, &idxFile) == FR_OK) {
        parseIndexFile(idxFile);
        fm->fclose(idxFile);
    } else {
        printf("Cannot open index file.\n");
    }
}

static void trimLine(char *line)
{
    // first truncate anything behind a semicolon
    int len = strlen(line);
    for (int i=0;i<len;i++) {
        if (line[i] == ';') {
            line[i] = 0;
            break;
        }
    }

    // then, remove trailing spaces
    len = strlen(line);
    for (int i=len-1; i >= 0; i--) {
        if ((line[i] == ' ')||(line[i] == '\t')) {
            line[i] = 0;
        } else {
            break;
        }
    }
}

static void parseLine(char *line, char *name, int len, uint32_t *offset)
{
    char *rest;
    uint32_t value = strtol(line, &rest, 0);
    *offset = value;
    if (*rest) {
        rest++;
    }
    name[len-1] = 0;
    strncpy(name, rest, len-1);
}

void FileTypeTap :: parseIndexFile(File *f)
{
    uint32_t size = f->get_size();
    if ((size > 8192) || (size < 8)) { // max 8K index file
        return;
    }
    char *buffer = new char[size+1];
    char *linebuf = new char[80];
    char *name;

    uint32_t transferred;
    f->read(buffer, size, &transferred);
    buffer[size] = 0;

    uint32_t offset;

    int index = 0;
    while(index < size) {
        index = read_line(buffer, index, linebuf, 80);
        trimLine(linebuf);
        if (strlen(linebuf) > 0) {
            TapIndexEntry *entry = new TapIndexEntry;
            parseLine(linebuf, entry->name, 28, &(entry->offset));
            tapIndices.append(entry);
        }
    }
    delete linebuf;
    delete buffer;
    indexValid = true;
}

int FileTypeTap :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_C2N_STREAMER) {
        list.append(new Action("Enter (index)", FileTypeTap :: enter_st, TAPFILE_INDEX, 0 ));
        count++;
        list.append(new Action("Run Tape", FileTypeTap :: execute_st, TAPFILE_RUN, 0 ));
        count++;
        list.append(new Action("Write to Tape", FileTypeTap :: execute_st, TAPFILE_WRITE, 0 ));
        count++;
        list.append(new Action("Start Tape", FileTypeTap :: execute_st, TAPFILE_START, 0 ));
        count++;

/*
            for(int i=0; i < tapIndices.get_elements(); i++) {
                list.append(new Action(tapIndices[i]->name, FileTypeTap :: execute_st, TAPFILE_START, tapIndices[i]->offset ));
                count++;
            }
*/
//        list.append(new Action("Alt. Write", FileTypeTap :: execute_st, TAPFILE_WRITE2, 0 ));
//        count++;
    }
    return count;
}

void BrowsableTapEntry :: fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Start From Here", FileTypeTap :: execute_st, TAPFILE_START_PARENT, tiEntry->offset ));
    list.append(new Action("Run From Here",   FileTypeTap :: execute_st, TAPFILE_RUN_PARENT, tiEntry->offset ));
    list.append(new Action("Write From Here", FileTypeTap :: execute_st, TAPFILE_WRITE_PARENT, tiEntry->offset ));
}

FileType *FileTypeTap :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "TAP")==0)
        return new FileTypeTap(obj);
    return NULL;
}

int FileTypeTap :: getCustomBrowsables(Browsable *parentBrowsable, IndexedList<Browsable *> &list)
{
    if (list.get_elements())
        return 0; // List is not empty; I have done this before.

    if (!indexValid) { // no need to read again
        this->readIndexFile();
    }
    if (indexValid) {
        for(int i=0; i < tapIndices.get_elements(); i++) {
            list.append(new BrowsableTapEntry(parentBrowsable, tapIndices[i]));
        }
        return 1; // OK
    }
    // too bad, there is no index file
    return -1;
}

SubsysResultCode_e FileTypeTap :: enter_st(SubsysCommand *cmd)
{
    if (cmd->user_interface) {
        int ret = cmd->user_interface->enterSelection();
        if (ret < 0) {
            cmd->user_interface->popup("No Index file found", BUTTON_OK);
        }
        return SSRET_OK;
    }
    return SSRET_NO_USER_INTERFACE;
}

SubsysResultCode_e FileTypeTap :: execute_st(SubsysCommand *cmd)
{
	FRESULT fres;
	uint8_t read_buf[20];
	const char *signature = "C64-TAPE-RAW";
	uint32_t *pul;
	uint32_t bytes_read;
	SubsysCommand *c64_command;
	
	tape_controller->stop();
	tape_controller->close();

	int functionID = cmd->functionID & 0x7FFF;
	const char *fn = cmd->filename.c_str();

	if (cmd->functionID & 0x8000) {
	    fn = "";
	}

	File *file = 0;
	fres = FileManager :: getFileManager() -> fopen(cmd->path.c_str(), fn, FA_READ, &file);
	if(!file) {
		cmd->user_interface->popup("Can't open TAP file.", BUTTON_OK);
		return SSRET_CANNOT_OPEN_FILE;
	}
	fres = file->read(read_buf, 20, &bytes_read);
	if(fres != FR_OK) {
		cmd->user_interface->popup("Error reading TAP file header.", BUTTON_OK);
		return SSRET_FILE_READ_FAILED;
	}
	if((bytes_read != 20) || (memcmp(read_buf, signature, 12))) {
		cmd->user_interface->popup("TAP file: invalid signature.", BUTTON_OK);
		return SSRET_ERROR_IN_FILE_FORMAT;
	}
	pul = (uint32_t *)&read_buf[16];
	tape_controller->set_file(file, le_to_cpu_32(*pul), int(read_buf[12]), cmd->mode); // the mode parameter holds the offset in the file
	file = NULL; // after set file, the tape controller is now owner of the File object :)

	switch(functionID) {
	case TAPFILE_START:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_UNFREEZE, 0, "", "");
        c64_command->execute();
        vTaskDelay(50);
        //if(cmd->user_interface->popup("Tape emulation starts now..", BUTTON_OK | BUTTON_CANCEL) == BUTTON_OK) {
			tape_controller->start(1);
		//}
        printf("Tape emulation started.\n");
		break;
	case TAPFILE_RUN:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_LOAD_RUN, "A", "");
        c64_command->execute();
		tape_controller->start(1);
		break;
    case TAPFILE_WRITE:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_RECORD, "A", "");
        c64_command->execute();
		tape_controller->start(2);
		break;
    case TAPFILE_WRITE2:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_RECORD, "A", "");
        c64_command->execute();
		tape_controller->start(3);
		break;
    default:
		break;
	}
	return SSRET_OK;
}
