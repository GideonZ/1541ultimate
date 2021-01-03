/*
 * filetype_esp.cc
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

#include "filetype_esp.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"
#include "browsable_root.h"

extern "C" {
	#include "dump_hex.h"
}

#include "wifi.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_esp(FileType :: getFileTypeFactory(), FileTypeESP :: test_type);

#define FLASH_ESP 0x8501

/*************************************************************/
/* Update File Browser Handling                              */
/*************************************************************/

FileTypeESP :: FileTypeESP(BrowsableDirEntry *br)
{
	printf("Creating Update type from info: %s\n", br->getName());
	browsable = br;
}

FileTypeESP :: ~FileTypeESP()
{
}


int FileTypeESP :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
	list.append(new Action("Flash into ESP32", FileTypeESP :: execute, FLASH_ESP ));
	count++;
    return count;
}

FileType *FileTypeESP :: test_type(BrowsableDirEntry *br)
{
    FileInfo *inf = br->getInfo();
    if (strcmp(inf->extension, "ESP") == 0)
        return new FileTypeESP(br);
    return NULL;
}

int FileTypeESP::execute(SubsysCommand *cmd)
{
    File *file = 0;
    uint32_t bytes_read;
    bool progress;
    int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    int remain;

    uint8_t *dest;

    FileManager *fm = FileManager::getFileManager();
    FileInfo inf(32);
    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), inf);
    remain = inf.size;

    printf("Multipart ESP application Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    struct
    {
        uint32_t signature;
        uint32_t length;
        uint32_t address;
    } header;
    uint32_t start = 0;

    if (file) {
        total_bytes_read = 0;
        cmd->user_interface->popup("ESP Programming now starts.", BUTTON_OK);

        wifi.doDownloadWrap(true);
        while (remain) {
            file->read(&header, 12, &bytes_read);
            remain -= bytes_read;
            total_bytes_read += bytes_read;
            if (bytes_read != 12) {
                cmd->user_interface->popup("Could not read header.", BUTTON_OK);
                break;
            }
            if (header.signature != 0x4d474553) {
                cmd->user_interface->popup("Incorrect signature in file.", BUTTON_OK);
                break;
            }
            dest = new uint8_t[header.length];
            if (!dest) {
                cmd->user_interface->popup("Couldn't alloc mem to load segment.", BUTTON_OK);
                break;
            }
            file->read(dest, header.length, &bytes_read);
            remain -= bytes_read;
            total_bytes_read += bytes_read;
            wifi.doDownload(dest, header.address, header.length, true);
        }
        wifi.doDownloadWrap(false);
        fm->fclose(file);
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem::get_error_string(fres), BUTTON_OK);
        return -1;
    }
    return 0;
}
