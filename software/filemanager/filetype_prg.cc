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
FileTypePRG tester_prg(file_type_factory);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
#define PRGFILE_MOUNT_RUN 0x2203
#define PRGFILE_MOUNT_REAL_RUN 0x2204

FileTypePRG :: FileTypePRG(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    info = NULL;
}

FileTypePRG :: FileTypePRG(PathObject *par, FileInfo *fi, bool hdr) : FileDirEntry(par, fi)
{
    printf("Creating PRG type from info: %s\n", fi->lfname);
    has_header = hdr;
}

FileTypePRG :: ~FileTypePRG()
{
}

int FileTypePRG :: fetch_children(void)
{
  return -1;
}

int FileTypePRG :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Run",  PRGFILE_RUN));
    list.append(new MenuItem(this, "Load", PRGFILE_LOAD));
	int count = 2;

	// a little dirty
	FileInfo *inf = parent->get_file_info();
    if(strcmp(inf->extension, "D64")==0) {
    	list.append(new MenuItem(this, "Mount & Run", PRGFILE_MOUNT_RUN));
    	count++;
    	list.append(new MenuItem(this, "Real Run", PRGFILE_MOUNT_REAL_RUN));
    	count++;
    }

    return count + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypePRG :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "PRG")==0)
        return new FileTypePRG(obj->parent, inf, false);
    if(inf->extension[0] == 'P') {
        if(isdigit(inf->extension[1]) && isdigit(inf->extension[2])) {
            return new FileTypePRG(obj->parent, inf, true);
        }
    }
    return NULL;
}

bool FileTypePRG :: check_header(File *f)
{
    static char p00_header[0x1A]; 

    if(!has_header)
        return true;
    UINT bytes_read;
    FRESULT res = f->read(p00_header, 0x1A, &bytes_read);
    if(res != FR_OK)
        return false;
    if(strncmp(p00_header, "C64File", 7))
        return false;
    return true;        
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
		FileDirEntry :: execute(selection);
        return;
    }

    printf("DMA Load.. %s\n", get_name());
    file = root.fopen(this, FA_READ);
    if(file) {
        if(check_header(file)) {

            char *name = get_name();
            int len = strlen(name);

            /* heuristic name length (kill trailing spaces) */
            while (len > 1) {
                if (name[len-1] != 0x20) {
                    break;
                }
                len--;
            }
            C64Event::prepare_dma_load(file, name, len, run_code);

            if (run_code & RUNCODE_MOUNT_BIT) {
                inf = parent->get_file_info();
                d64 = root.fopen(parent, FA_READ);
                if(d64) { // mount read only only if file is read only
                    drive_command = new t_drive_command;
                    drive_command->command = MENU_1541_MOUNT;
                    drive_command->protect = ((inf->attrib & AM_RDO) != 0);
                    drive_command->file = d64;
                    push_event(e_object_private_cmd, c1541_A, (int)drive_command);
                }
                else
                    printf("Can't open D64 file..\n");
            }

            C64Event::perform_dma_load(file, run_code);

        } else {
            printf("Header of P00 file not correct.\n");
            root.fclose(file);
        }
    } else {
        printf("Error opening file.\n");
    }

}
