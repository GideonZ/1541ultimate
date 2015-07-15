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
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_prg(Globals :: getFileTypeFactory(), FileTypePRG :: test_type);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
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
	count += 2;

	if (!c1541_A)
		return count;

	// if this file is inside of a D64, then there should be a mount point already of this D64.
	MountPoint *mp = FileManager :: getFileManager() -> find_mount_point(node->getPath()->get_path());
	if(mp) {
    	list.append(new Action("Mount & Run", FileTypePRG :: execute_st, PRGFILE_MOUNT_RUN, mode));
    	count++;
    	list.append(new Action("Real Run", FileTypePRG :: execute_st, PRGFILE_MOUNT_REAL_RUN, mode));
    	count++;
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
	File *file, *d64;
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
    default:
        return -1;
    }

    const char *name = cmd->filename.c_str();
    printf("DMA Load.. %s\n", name);
    FileManager *fm = FileManager :: getFileManager();
    file = fm->fopen(cmd->path.c_str(), name, FA_READ);
    if(file) {
        if(check_header(file, cmd->mode)) {

            if (run_code & RUNCODE_MOUNT_BIT) {
            	MountPoint *mp = fm -> find_mount_point(cmd->path.c_str());
            	if (mp) {
					Path *d64_path = fm -> get_new_path("temp_prg");
					d64_path->cd(cmd->path.c_str());
					d64_path->cd("..");
					const char *pathname = d64_path->get_path();
					const char *filename = mp->get_file()->getFileInfo()->lfname;
					printf("Mounting %s in %s to drive A\n", filename, pathname);
					drive_command = new SubsysCommand(cmd->user_interface, SUBSYSID_DRIVE_A, MENU_1541_MOUNT, 0, pathname, filename);
					drive_command->execute();
					fm->release_path(d64_path);
            	} else {
            		printf("INTERR: Can't find mount point anymore?\n");
            	}
            }
            c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD, run_code, cmd->path.c_str(), cmd->filename.c_str());
            c64_command->execute();

        } else {
            printf("Header of P00 file not correct.\n");
            fm->fclose(file);
        }
    } else {
        printf("Error opening file.\n");
        return -2;
    }
    return 0;
}
