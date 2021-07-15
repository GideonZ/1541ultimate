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

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_vpl(FileType :: getFileTypeFactory(), FileTypePalette :: test_type);

#define UPDATE_RUN 0x7501

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
	list.append(new Action("Apply Palette", FileTypePalette :: execute, UPDATE_RUN ));
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

// defined in filetype.tap
uint32_t readLine(const char *buffer, uint32_t index, char *out, int outlen);

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

static void parseVplFile(File *f, uint8_t rgb[16][3])
{
    uint32_t size = f->get_size();
    if ((size > 8192) || (size < 8)) { // max 8K index file
        return;
    }
    char *buffer = new char[size+1];
    char *linebuf = new char[80];

    uint32_t transferred;
    f->read(buffer, size, &transferred);
    buffer[size] = 0;

    uint32_t offset;
    uint32_t index = 0;

    int r, g, b, idx = 0;
    while(index < size) {
        index = readLine(buffer, index, linebuf, 80);
        trimLine(linebuf);
        if (strlen(linebuf) > 0) {
            sscanf(linebuf, "%x %x %x", &r, &g, &b);
            //printf("%d %d %d\n", r, g, b);
            rgb[idx][0] = (uint8_t)r;
            rgb[idx][1] = (uint8_t)g;
            rgb[idx][2] = (uint8_t)b;
            idx ++;
            if (idx == 16) {
                break;
            }
        }
    }
    delete linebuf;
    delete buffer;
}

int FileTypePalette :: execute(SubsysCommand *cmd)
{
    File *file = NULL;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    uint8_t rgb[16][3];

    if(file) {
        parseVplFile(file, rgb);
        U64Config :: set_palette_rgb(rgb);
        fm->fclose(file);
    }
    return 0;
}

