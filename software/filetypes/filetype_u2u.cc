/*
 * filetype_u2u.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2015 Gideon Zweijtzer <info@1541ultimate.net>
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

#include "filetype_u2u.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FactoryRegistrator<FileInfo *, FileType *> tester_u2u(Globals :: getFileTypeFactory(), FileTypeUpdate :: test_type);

#define UPDATE_RUN 0x7501

/*************************************************************/
/* Update File Browser Handling                              */
/*************************************************************/

FileTypeUpdate :: FileTypeUpdate(CachedTreeNode *node)
{
    this->node = node;
	printf("Creating Update type from info: %s\n", node->get_name());
}

FileTypeUpdate :: ~FileTypeUpdate()
{
}


int FileTypeUpdate :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
	list.append(new Action("Run Update", FileTypeUpdate :: execute, node, (void *)UPDATE_RUN ));
	count++;
    return count;
}

FileType *FileTypeUpdate :: test_type(FileInfo *inf)
{
    if(strcmp(inf->extension, "U2U")==0)
        return new FileTypeUpdate(inf);
    return NULL;
}

void (*function)();

void jump_run(uint32_t a)
{
    uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    ioWrite8(ITU_IRQ_GLOBAL, 0);
    function();
    while(1)
    	;
}

void FileTypeUpdate :: execute(void *obj, void *param)
{
	File *file;
	uint32_t bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    int remain;

	static char buffer[36];
    uint8_t *dest;

    CachedTreeNode *node = (CachedTreeNode *)obj;
    FileManager *fm = FileManager :: getFileManager();

	sectors = (node->get_file_info()->size >> 9);
	if(sectors >= 128)
		progress = true;
	secs_per_step = (sectors + 31) >> 5;
	bytes_per_step = secs_per_step << 9;
	remain = node->get_file_info()->size;

	printf("Update Load.. %s\n", node->get_name());
	file = fm->fopen_node(node, FA_READ);
	if(file) {
		total_bytes_read = 0;
		// load file in REU memory
		if(progress) {
			user_interface->show_progress("Loading Update..", 32);
			dest = (uint8_t *)(REU_MEMORY_BASE);
			while(remain >= 0) {
				file->read(dest, bytes_per_step, &bytes_read);
				total_bytes_read += bytes_read;
				user_interface->update_progress(NULL, 1);
				remain -= bytes_per_step;
				dest += bytes_per_step;
			}
			user_interface->hide_progress();
		} else {
			file->read((void *)(REU_MEMORY_BASE), (REU_MAX_SIZE), &bytes_read);
			total_bytes_read += bytes_read;
		}
		fm->fclose(file);
		file = NULL;
		if ((*(uint32_t *)REU_MEMORY_BASE) != 0x3021FFD8) {
			user_interface->popup("Signature check failed.", BUTTON_OK);
		} else {
			jump_run(REU_MEMORY_BASE);
		}
	} else {
		printf("Error opening file.\n");
	}
}

