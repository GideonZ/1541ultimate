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
FactoryRegistrator<CachedTreeNode *, FileType *> tester_prg(Globals :: getFileTypeFactory(), FileTypePRG :: test_type);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
#define PRGFILE_MOUNT_RUN 0x2203
#define PRGFILE_MOUNT_REAL_RUN 0x2204

FileTypePRG :: FileTypePRG(CachedTreeNode *n, bool hdr)
{
    printf("Creating PRG type from info: %s\n", n->get_name());
    node = n;
    has_header = hdr;
}

FileTypePRG :: ~FileTypePRG()
{
}

int FileTypePRG :: fetch_context_items(IndexedList<Action *> &list)
{
    list.append(new Action("Run",  FileTypePRG :: execute_st, this, (void *)PRGFILE_RUN));
    list.append(new Action("Load", FileTypePRG :: execute_st, this, (void *)PRGFILE_LOAD));
	int count = 2;

	if (!c1541_A)
		return count;

	FileInfo *pinfo = node->parent->get_file_info();
    if(strcmp(pinfo->extension, "D64")==0) {
    	list.append(new Action("Mount & Run", FileTypePRG :: execute_st, this, (void *)PRGFILE_MOUNT_RUN));
    	count++;
    	list.append(new Action("Real Run", FileTypePRG :: execute_st, this, (void *)PRGFILE_MOUNT_REAL_RUN));
    	count++;
    }
    return count;
}

// static member
FileType *FileTypePRG :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "PRG")==0)
        return new FileTypePRG(obj, false);
    if(inf->extension[0] == 'P') {
        if(isdigit(inf->extension[1]) && isdigit(inf->extension[2])) {
            return new FileTypePRG(obj, true);
        }
    }
    return NULL;
}

bool FileTypePRG :: check_header(File *f)
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

void FileTypePRG :: execute_st(void *obj, void *param)
{
	// fall through in original execute method
	((FileTypePRG *)obj)->execute((int)param);
}

void FileTypePRG :: execute(int selection)
{
	printf("PRG Select: %4x\n", selection);
	File *file, *d64;
	FileInfo *inf;
	t_drive_command *drive_command;
	
    int run_code = -1;
	switch(selection) {
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
        return;
    }

    printf("DMA Load.. %s\n", node->get_name());
    FileManager *fm = FileManager :: getFileManager();
    file = fm->fopen_node(node, FA_READ);
    if(file) {
        if(check_header(file)) {

            char *name = node->get_name();
            int len = strlen(name);

            /* heuristic name length (kill trailing spaces) */
            while (len > 1) {
                if (name[len-1] != 0x20) {
                    break;
                }
                len--;
            }

            if (run_code & RUNCODE_MOUNT_BIT) {
				drive_command = new t_drive_command;
				drive_command->command = MENU_1541_MOUNT;
				drive_command->node = node->parent;
				c1541_A->executeCommand(drive_command);
			}

            C64Event::prepare_dma_load(file, name, len, run_code);
            C64Event::perform_dma_load(file, run_code);

        } else {
            printf("Header of P00 file not correct.\n");
            fm->fclose(file);
        }
    } else {
        printf("Error opening file.\n");
    }
}
