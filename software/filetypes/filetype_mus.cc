/*
 * filetype_mus.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *    Scott Hutter <scott.hutter@gmail.com>
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

#include "filetype_mus.h"
#include "directory.h"
#include "filemanager.h"
#include "c1541.h"
#include "c64.h"
#include <ctype.h>

/* Drives to mount on */
extern C1541 *c1541_A;

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_mus(FileType :: getFileTypeFactory(), FileTypeMUS :: test_type);

/*********************************************************************/
/* PRG File Browser Handling                                         */
/*********************************************************************/
#define PRGFILE_RUN       0x2201
#define PRGFILE_LOAD      0x2202
#define PRGFILE_DMAONLY   0x2205
#define PRGFILE_MOUNT_RUN 0x2203
#define PRGFILE_MOUNT_REAL_RUN 0x2204

FileTypeMUS :: FileTypeMUS(BrowsableDirEntry *n, bool hdr)
{
    node = n;
    has_header = hdr;
}

FileTypeMUS :: ~FileTypeMUS()
{
}

int FileTypeMUS :: fetch_context_items(IndexedList<Action *> &list)
{
	int mode = (has_header)?1:0;

	int count = 0;
	if (!c64->exists()) {
		return 0;
	}

	list.append(new Action("Play Tune",  FileTypeMUS :: execute_st, PRGFILE_RUN, 0)); //mode));
	count = 1;

	if (!c1541_A)
		return count;
}

// static member
FileType *FileTypeMUS :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "MUS")==0)
        return new FileTypeMUS(obj, false);
    else
        return NULL;
}

bool FileTypeMUS :: check_header(File *f, bool has_header)
{
    return false;        
}

int FileTypeMUS :: execute_st(SubsysCommand *cmd)
{
	printf("MUS Select: %4x\n", cmd->functionID);
	File *filePlugin = 0;
    File *fileData = 0;
	SubsysCommand *c64_command;
	
   	int run_code = RUNCODE_DMALOAD_RUN;

    const char *path = cmd->path.c_str();
    const char *name = cmd->filename.c_str();
    const char *plugin = "mus.prg";
    printf("DMA Load.. %s\n", name);

    FileManager *fm = FileManager :: getFileManager();

    // etract the root (theres probably an easier way to do this)
    char pluginsPath[120];
    int idx = 0;
    do
    {
        pluginsPath[idx] = cmd->path.c_str()[idx];
        idx++;
    } while(cmd->path.c_str()[idx] != '/' && cmd->path.c_str()[idx] != 0);
    pluginsPath[idx] = 0;

    strcat(pluginsPath, "/ultimate-plugins/");
    
    FRESULT fmus = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &fileData);
    
    if(fileData) {
        uint32_t size = fileData->get_size();
        uint8_t *buffer = new uint8_t[size+1];
    	uint32_t bytes_read = 0;
        uint16_t header = 0x4000;
        uint16_t filedata_addr = 0x4100;

    	fileData->read(buffer, size, &bytes_read);
        fm->fclose(fileData);

        int tmpmode = C64_MODE;
        C64_MODE = MODE_NORMAL;
        memcpy((void *)(C64_MEMORY_BASE + filedata_addr), buffer+2, bytes_read-2);  
        C64_MODE = tmpmode; 

        FRESULT fres = fm->fopen(pluginsPath, plugin, FA_READ, &filePlugin);

        if(filePlugin) {
        
            tmpmode = C64_MODE;
            C64_MODE = MODE_NORMAL;
            memcpy((void *)(C64_MEMORY_BASE + header), name, strlen(name));  
            C64_POKE(header + strlen(name), 0);
            C64_MODE = tmpmode; 

            c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DMA_LOAD, run_code, pluginsPath, plugin);
            c64_command->execute();
        } else
        {
            cmd->user_interface->popup("MUS plugin not found.", BUTTON_OK);
        }
        

    } else {
        printf("Error opening file. %s\n", FileSystem :: get_error_string(fmus));
        return -2;
    }
    return 0;
}
