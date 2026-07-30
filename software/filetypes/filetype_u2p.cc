/*
 * filetype_u2p.cc
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

#include "filetype_u2p.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"
#include "browsable_root.h"
#include "product.h"

extern "C" {
	#include "dump_hex.h"
}

#ifdef U64
#ifndef RISCV
#include "esp32.h"
#endif
#endif
#include "acia.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_u2p(FileType :: getFileTypeFactory(), FileTypeUpdate :: test_type);

#define UPDATE_RUN 0x7501

/*************************************************************/
/* Update File Browser Handling                              */
/*************************************************************/

FileTypeUpdate :: FileTypeUpdate(BrowsableDirEntry *br, int format)
{
	printf("Creating Update type from info: %s\n", br->getName());
	browsable = br;
    this->format = format;
}

FileTypeUpdate :: ~FileTypeUpdate()
{
}


int FileTypeUpdate :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
	list.append(new Action("Run Update", FileTypeUpdate :: execute, UPDATE_RUN, format ));
	count++;
    return count;
}

FileType *FileTypeUpdate :: test_type(BrowsableDirEntry *br)
{
	FileInfo *inf = br->getInfo();
	uint32_t cap = getFpgaCapabilities();

    if(strcmp(inf->extension, "CFW")==0) { // Commodore compatible multi-target firmware
        return new FileTypeUpdate(br, 1);
    }

    if (getFpgaType() >= 3) {
        return NULL; // From 3 onwards, we only support "cfw"
    }

    if(strcmp(inf->extension, getProductUpdateFileExtension())==0)
        return new FileTypeUpdate(br, 0);
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

SubsysResultCode_e FileTypeUpdate :: execute(SubsysCommand *cmd)
{
	File *file = 0;
    int remain;

    FileManager *fm = FileManager :: getFileManager();
    FileInfo inf(32);
    fm->fstat(cmd->path.c_str(), cmd->filename.c_str(), inf);
	remain = inf.size;

	printf("Update Load.. %s\n", cmd->filename.c_str());
	FRESULT fres = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
    if ((fres != FR_OK) || !file) {
		printf("Error opening file.\n");
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
		return SSRET_CANNOT_OPEN_FILE;
    }

    uint32_t start = 0;
    if (cmd->mode == 0) {
        start = load_format_0(cmd, file, remain);
    } else {
        start = load_format_1(cmd, file, remain);
    }
    fm->fclose(file);
    // this is a hack!
    cmd->user_interface->host->release_ownership();
    file = NULL;
#if U64
#ifndef RISCV
    esp32.Quit();
#endif
#endif
#ifndef RECOVERYAPP
    acia.deinit();
#endif
    if (start) {
        jump_run(start);
    }
    return SSRET_OK;
}


uint32_t FileTypeUpdate :: load_format_0(SubsysCommand *cmd, File *file, uint32_t remain)
{
	uint32_t bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    uint8_t *dest;

	struct {
		uint32_t load;
		uint32_t length;
		uint32_t start;
	} header;
	uint32_t start = 0;

    total_bytes_read = 0;
    // load file in REU memory
    while(remain) {
        file->read(&header, 12, &bytes_read);
        remain -= bytes_read;
        total_bytes_read += bytes_read;
        if (header.start)
            start = header.start;
        if (bytes_read == 12) {
            dest = (uint8_t *)(header.load);
            file->read(dest, header.length, &bytes_read);
            remain -= bytes_read;
            total_bytes_read += bytes_read;
        }
    }
    return start;
}

uint32_t FileTypeUpdate :: load_format_1(SubsysCommand *cmd, File *file, uint32_t remain)
{
	uint32_t bytes_read;
	bool progress;
	int sectors;
    int secs_per_step;
    int bytes_per_step;
    int total_bytes_read;
    uint8_t *dest;

#define FINGERPRINT 0x4115ADDE

	struct {
        uint32_t fingerprint; // 0xDEAD1541
        char prod_family[16]; // If set, only the product family needs to match
        char prod_name[16];   // It family is not set, the exact product name needs to match
    } pre_header;

    file->read(&pre_header, sizeof(pre_header), &bytes_read);
    remain -= bytes_read;
    total_bytes_read += bytes_read;

    if (bytes_read != sizeof(pre_header) || pre_header.fingerprint != FINGERPRINT) {
        cmd->user_interface->popup("Invalid Update Image", BUTTON_OK);
        return 0;
    }
    if (pre_header.prod_family[0]) {
        if (strncmp(pre_header.prod_family, getProductFamily(), 16) != 0) {
            cmd->user_interface->popup("Wrong Product Family", BUTTON_OK);
            return 0;
        }
    }
    if (pre_header.prod_name[0]) {
        if (strncmp(pre_header.prod_name, getProductString(), 16) != 0) {
            cmd->user_interface->popup("Update for Wrong Product", BUTTON_OK);
            return 0;
        }
    }
    return load_format_0(cmd, file, remain);
}
