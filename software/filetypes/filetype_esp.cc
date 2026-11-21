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

#include "esp32.h"
#include "esp_image.h"

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

static void status_callback(void *user)
{
    UserInterface *ui = (UserInterface *)user;
    ui->update_progress(NULL, 1);
}

// Reads len bytes of the file at pos, for esp_image_check
static bool esp_read_at(void *context, uint32_t pos, void *buffer, uint32_t len)
{
    File *file = (File *)context;
    uint32_t bytes_read = 0;
    if (file->seek(pos) != FR_OK) {
        return false;
    }
    return (file->read(buffer, len, &bytes_read) == FR_OK) && (bytes_read == len);
}

SubsysResultCode_e FileTypeESP::execute(SubsysCommand *cmd)
{
    File *file = 0;
    uint32_t bytes_read;
    uint8_t *dest;

    FileManager *fm = FileManager::getFileManager();
    printf("Multipart ESP application Load.. %s\n", cmd->filename.c_str());
    FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);

    struct
    {
        uint32_t signature;
        uint32_t length;
        uint32_t address;
    } header;
    bool error = false;
    if (file) {
        uint32_t filesize = file->get_size();

        // Walk the whole file by its headers before the ESP32 is touched: a file that is
        // empty, cut short or wrong is refused with the module still running its application
        EspImageStatus check = esp_image_check(filesize, esp_read_at, file);
        if (check != ESP_IMAGE_OK) {
            fm->fclose(file);
            cmd->user_interface->popup(esp_image_message(check), BUTTON_OK);
            return SSRET_INTERNAL_ERROR;
        }
        file->seek(0);

        esp32.StopApp();
        int status = esp32.Download();
        if (status != 0) {
            fm->fclose(file);
            esp32.EnableRunMode();
            esp32.StartApp();
            cmd->user_interface->popup("Could not switch to download mode.", BUTTON_OK);
            return SSRET_INTERNAL_ERROR;
        }

        cmd->user_interface->show_progress("Flashing ESP32 Application", filesize / 1024);
        uint32_t remain = filesize;
        while (remain) {
            file->read(&header, 12, &bytes_read);
            remain -= bytes_read;
            if (bytes_read != 12) {
                cmd->user_interface->hide_progress();
                cmd->user_interface->popup("Could not read header.", BUTTON_OK);
                error = true;
                break;
            }
            if (header.signature != ESP_SEGMENT_SIGNATURE) {
                cmd->user_interface->hide_progress();
                cmd->user_interface->popup("Incorrect signature in file.", BUTTON_OK);
                error = true;
                break;
            }
            dest = new uint8_t[header.length];
            if (!dest) {
                cmd->user_interface->hide_progress();
                cmd->user_interface->popup("Couldn't alloc mem to load segment.", BUTTON_OK);
                error = true;
                break;
            }
            file->read(dest, header.length, &bytes_read);
            remain -= bytes_read;
            if (bytes_read != header.length) {
                delete[] dest;
                cmd->user_interface->hide_progress();
                cmd->user_interface->popup("Could not read segment.", BUTTON_OK);
                error = true;
                break;
            }
            status = esp32.Flash(dest, header.address, header.length, status_callback, cmd->user_interface);
            if (status == 0) {
                status = esp32.VerifyFlash(dest, header.address, header.length);
            }
            delete[] dest;
            if (status != 0) {
                char errmsg[32];
                sprintf(errmsg, "Flashing Error %d", status);
                cmd->user_interface->hide_progress();
                cmd->user_interface->popup(errmsg, BUTTON_OK);
                error = true;
                break;
            }
        }
        fm->fclose(file);
        if (!error) {
            cmd->user_interface->hide_progress();
            cmd->user_interface->popup("Flashing ESP32 Success!", BUTTON_OK);
        }
        esp32.EnableRunMode();
        esp32.StartApp();
    } else {
        printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem::get_error_string(fres), BUTTON_OK);
        return SSRET_CANNOT_OPEN_FILE;
    }
    return SSRET_OK;
}
