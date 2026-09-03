/*
 * filetype_vpl.cc
 *
 * Written by 
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2021 Gideon Zweijtzer <info@1541ultimate.net>
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

#include "filetype_vpl.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"
#include "browsable_root.h"
#include "pattern.h"

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_vpl(FileType :: getFileTypeFactory(), FileTypePalette :: test_type);

/*************************************************************/
/* Update File Browser Handling                              */
/*************************************************************/

FileTypePalette :: FileTypePalette(BrowsableDirEntry *br)
{
	printf("Creating Palette type from info: %s\n", br->getName());
	browsable = br;
}

FileTypePalette :: ~FileTypePalette()
{
}


int FileTypePalette :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
	list.append(new Action("Apply Palette", FileTypePalette :: execute, 0 ));
    list.append(new Action("Set as Default", FileTypePalette :: executeFlash, 0 ));
	count++;
    return count;
}

FileType *FileTypePalette :: test_type(BrowsableDirEntry *br)
{
	FileInfo *inf = br->getInfo();
	if(strcmp(inf->extension, "VPL")==0)
        return new FileTypePalette(br);
    return NULL;
}

static void trimLine(char *line)
{
    // first truncate anything behind a #
    int len = strlen(line);
    for (int i=0;i<len;i++) {
        if (line[i] == '#') {
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

bool FileTypePalette :: parseVplFile(File *f, uint8_t rgb[16][3])
{
    uint32_t size = f->get_size();
    if ((size > 8192) || (size < 8)) { // max 8K index file
        return false;
    }
    char *buffer = new char[size+1];
    char *linebuf = new char[80];

    uint32_t transferred;
    f->read(buffer, size, &transferred);
    buffer[size] = 0;

    uint32_t offset;
    int index = 0;

    int r, g, b, idx = 0;
    while(index < size) {
        //uint32_t idx_old = index;
        index = read_line(buffer, index, linebuf, 80);
        trimLine(linebuf);
        if (strlen(linebuf) > 0) {
            int num_values = sscanf(linebuf, "%x %x %x", &r, &g, &b);
            //printf("Line (%d-%d): '%s' => %d\n", idx_old, index, linebuf, num_values);
            if (num_values == 3) {
                rgb[idx][0] = (uint8_t)r;
                rgb[idx][1] = (uint8_t)g;
                rgb[idx][2] = (uint8_t)b;
                idx ++;
                if (idx == 16) {
                    break;
                }
            } else {
                break;
            }
        }
    }
    delete linebuf;
    delete buffer;
    return (idx == 16);
}

SubsysResultCode_e FileTypePalette :: execute(SubsysCommand *cmd)
{
    if (!U64Config :: load_palette_vpl(cmd->path.c_str(), cmd->filename.c_str())) {
        if (cmd->user_interface) {
            cmd->user_interface->popup("Invalid palette file.", BUTTON_OK);
            return SSRET_ERROR_IN_FILE_FORMAT;
        }
    }
    return SSRET_OK;
}

SubsysResultCode_e FileTypePalette :: executeFlash(SubsysCommand *cmd)
{
    FileManager *fm = FileManager::getFileManager();

    fm->create_dir(DATA_DIRECTORY); // just in case it doesn't exist
    char fnbuf[32];
    truncate_filename(cmd->filename.c_str(), fnbuf, 30);
    FRESULT fres = fm->fcopy(cmd->path.c_str(), cmd->filename.c_str(), DATA_DIRECTORY, fnbuf, true);
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem::get_error_string(fres), BUTTON_OK);
        return SSRET_DISK_ERROR;
    }
    U64Config :: getConfigurator() -> set_palette_filename(fnbuf);
    return SSRET_OK;
}
