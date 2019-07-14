/*
 * filetype_prg.cc
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

#include "filetype_prg.h"
#include "directory.h"
#include "filemanager.h"
#include "c1541.h"
#include "c64.h"
#include <ctype.h>

/* Drives to mount on */
extern C1541 *c1541_A;

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_prg(FileType :: getFileTypeFactory(), FileTypePRG :: test_type);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
#define PRGFILE_DMAONLY   0x2205
#define PRGFILE_MOUNT_RUN 0x2203
#define PRGFILE_MOUNT_REAL_RUN 0x2204

FileTypePRG :: FileTypePRG(BrowsableDirEntry *n, bool hdr)
{
    node = n;
    has_header = hdr;
}

FileTypePRG :: ~FileTypePRG()
{
}

int FileTypePRG :: fetch_context_items(IndexedList<Action *> &list)
{
	int mode = (has_header)?1:0;

	int count = 0;
	if (!c64->exists()) {
		return 0;
	}

	list.append(new Action("Run",  FileTypePRG :: execute_st, PRGFILE_RUN, mode));
	list.append(new Action("Load", FileTypePRG :: execute_st, PRGFILE_LOAD, mode));
	list.append(new Action("DMA",  FileTypePRG :: execute_st, PRGFILE_DMAONLY, mode));
	count += 3;

	if (!c1541_A)
		return count;

	BrowsableDirEntry *parentEntry = (BrowsableDirEntry *)(node->getParent());
	FileInfo *parentInfo = parentEntry->getInfo();
	if (strcasecmp("D64", parentInfo->extension) == 0) {
	    // if this file is inside of a D64, then there should be a mount point already of this D64,
	    // if not, we cannot mount the disk.

	    MountPoint *mp = FileManager :: getFileManager() -> find_mount_point(parentInfo, 0, 0, 0);
        if(mp) {
            list.append(new Action("Mount & Run", FileTypePRG :: execute_st, PRGFILE_MOUNT_RUN, mode));
            count++;
            list.append(new Action("Real Run", FileTypePRG :: execute_st, PRGFILE_MOUNT_REAL_RUN, mode));
            count++;
        }
	}
    return count;
}

// static member
FileType *FileTypePRG :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "PRG")==0)
        return new FileTypePRG(obj, false);
    if(inf->extension[0] == 'P') {
        if(isdigit(inf->extension[1]) && isdigit(inf->extension[2])) {
            return new FileTypePRG(obj, true);
        }
    }
    return NULL;
}

bool FileTypePRG :: check_header(File *f, bool has_header)
{
    static char p00_header[0x1A]; 

    if(!has_header)
        return true;
    uint32_t bytes_read;
    FRESULT res = f->read(p00_header, 0x1A, &bytes_read);
    if(res != FR_OK)
        return false;
    if(strncmp(p00_header, "C64File", 7))
        return false;
    return true;        
}

int FileTypePRG :: execute_st(SubsysCommand *cmd)
{
	printf("PRG Select: %4x\n", cmd->functionID);
	File *file = 0, *d64;
	FileInfo *inf;
	SubsysCommand *drive_command;
	SubsysCommand *c64_command;
	
    int run_code = -1;
	switch(cmd->functionID) {
	case PRGFILE_RUN:
        run_code = RUNCODE_DMALOAD_RUN;
        break;
    case PRGFILE_LOAD:
        run_code = RUNCODE_DMALOAD;
        break;
    case PRGFILE_MOUNT_RUN:
        run_code = RUNCODE_MOUNT_DMALOAD_RUN;
        break;
    case PRGFILE_MOUNT_REAL_RUN:
        run_code = RUNCODE_MOUNT_LOAD_RUN;
        break;
    case PRGFILE_DMAONLY:
    	run_code = 0;
    	break;
    default:
        return -1;
    }

    const char *name = cmd->filename.c_str();
    printf("DMA Load.. %s\n", name);
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(cmd->path.c_str(), name, FA_READ, &file);
    if(file) {
        if(check_header(file, cmd->mode)) {

            if (run_code & RUNCODE_MOUNT_BIT) {
            	printf("Runcode mount bit set. trying to find mount point '%s' resulted: ", cmd->path.c_str());
            	FileInfo info(32);
            	fres = fm->fstat(cmd->path.c_str(), info);
            	printf("%s\n", FileSystem :: get_error_string(fres));

            	if (fres == FR_OK) {
                	MountPoint *mp = fm -> find_mount_point(&info, 0, 0, 0);
                	if (mp) {
    					printf("Mounting %s to drive A\n", cmd->path.c_str());

    					drive_command = new SubsysCommand(cmd->user_interface, SUBSYSID_DRIVE_A, MENU_1541_MOUNT, 0, 0, cmd->path.c_str());
    					drive_command->execute();
                	} else {
                		printf("INTERR: Can't find mount point anymore?\n");
                	}
            	}
            }
            if (run_code) {
            	c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD, run_code, cmd->path.c_str(), cmd->filename.c_str());
            } else {
            	c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD_RAW, run_code, cmd->path.c_str(), cmd->filename.c_str());
            }
        	c64_command->execute();

        } else {
            printf("Header of P00 file not correct.\n");
            fm->fclose(file);
        }
    } else {
        printf("Error opening file. %s\n", FileSystem :: get_error_string(fres));
        return -2;
    }
    return 0;
}
